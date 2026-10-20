#include "../runtime/resource_access.h"
#include "pch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include "spelling_repair.h"
#include "syllable_catalog.h"

constexpr uint16_t INVALID_ID = UINT16_MAX;
constexpr size_t CorrectionMaxLimit = coral::phonetics::CharacterPrior::MAX_PINYIN_LENGTH + 1;
constexpr size_t CorrectionMinLimit = coral::phonetics::CharacterPrior::MIN_PINYIN_LENGTH;
constexpr size_t MAX_BIGRAM_CACHE_SIZE = 5000;
constexpr size_t MAX_CACHE_SIZE = 10000;
constexpr float LN20 = 2.995732273553991f;	// ln(20)

inline uint32_t makeSyllablePairKey(const uint16_t id1, const uint16_t id2) {
    return (static_cast<uint32_t>(id1) << 16) | id2;
}

inline uint64_t makeSyllableTripletKey(const uint16_t id1, const uint16_t id2, const uint16_t id3) {
    return (static_cast<uint64_t>(id1) << 32) | (static_cast<uint64_t>(id2) << 16) | id3;
}

// 单字符的声母判定在候选生成的内层循环里，查集合前还要拼一个临时 string，
// 这里改成 load 时算好的 256 项查表。
const std::array<bool, 256>& singleCharInitialTable()
{
	static const std::array<bool, 256> table = [] {
		std::array<bool, 256> t{};
		for (int i = 0; i < 256; ++i)
		{
			const char c = static_cast<char>(i);
			t[static_cast<size_t>(i)] = c != 'n' && coral::phonetics::syntax::kSoftInitials.count(std::string(1, c)) > 0;
		}
		return t;
	}();
	return table;
}

// 辅助函数：判断字符是否为声母
inline bool isInitial(const char &c) {
	return singleCharInitialTable()[static_cast<unsigned char>(c)];
}

// 辅助函数：判断字符是否为韵母（非声母的字母）
inline bool isFinal(const char &c) {
	return std::isalpha(static_cast<unsigned char>(c)) && !isInitial(c);
}

inline bool compareBigram(const coral::storage::SpellingPairRecord& a, const uint32_t b) {
	return a.key < b;
}

inline bool compareTrigram(const coral::storage::SpellingTripleRecord& a, const uint64_t b) {
	return a.key < b;
}

int coral::phonetics::SpellingRepair::get_valid_initial_length(const std::string& pinyin) {
	if (pinyin.empty()) return 0;

	std::string_view sv(pinyin);
	const size_t n = sv.size();
	if (n >= 2) {
		std::string_view two = sv.substr(0, 2);
		if (coral::phonetics::syntax::kSoftInitials.count(two) > 0) {
			return 2;
		}
	}
	{
		std::string_view one = sv.substr(0, 1);
		if (coral::phonetics::syntax::kSoftInitials.count(one) > 0) {
			return 1;
		}
	}
	return 0;
}

bool coral::phonetics::SpellingRepair::load(const std::filesystem::path& modelPath, const std::string& versionId, const std::string& identifier)
{
    if (!mapper_.load(hlp::buildSHNameW(versionId, "CorrectionModel"), modelPath, identifier, coral::runtime::resource_access())) {
        LOG_ERROR("Langengine: Failed to load correction pinyin model model");
        return false;
    }

    const auto& memorySize = mapper_.size();
    const auto& memoryAddress = mapper_.data();

    if (memorySize < sizeof(coral::storage::SpellingModelHeader))
    {
        LOG_ERROR("Langengine: Failed to load correction pinyin model, the file is too small!");
        return false;
    }

    const auto basePtr = static_cast<const char*>(memoryAddress);
    const auto header = reinterpret_cast<const coral::storage::SpellingModelHeader*>(basePtr);

    if (std::string(header->magic, 8) != "PINYINLM" || header->version != 1)
    {
        LOG_ERROR("Correction pinyin header is invalid!");
        return false;
    }

    trie_.map(basePtr + header->trie_offset, static_cast<size_t>(header->trie_size));
    unigram_counts_ = reinterpret_cast<const uint64_t*>(basePtr + header->unigram_offset);
    const char* bigramPtr = basePtr + header->bigram_offset;
    bigram_count_ = *reinterpret_cast<const uint32_t*>(bigramPtr);
    bigram_data_ = reinterpret_cast<const coral::storage::SpellingPairRecord*>(bigramPtr + sizeof(uint32_t));
    const char* trigramPtr = basePtr + header->trigram_offset;
    trigram_count_ = *reinterpret_cast<const uint32_t*>(trigramPtr);
    trigram_data_ = reinterpret_cast<const coral::storage::SpellingTripleRecord*>(trigramPtr + sizeof(uint32_t));
    vocab_size_ = trie_.num_keys();
    start_id_ = get_syllable_id("<s>");
    end_id_ = get_syllable_id("</s>");
	// 预计算固定常量，避免运行时重复计算
	total_unigram_count_ = std::accumulate(unigram_counts_, unigram_counts_ + vocab_size_, 0ULL);
	calculate_discount_parameters();
    precompute_kneser_ney_counts();
    build_must_correction_rules();
	return true;
}

bool coral::phonetics::SpellingRepair::load_char_language_model(const std::filesystem::path &charLMPath, const std::string &versionId, const std::string &identifier)
{
	if (char_lm_.load(charLMPath, versionId, identifier))
	{
		LOG_INFO("Character language model loaded successfully");
        loaded_ = true;
		return true;
	}
	else
	{
		LOG_ERROR("Character language model load failed, will not use character-level scoring");
		return false;
	}
}

std::pair<std::vector<std::string>, float> coral::phonetics::SpellingRepair::predict(const std::string& text, const coral::phonetics::syntax::CorrectionMethod& correctionMethod) const
{
    if (text.empty() || !loaded_)
        return {{}, 0.0};

    const size_t textLen = text.length();
    std::vector<coral::phonetics::ParseScoreMap, coral::phonetics::support::PoolAllocator<coral::phonetics::ParseScoreMap>> dp(textLen + 1);
    std::vector<coral::phonetics::ParseIndexMap, coral::phonetics::support::PoolAllocator<coral::phonetics::ParseIndexMap>> backPointer(textLen + 1);
    dp[0][makeSyllablePairKey(start_id_, start_id_)] = 0.0f;
	auto isValidPinyin = [](const std::string_view &syllable)
	{
		bool valid = syllable == "<s>";
		valid = valid || syllable == "</s>";
		valid = valid || validPinyinCombines.find(syllable) != validPinyinCombines.end();
		valid = valid || coral::phonetics::syntax::kSoftInitials.find(syllable) != coral::phonetics::syntax::kSoftInitials.end();
		return valid;
	};
	const float lengthPenaltyFactor = 0.95f;
    const size_t maxBeamSize = (textLen > 20) ? 1000 : 5000;
    std::vector<std::pair<int, int>> operateIndexRange;
    if(correctionMethod.type == coral::phonetics::syntax::CorrectionType::EXTRA_RULE)
    {
        int offset = 0;
        operateIndexRange.reserve(correctionMethod.operateIndexRange.size());
        for(size_t i = 0; i < correctionMethod.operateIndexRange.size(); ++i)
        {
            const auto& [start, end] = correctionMethod.operateIndexRange[i];
            operateIndexRange.emplace_back(static_cast<int>(start) + offset, static_cast<int>(start + correctionMethod.operateAlphabetString[i].length()) + offset- 1);
            offset += static_cast<int>(correctionMethod.operateAlphabetString[i].length()) - (static_cast<int>(end) - static_cast<int>(start) + 1);
        }
    }
    size_t operateIndex = 0;
    for (int i = 1; i <= static_cast<int>(textLen); ++i)
    {
        int startIndex = 0, endIndex = i;
        if(correctionMethod.type == coral::phonetics::syntax::CorrectionType::EXTRA_RULE)
        {
            if(operateIndex < operateIndexRange.size())
            {
                const auto& [start, end] = operateIndexRange[operateIndex]; 
                if(i <= start)
                {
                    if(operateIndex > 0)
                    {
                        const auto& [preStart, preEnd] = operateIndexRange[operateIndex - 1];
                        startIndex = preEnd + 1;
                    }
                }
                else if(i <= end)
                {
                    continue;
                }
                else if(i == end + 1)
                {
                    startIndex = start, endIndex = start + 1;
                    operateIndex++;
                }
            }
            else if(operateIndex == operateIndexRange.size())
            {
                if(operateIndex > 0)
                {
                    const auto& [preStart, preEnd] = operateIndexRange[operateIndex - 1];
                    startIndex = preEnd + 1;
                }
            }
        }
        for (int j = startIndex; j < endIndex; ++j)
        {
			if (!isValidPinyin(std::string_view(text.data() + j, static_cast<size_t>(i) - j)))
				continue;
            if (dp[j].empty())
                continue;
			marisa::Agent agent;
            agent.set_query(text.data() + j, static_cast<size_t>(i) - j);
            if (trie_.lookup(agent)) {
				const uint16_t currentId = agent.key().id();
				for (const auto &[pairKey, prevLogProb] : dp[j])
				{
					uint16_t grandPrevId = (pairKey >> 16) & 0xFFFF;
					const uint16_t prevId = pairKey & 0xFFFF;
					const float logP = get_trigram_log_prob(grandPrevId, prevId, currentId);
					const float candidateProb = prevLogProb + logP;
					uint32_t newPairKey = makeSyllablePairKey(prevId, currentId);
					if (dp[i].find(newPairKey) == dp[i].end() || candidateProb > dp[i][newPairKey])
					{
						dp[i][newPairKey] = candidateProb;
                        backPointer[i][newPairKey] = {j, grandPrevId};
					}
				}
			}
		}
        if (dp[i].size() > maxBeamSize) {
            std::vector<std::pair<uint32_t, float>> candidates;
            candidates.reserve(dp[i].size());
            for (const auto& [key, prob] : dp[i])
            {
                candidates.emplace_back(key, prob);
            }
            
            std::partial_sort(candidates.begin(), candidates.begin() + maxBeamSize, candidates.end(),
                            [](const auto& a, const auto& b) { return a.second > b.second; });
        
            dp[i].clear();
            for (size_t k = 0; k < maxBeamSize; ++k)
            {
                dp[i][candidates[k].first] = candidates[k].second;
            }
        }
	}

	if (dp[textLen].empty())
	{
		return {{}, -std::numeric_limits<float>::infinity()};
	}

	float bestFinalProb = -std::numeric_limits<float>::infinity();
	uint32_t bestLastPairKey = 0;

	for (const auto& [pairKey, logProb] : dp[textLen])
    {
        const uint16_t prevId = (pairKey >> 16) & 0xFFFF;
        const uint16_t lastId = pairKey & 0xFFFF;
        const float finalLogP = get_trigram_log_prob(prevId, lastId, end_id_);
        const float normalizedFinalLogP = finalLogP * std::powf(lengthPenaltyFactor, static_cast<float>(textLen));
        const float finalProb = logProb + normalizedFinalLogP;
        
        if (finalProb > bestFinalProb) {
            bestFinalProb = finalProb;
            bestLastPairKey = pairKey;
        }
    }

    if (bestLastPairKey == 0) {
         return {{}, -std::numeric_limits<float>::infinity()};
    }

    std::vector<uint16_t> pathIds;
    size_t currentPos = textLen;
    uint32_t currentPairKey = bestLastPairKey;

    while (currentPos > 0) {
        const auto& [prevPos, grandPrevId] = backPointer[currentPos].at(currentPairKey);
        pathIds.push_back(currentPairKey & 0xFFFF);
        currentPairKey = makeSyllablePairKey(grandPrevId, (currentPairKey >> 16) & 0xFFFF);
        currentPos = prevPos;
    }
    std::reverse(pathIds.begin(), pathIds.end());
    std::vector<std::string> finalPath;
    finalPath.reserve(pathIds.size());

    std::transform(pathIds.begin(), pathIds.end(), std::back_inserter(finalPath),
        [this](uint16_t id) { return get_syllable_from_id(id); });
    const float lengthNormalizedProb = bestFinalProb / textLen;
    return {finalPath, lengthNormalizedProb};
}

std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> coral::phonetics::SpellingRepair::generate_all_corrections_with_details(const std::string& pinyin, const std::vector<std::string>& originalSequence, bool useSmartCorrection)
{
    std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>> corrections;
    if (!loaded_)
    {
        return corrections;
    }
	// 根据4种纠错规则精确计算预分配大小
	size_t estimatedSize = 5; // 基础大小 + MustCorrections
	
	if (useSmartCorrection && pinyin.length() <= CorrectionMaxLimit)
	{
		const size_t len = pinyin.length();
		// 插入：每个位置最多24个字符（排除v,w）
		estimatedSize += (len + 1) * 24;
		// 删除：每个字符位置1个候选
		estimatedSize += len;
		// 替换：每个字符平均5个相邻字符
		estimatedSize += len * 5;
		// 转置：相邻字符交换
		estimatedSize += (len > 1) ? (len - 1) : 0;
	}
	
    // 预估容量，减少vector重新分配
	corrections.reserve(estimatedSize);
	float originalPinyinCharLMScore = char_lm_.calculate_log_prob(pinyin);
	if (useSmartCorrection && pinyin.length() <= CorrectionMaxLimit)
	{
        generate_swaps(pinyin, corrections, originalPinyinCharLMScore);
        if(pinyin.length() <= kPinyinMaxLen)
        {
            for (const auto& [swapPinyin, swapMethod] : corrections)
            {
                const auto& [swapSequence, score] = predict(swapPinyin, swapMethod);
                if (swapSequence.size() == 1 && coral::phonetics::syntax::is_valid_full_pinyin(swapSequence[0]))
                {
                    return {{swapPinyin, swapMethod}};
                }
            }
        }
		generate_insertions(pinyin, corrections, originalPinyinCharLMScore);
		generate_deletions(pinyin, corrections, originalPinyinCharLMScore);
		generate_replacements(pinyin, corrections, originalPinyinCharLMScore, originalSequence);
	}
	generate_must_corrections(pinyin, corrections, originalPinyinCharLMScore);
	std::sort(corrections.begin(), corrections.end(),
			  [](const auto &a, const auto &b)
			  {
				  if (a.first == b.first)
				  {
					  return a.second.charTrigramScore > b.second.charTrigramScore;	 
				  }
				  return a.first < b.first;	 
			  });

	auto last = std::unique(corrections.begin(), corrections.end(),
							[](const auto &a, const auto &b)
							{
								return a.first == b.first;
							});
	corrections.erase(last, corrections.end());
	std::sort(corrections.begin(), corrections.end(),
			  [](const auto &a, const auto &b)
			  {
				  return a.second.charTrigramScore > b.second.charTrigramScore;
			  });
	corrections.resize(std::min(corrections.size(), static_cast<size_t>(40)));
	return corrections;
}

inline uint64_t coral::phonetics::SpellingRepair::get_bigram_count(const uint16_t id1, const uint16_t id2) const
{
    const uint32_t key = (static_cast<uint32_t>(id1) << 16) | id2;
    const coral::storage::SpellingPairRecord* end = bigram_data_ + bigram_count_;
    const coral::storage::SpellingPairRecord* it = std::lower_bound(bigram_data_, end, key, compareBigram);
    return (it != end && it->key == key) ? it->count : 0;
}

inline uint64_t coral::phonetics::SpellingRepair::get_trigram_count(const uint16_t id1, const uint16_t id2, const uint16_t id3) const
{
    const uint64_t key = (static_cast<uint64_t>(id1) << 32) | (static_cast<uint64_t>(id2) << 16) | id3;
    const coral::storage::SpellingTripleRecord* end = trigram_data_ + trigram_count_;
    const coral::storage::SpellingTripleRecord* it = std::lower_bound(trigram_data_, end, key, compareTrigram);
    return (it != end && it->key == key) ? it->count : 0;
}

float coral::phonetics::SpellingRepair::get_trigram_log_prob(const uint16_t id1, const uint16_t id2, const uint16_t id3) const
{
    const uint64_t cacheKey = (static_cast<uint64_t>(id1) << 32) | (static_cast<uint64_t>(id2) << 16) | id3;
    if (trigram_prob_cache_.empty())
        return get_kneser_ney_trigram_log_prob(id1, id2, id3);

    uint64_t hash = cacheKey;
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    TrigramProbSlot& slot = trigram_prob_cache_[static_cast<size_t>(hash) & (TRIGRAM_CACHE_SLOTS - 1)];
    if (slot.key == cacheKey)
        return slot.value;

    const float result = get_kneser_ney_trigram_log_prob(id1, id2, id3);
    slot.key = cacheKey;
    slot.value = result;
    return result;
}

uint16_t coral::phonetics::SpellingRepair::get_syllable_id(const char* s) const
{
    marisa::Agent agent;
    agent.set_query(s);
    if (trie_.lookup(agent))
        return agent.key().id();
    return INVALID_ID;
}

std::string coral::phonetics::SpellingRepair::get_syllable_from_id(const uint16_t id) const
{
    marisa::Agent agent;
    agent.set_query(id);
    trie_.reverse_lookup(agent);
    return {agent.key().ptr(), agent.key().length()};
}

//延续概率
uint64_t coral::phonetics::SpellingRepair::get_continuation_count1(const uint16_t w) const
{
    auto it = continuation_vocab_cache_.find(w);
    return (it != continuation_vocab_cache_.end()) ? it->second : 0;
}

uint64_t coral::phonetics::SpellingRepair::get_follow_count1(const uint16_t w) const
{
    auto it = follow_count1_cache_.find(w);
    return (it != follow_count1_cache_.end()) ? it->second : 0;
}

uint64_t coral::phonetics::SpellingRepair::get_follow_count2(const uint16_t w1, const uint16_t w2) const
{
    const uint32_t key = (static_cast<uint32_t>(w1) << 16) | w2;
    auto it = follow_count2_cache_.find(key);
    return (it != follow_count2_cache_.end()) ? it->second : 0;
}

//低、中、高频率的折扣参数
float coral::phonetics::SpellingRepair::get_kneser_ney_discount(const uint64_t count) const
{
    switch (count) {
        case 0: return 0.0f;
        case 1: return discountlow_;
        case 2: return discountmid_;
        case 3: return discounthigh_;
        case 4:
        case 5: return discounthigh_ * 0.95f;
        default:
            if (count <= 10) {
                return discounthigh_ * 0.85f;
            } else if (count <= 20) {
                return discounthigh_ * 0.70f;
            }
            return std::max(0.2f, discounthigh_ * 0.6f * LN20 / std::logf(static_cast<float>(count)));
    }
}

//低中高三种级别的折扣参数预计算（还未找到最佳方案，暂定这些参数）
void coral::phonetics::SpellingRepair::calculate_discount_parameters()
{
    discountlow_ = 0.75f;
    discountmid_ = 0.70f;
    discounthigh_ = 0.65f;
}

//延续技术的预计算和缓存
void coral::phonetics::SpellingRepair::precompute_kneser_ney_counts()
{
    trigram_prob_cache_.assign(TRIGRAM_CACHE_SLOTS, TrigramProbSlot{ TRIGRAM_CACHE_EMPTY, 0.0f });
    continuation_vocab_cache_.clear();
    continuation_bigram_cache_.clear();
    follow_count1_cache_.clear();
    follow_count2_cache_.clear();
    continuation_vocab_cache_.reserve(vocab_size_);
    continuation_bigram_cache_.reserve(bigram_count_);
    follow_count1_cache_.reserve(vocab_size_);
    follow_count2_cache_.reserve(bigram_count_);
    
    for (uint32_t i = 0; i < bigram_count_; ++i)
    {
        const uint32_t key = bigram_data_[i].key;
        const uint16_t w1 = (key >> 16) & 0xFFFF;
        const uint16_t w2 = key & 0xFFFF;
        continuation_vocab_cache_[w2]++;
        follow_count1_cache_[w1]++;
    }
    
    for (uint32_t i = 0; i < trigram_count_; ++i)
    {
        const uint64_t key = trigram_data_[i].key;
        const uint16_t w1 = (key >> 32) & 0xFFFF;
        const uint16_t w2 = (key >> 16) & 0xFFFF;
        const uint16_t w3 = key & 0xFFFF;
        const uint32_t bigram23Key = (static_cast<uint32_t>(w2) << 16) | w3;
        continuation_bigram_cache_[bigram23Key]++;
        const uint32_t bigram12Key = (static_cast<uint32_t>(w1) << 16) | w2;
        follow_count2_cache_[bigram12Key]++;
    }
}

float coral::phonetics::SpellingRepair::get_kneser_ney_trigram_log_prob(const uint16_t id1, const uint16_t id2, const uint16_t id3) const
{
    // 一次性获取所有计数，避免重复查找
    const uint64_t bigramCount = get_bigram_count(id1, id2);

    if (bigramCount == 0)
    {
        return get_bigram_backoff(id2, id3);
    }
    const uint64_t trigramCount = get_trigram_count(id1, id2, id3);
    // 预计算常用值
    const float discount = get_kneser_ney_discount(trigramCount);
    const float bigramCountDouble = static_cast<float>(bigramCount);
    const float mainTerm = std::max(0.0f, static_cast<float>(trigramCount) - discount) / bigramCountDouble;
    const uint64_t typeCount = get_follow_count2(id1, id2);
    float lambda = 0.0f;
    
    if (typeCount > 0)
    {
        lambda = discount * static_cast<float>(typeCount) / bigramCountDouble;
        // 简化条件判断
        lambda *= (bigramCount >= 50) ? 0.85f : (bigramCount <= 3) ? 1.3f : 1.0f;
        const float maxLambda = (bigramCount >=15000) ? 0.7f : 0.9f;
        lambda = std::clamp(lambda, 0.01f, maxLambda);  // 用clamp替代min/max组合
    }
    
    const float bigramProb = get_kneser_ney_bigram_prob(id2, id3);
    float finalProb = mainTerm + lambda * bigramProb;
    
    if (trigramCount == 0)
    {
        finalProb = 0.05f * mainTerm + 1.1f * lambda * bigramProb;
    }
    
    const float minProb = (bigramCount >= 10) ? 1e-12f : 1e-10f;
    return std::logf(std::max(finalProb, minProb));
}

//KN二元模型概率计算
float coral::phonetics::SpellingRepair::get_kneser_ney_bigram_prob(const uint16_t id2, const uint16_t id3) const
{
	// id2越界检查
    if(id2 >= total_unigram_count_)
    {
        return 0;
    }
	const uint32_t cacheKey = (static_cast<uint32_t>(id2) << 16) | id3;
	auto cacheIt = bigram_prob_cache_.find(cacheKey);

	if (cacheIt != bigram_prob_cache_.end())
	{
		return cacheIt->second;
	}

    const uint64_t unigram2Count = unigram_counts_[id2];
    
    if (unigram2Count == 0)
    {
		const float result = get_kneser_ney_unigram_prob(id3);
		// 缓存结果
		if (bigram_prob_cache_.size() < MAX_BIGRAM_CACHE_SIZE)
		{
			bigram_prob_cache_[cacheKey] = result;
		}
		return result;
	}

	const uint64_t bigram23Count = get_bigram_count(id2, id3);
	const float bigramDiscount = get_kneser_ney_discount(bigram23Count);
    const float bigramMainTerm = std::max(0.0f, static_cast<float>(bigram23Count) - bigramDiscount) / static_cast<float>(unigram2Count);
    const uint64_t bigramTypeCount = get_follow_count1(id2);
    float bigramLambda = 0.0f;
    
    if (bigramTypeCount > 0)
    {
        bigramLambda = bigramDiscount * static_cast<float>(bigramTypeCount) / static_cast<float>(unigram2Count);

		// 优化条件判断顺序，最常见情况优先
		if (unigram2Count >= 100) {
            bigramLambda *= 0.9f;
        } else if (unigram2Count <= 5) {
            bigramLambda *= 1.2f;
        }

		bigramLambda = std::clamp(bigramLambda, 0.05f, 0.8f);
	}

    const float continuationProb = get_kneser_ney_unigram_prob(id3);
    const float baseProb = bigramMainTerm + bigramLambda * continuationProb;
	const float result = std::max(baseProb, 1e-10f);

	// 缓存结果
	if (bigram_prob_cache_.size() < MAX_BIGRAM_CACHE_SIZE)
	{
		bigram_prob_cache_[cacheKey] = result;
	}

	return result;
}

//单元模型概率计算,考虑一元词作为接续词的能力
float coral::phonetics::SpellingRepair::get_kneser_ney_unigram_prob(const uint16_t id3) const
{
    //id3越界检查a
    if(id3 >= total_unigram_count_)
    {
        return 0.0f;
    }

    const uint64_t totalBigramTypes = bigram_count_;

	if (totalBigramTypes == 0)
    {
		return 1.0f / static_cast<float>(vocab_size_);
    }

    const uint64_t continuationCount = get_continuation_count1(id3);
	const uint64_t unigramCount = unigram_counts_[id3];
	const float continuationProb = static_cast<float>(continuationCount) / static_cast<float>(totalBigramTypes);
	const float unigramProb = (total_unigram_count_ > 0) ? static_cast<float>(unigramCount) / static_cast<float>(total_unigram_count_)
														 : 1.0f / static_cast<float>(vocab_size_);
	const float alpha = std::min(0.95f, static_cast<float>(continuationCount) / (continuationCount + 5.0f));
    const float interpolatedProb = alpha * continuationProb + (1.0f - alpha) * unigramProb;
    const float smoothedNumerator = interpolatedProb * totalBigramTypes + 0.1f;
    const float smoothedDenominator = static_cast<float>(totalBigramTypes) + 0.1f * vocab_size_;
    return smoothedNumerator / smoothedDenominator;
}

//二元模型回退计算
float coral::phonetics::SpellingRepair::get_bigram_backoff(const uint16_t id2, const uint16_t id3) const
{
    //id2越界检查
    if(id2 >= total_unigram_count_)
    {
        return 0.0f;
    }
	const uint64_t unigram2Count = unigram_counts_[id2];
    
    if (unigram2Count == 0)
    {
        const float unigramProb = get_kneser_ney_unigram_prob(id3);
        return std::logf(unigramProb);
    }
    
    const float bigramProb = get_kneser_ney_bigram_prob(id2, id3);
    return std::logf(std::max(bigramProb, 1e-15f));
}

void coral::phonetics::SpellingRepair::build_must_correction_rules()
{
    if (is_enable_correction_)
        return;
    max_correct_key_length_ = 0;
    for (const auto& rule : coral::phonetics::syntax::get_correct_rules())
    {
        if (rule.prefix.empty()) {
            correct_map_[rule.pattern].insert(rule.replacement);
            if (rule.pattern.length() > max_correct_key_length_)
                max_correct_key_length_ = rule.pattern.length();
        } else {
            for (const auto& pre : rule.prefix) {
                auto key = pre + rule.pattern;
                correct_map_[key].insert(pre + rule.replacement);
                if (key.length() > max_correct_key_length_)
                    max_correct_key_length_ = key.length();
            }
        }
    }
    is_enable_correction_ = true;
}

void coral::phonetics::SpellingRepair::generate_insertions(const std::string& pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>>& corrections,
    float originalPinyinCharLMScore)
{
    if (pinyin.length() > CorrectionMaxLimit - 1)
    {
		return;
	}
    //按拼音重要性排序，高频字符优先
    static const char priorityChars[] = "aeiouhnglrmstzcbdpkjfqyx";
    // 插入串只比原串多一个字符，复用同一块缓冲，避免内层循环里两次 substr 加一次拼接
    std::string temp;
    temp.reserve(pinyin.length() + 1);
    // 如果首字母是合法声母，跳过声母长度的插入操作
    for (size_t i = static_cast<size_t>(get_valid_initial_length(pinyin)); i < pinyin.length(); ++i)
    {
        for (char c : priorityChars) 
        {
            const char &prevChar = (i == 0) ? '<' : pinyin[i - 1];
			const char &nextChar = pinyin[i];

			bool shouldSkip = false;
			if ((prevChar == 'z' || prevChar == 'c' || prevChar == 's') && nextChar == 'h')
			{
				shouldSkip = true;
			}

			if (prevChar == c && c != 'n' && c != 'g')
			{
				shouldSkip = true;
			}

			// 新增：如果插入的字符是声母，检查额外的条件
			if (isInitial(c))
			{
				// 如果后一个不是韵母，跳过插入
				if (!isFinal(nextChar))
				{
					shouldSkip = true;
				}
			}
			if (prevChar == 'n' && c == 'g' && nextChar != 'h')
			{
				shouldSkip = false;
			}
			if (shouldSkip)
			{
				continue;
			}

			temp.assign(pinyin, 0, i);
			temp += c;
			temp.append(pinyin, i, std::string::npos);

			float candidateScore = 0.0f;
			if (check_correct_pinyin_valid(temp, originalPinyinCharLMScore, candidateScore))
			{
				coral::phonetics::syntax::CorrectionMethod method = {coral::phonetics::syntax::CorrectionType::INSERTION, static_cast<int>(i), c, {}, {}, candidateScore};
				corrections.emplace_back(temp, method);
			}
		}
	}
}

void coral::phonetics::SpellingRepair::generate_deletions(const std::string& pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>>& corrections,
    float originalPinyinCharLMScore)
{
    if (pinyin.length() <= CorrectionMinLimit)
	{
		return;
	}

	for (size_t i = static_cast<size_t>(get_valid_initial_length(pinyin)); i < pinyin.length(); ++i)
	{
        bool shouldSkip = false;
        if (i > 0 && pinyin[i] == 'h' &&
            (pinyin[i - 1] == 'z' || pinyin[i - 1] == 'c' || pinyin[i - 1] == 's')) {
            shouldSkip = true;
        }
        if (i < pinyin.length() - 1 && pinyin[i + 1] == 'h' &&
            (pinyin[i] == 'z' || pinyin[i] == 'c' || pinyin[i] == 's')) {
            shouldSkip = true; 
        }
        if(i == pinyin.length() - 1)
        {
            shouldSkip = true;
        }
		
		if (!shouldSkip)
		{
			const char deletedChar = pinyin[i];
			std::string temp = pinyin;
			temp.erase(i, 1);

			float candidateScore = 0.0f;
			if (check_correct_pinyin_valid(temp, originalPinyinCharLMScore, candidateScore))
			{
				coral::phonetics::syntax::CorrectionMethod method = {coral::phonetics::syntax::CorrectionType::DELETION, static_cast<int>(i), deletedChar, {}, {}, candidateScore};
				corrections.emplace_back(temp, method);
			}
		}
	}
}

void coral::phonetics::SpellingRepair::generate_replacements(const std::string& pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>>& corrections,
    float originalPinyinCharLMScore, const std::vector<std::string>& originalSequence)
{
    // 使用 string_view 数组替代C风格字符数组，按替换概率排序
    static const std::string_view adjacentMapping[26] = 
    {
        "qwsz",      // a
        "vghn",      // b  
        "xdfv",      // c
        "serfcx",    // d
        "wsdr",      // e
        "drtgvc",    // f
        "ftyhbv",    // g
        "gyujnb",    // h
        "uojk",      // i
        "huikmn",    // j
        "jiolm",     // k
        "kop",       // l
        "njk",       // m
        "bhjm",      // n
        "iklp",      // o
        "ol",        // p
        "wa",        // q
        "edft",      // r
        "awedxz",    // s
        "rfgy",      // t
        "yihj",      // u
        "cfgb",      // v
        "qase",      // w
        "zsdc",      // x
        "tghu",      // y
        "asx"        // z
    };

	for (size_t i = static_cast<size_t>(get_valid_initial_length(pinyin)), index = 0, startPos = 0; i <pinyin.length(); ++i)
	{
        const char originalChar = pinyin[i];

        if(index < originalSequence.size() && startPos + originalSequence[index].size() == i)
            startPos += originalSequence[index++].size();

		if (originalChar < 'a' || originalChar > 'z')
		{
			continue;
		}

		const std::string_view& neighbors = adjacentMapping[originalChar - 'a'];
        
        for (char replacementChar : neighbors) 
        {
            bool shouldSkip = false;

			if (i < pinyin.length() - 1 && pinyin[i + 1] == 'h')
			{
				if ((originalChar == 'z' || originalChar == 'c' || originalChar == 's') && replacementChar != originalChar)
				{
					shouldSkip = true;
				}
			}

			if (i > 0 && pinyin[i - 1] == replacementChar && replacementChar != 'n' && replacementChar != 'g')
			{
				shouldSkip = true;
			}

			if (!shouldSkip)
			{
				std::string temp = pinyin;
				temp[i] = replacementChar;

				float candidateScore = 0.0f;
				if (check_correct_pinyin_valid(temp, originalPinyinCharLMScore, candidateScore)
                    || (index < static_cast<int>(originalSequence.size()) && validPinyinCombines.count(originalSequence[index]) && validPinyinCombines.count(temp.substr(startPos, originalSequence[index].size()))))
				{
					coral::phonetics::syntax::CorrectionMethod method = {coral::phonetics::syntax::CorrectionType::REPLACEMENT, static_cast<int>(i), replacementChar, {}, {}, candidateScore};
					corrections.emplace_back(temp, method);
				}
			}
		}
	}
}

void coral::phonetics::SpellingRepair::generate_swaps(const std::string& pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>>& corrections,
    float originalPinyinCharLMScore)
{
    if (pinyin.length() < 2) 
    {
        return;
    }
	for (size_t i = get_valid_initial_length(pinyin); i < pinyin.length() - 1; ++i)
	{
        const char c1 = pinyin[i];
        const char c2 = pinyin[i + 1];
        
        if (c2 == 'h' && (c1 == 'z' || c1 == 'c' || c1 == 's')) {
            continue;
        }

        if (c1 == c2) {
            continue;
        }

        if (c1 == 'h' && (c2 == 'z' || c2 == 'c' || c2 == 's')) {
            continue; 
        }
        
        std::string temp = pinyin;
        std::swap(temp[i], temp[i + 1]);

		float candidateScore = 0.0f;
		if (check_correct_pinyin_valid(temp, originalPinyinCharLMScore, candidateScore))
		{
			coral::phonetics::syntax::CorrectionMethod method = {coral::phonetics::syntax::CorrectionType::SWAP, static_cast<int>(i), '\0', {}, {}, candidateScore};
			corrections.emplace_back(temp, method);
		}
	}
}

void coral::phonetics::SpellingRepair::generate_must_corrections(const std::string& pinyin, std::vector<std::pair<std::string, coral::phonetics::syntax::CorrectionMethod>>& corrections,
    float originalPinyinCharLMScore)
{
    std::string correctedPinyin;
    coral::phonetics::syntax::CorrectionMethod method;
    method.type = coral::phonetics::syntax::CorrectionType::EXTRA_RULE;
    method.operateIndex = 0;
    method.operateAlphabet = '\0';
    size_t index = 0;
    const size_t pinyinLength = pinyin.length();

    while (index < pinyinLength) {
        bool ruleApplied = false;
        const size_t lenToCheck = std::min(pinyinLength - index, max_correct_key_length_);
        
        for (size_t len = lenToCheck; len >= 1; --len)
        {
            std::string sub = pinyin.substr(index, len);
            auto it = correct_map_.find(sub);

            if (it != correct_map_.end() && !it->second.empty()) {
                const std::string& replacement = *it->second.begin();
                correctedPinyin += replacement;

                method.operateIndexRange.emplace_back(index, index + len - 1);
                method.operateAlphabetString.push_back(replacement);

                index += len;
                ruleApplied = true;
                break;
            }
        }

        if (!ruleApplied) {
            correctedPinyin += pinyin[index];
            index++;
        }
    }

    if (correctedPinyin != pinyin) {
		method.charTrigramScore = char_lm_.calculate_log_prob(correctedPinyin);
		corrections.emplace_back(correctedPinyin, method);
#ifdef _DEBUG
        std::cout << "[Correction] Must applied: " << pinyin << " -> " << correctedPinyin << " with score: " << method.charTrigramScore << std::endl;
#endif
	}
}

bool coral::phonetics::SpellingRepair::check_correct_pinyin_valid(const std::string& pinyin, float originalScore, float& candidateScore) const
{
	// 首先检查是否超过拼音长度阈值
	candidateScore = char_lm_.calculate_log_prob(pinyin);
	if (!char_lm_.is_pinyin_above_threshold(pinyin, candidateScore))
		return false;  // 不满足阈值要求，跳过
	if (candidateScore < originalScore) {
		return false;
	}
	return true;
}
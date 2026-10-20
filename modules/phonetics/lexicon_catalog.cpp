/*
* @author: huangzhishou
* @date 2021-5-28 18:02:33
*/
#include "pch.h"
#include <cstring>
#include "runtime_context.h"
#include <vector>
#include "lexicon_catalog.h"
#include "phonetic_sequence.hpp"
#include "latin_lexicon.h"
#include "support/parse_arena.hpp"
#include "support/parse_metrics.hpp"

namespace
{
	// PhraseInfo::operator< 的等价排序键。
	//
	// 直接对 phraseInfoVec 排序有两个代价：PhraseInfo 近百字节且含 std::string，
	// O(n log n) 次元素搬移都要动字符串；比较器的次级键是 phrase 的字符串比较，而词库
	// 长尾词大量共享同一权重，权重相等的分支命中率很高，字符串比较因此成为取词阶段
	// 最大的单点开销。
	//
	// 这里把两级键压进定长整数：权重按 IEEE754 单调映射成 uint32（升序即原权重降序），
	// phrase 取前 16 字节按大端装进两个 uint64（不足补 0）。词条来自以 '\0' 结尾的词库
	// 字符串池，正文不含 0 字节，因此长度 ≤16 的词条由该前缀唯一决定；只有前缀相同且
	// 确实存在更长词条时才回落到真正的字符串比较。
	struct PhraseSortKey
	{
		uint32_t weightDesc;
		uint32_t index;
		uint64_t prefixHi;
		uint64_t prefixLo;
	};

	using phraseSortKeyVec = std::vector<PhraseSortKey, coral::phonetics::support::PoolAllocator<PhraseSortKey>>;

	constexpr size_t PHRASE_KEY_PREFIX_BYTES = 16;

	[[nodiscard]] uint32_t weightDescKey(coral::phonetics::Score weight) noexcept
	{
		if (weight == 0)	// 归一 -0.0：原比较器认为它与 +0.0 相等
			weight = 0;
		uint32_t bits = 0;
		std::memcpy(&bits, &weight, sizeof(bits));
		// 负数按位取反、正数翻转符号位，得到与浮点升序一致的整数键；再取反转为降序键
		bits = (bits & 0x80000000u) ? ~bits : (bits | 0x80000000u);
		return ~bits;
	}

	[[nodiscard]] uint64_t bigEndianChunk(const std::string_view phrase, const size_t offset) noexcept
	{
		uint64_t chunk = 0;
		const size_t available = phrase.size() > offset ? phrase.size() - offset : 0;
		const size_t count = available < sizeof(chunk) ? available : sizeof(chunk);
		for (size_t i = 0; i < count; ++i)
			chunk |= static_cast<uint64_t>(static_cast<unsigned char>(phrase[offset + i])) << (56 - 8 * i);
		return chunk;
	}

	// 权重降序、同权重按 phrase 降序，与 PhraseInfo::operator< 逐项等价
	void sortAndTruncatePhrases(coral::phonetics::ParsePhrases& phraseVec, const size_t keep)
	{
		const size_t count = phraseVec.size();
		if (count <= 1 || keep == 0)
		{
			if (keep < count)
				phraseVec.resize(keep);
			return;
		}

		phraseSortKeyVec keys;
		keys.reserve(count);
		for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i)
		{
			const std::string_view phrase = phraseVec[i].phrase;
			keys.push_back(PhraseSortKey{ weightDescKey(phraseVec[i].phrase_weight), i,
				bigEndianChunk(phrase, 0), bigEndianChunk(phrase, sizeof(uint64_t)) });
		}

		const auto keyLess = [&phraseVec](const PhraseSortKey& lhs, const PhraseSortKey& rhs) {
			if (lhs.weightDesc != rhs.weightDesc)
				return lhs.weightDesc < rhs.weightDesc;
			if (lhs.prefixHi != rhs.prefixHi)
				return lhs.prefixHi > rhs.prefixHi;
			if (lhs.prefixLo != rhs.prefixLo)
				return lhs.prefixLo > rhs.prefixLo;
			const std::string_view lhsPhrase = phraseVec[lhs.index].phrase;
			const std::string_view rhsPhrase = phraseVec[rhs.index].phrase;
			if (lhsPhrase.size() <= PHRASE_KEY_PREFIX_BYTES && rhsPhrase.size() <= PHRASE_KEY_PREFIX_BYTES)
				return false;	// 前缀已覆盖全部字节，两者相等
			return lhsPhrase > rhsPhrase;
		};

		if (keep < count)
			std::partial_sort(keys.begin(), keys.begin() + keep, keys.end(), keyLess);
		else
			std::sort(keys.begin(), keys.end(), keyLess);

		// 按 keys 给出的次序原地重排（keys 整体仍是 0..count-1 的置换）：跟随置换环，
		// 每个元素最多搬一次，取代排序内部的 O(n log n) 次搬移
		for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i)
		{
			if (keys[i].index == i)
				continue;
			coral::phonetics::LexiconPhrase pending = std::move(phraseVec[i]);
			uint32_t slot = i;
			for (;;)
			{
				const uint32_t source = keys[slot].index;
				keys[slot].index = slot;
				if (source == i)
					break;
				phraseVec[slot] = std::move(phraseVec[source]);
				slot = source;
			}
			phraseVec[slot] = std::move(pending);
		}

		if (keep < count)
			phraseVec.resize(keep);
	}
}

coral::phonetics::LexiconCatalog::~LexiconCatalog()
{
}

bool coral::phonetics::LexiconCatalog::load_primary_lexicon(const coral::phonetics::LexiconLocations& path)
{
	return primary_lexicon_.load(path.primaryMarisa, path.primaryData, path.versionId);
}

bool coral::phonetics::LexiconCatalog::load_pinyin_model(const coral::phonetics::LexiconLocations& path, const syllable_strview_map& syllables)
{
	if (!english_lexicon_.load(path.englishMarisa, path.englishData, path.versionId))
		return false;
	user_lexicon_.set_english_lexicon(&english_lexicon_);
	if (!user_lexicon_.load(path.userLexiconFile, syllables))
		return false;

	return true;
}

void coral::phonetics::LexiconCatalog::collect_candidates(const coral::phonetics::PhoneticSequence& pinyin_sequence
	, coral::phonetics::ParsePhrases& phraseVec
	, const int limit
	, bool isShowInfos
	, bool is_corrected
	, bool isAbbreviatehasFull
	, bool isMaxPinyinSequence
)
{
	PERF_INC(spanQueries);
	if (isShowInfos)
		primary_lexicon_.set_is_corrected(is_corrected);

	// 各词典的查询 key 完全相同，拼一次共用；缓冲复用成员，堆分配随解析次数摊平
	const auto& queryPinyin = pinyin_sequence.sub_sequences_.empty() ? pinyin_sequence.sequences_ : pinyin_sequence.sub_sequences_;
	coral::phonetics::syntax::join_string_views_into('\'', queryPinyin, query_key_);
	// 布隆过滤器的哈希也只算一次，主词典、细胞词库和每本云词典共用
	const coral::phonetics::support::KeyProbe queryProbe{ query_key_ };
	// 用户词典优先查询：用户自造词先于主词典写入候选，
	// 再由下方统一去重逻辑（保留权重更高者）决定是否保留。
	if (isMaxPinyinSequence)
	{
		user_lexicon_.get_phrases(pinyin_sequence, phraseVec, get_language_variant());
	}
	primary_lexicon_.get_phrases(pinyin_sequence, queryProbe, phraseVec, limit, isMaxPinyinSequence);
	primary_lexicon_.get_combined_phrases(pinyin_sequence, queryProbe, phraseVec, limit);
	// if (isMaxPinyinSequence)
		// m_primaryLexicon.getConditionPhrases(m_conditionPy, pinyinSequence, phraseVec);
	// 去重 只要显示的文本相同就应该去掉 不论拼音是否相同 另外有文本相同时去掉权重小的那一个
	// 重复只可能跨来源出现（主词典/细胞词库/云词典收词重叠、多音字经简拼混拼扩展匹配重复命中），占比很低，
	// 因此不做全量排序去重：先哈希检测是否存在重复，无重复时仅 n 次哈希插入、零搬移零字符串比较；
	// 有重复时按插入顺序合并（主词典结果最先插入，优先保留），合并语义与原排序去重一致。
	// 注意：后续按权重排序的比较器在去重后的集合上是严格全序，去重阶段的内部顺序不影响最终输出。
	{
		flat_hash_map<std::string_view, size_t>& phraseIndex = dedup_index_;
		phraseIndex.clear();
		phraseIndex.reserve(phraseVec.size());
		bool hasDuplicate = false;
		for (size_t i = 0; i < phraseVec.size(); ++i)
		{
			if (!phraseIndex.emplace(phraseVec[i].phrase, i).second)
			{
				hasDuplicate = true;
				break;
			}
		}
		if (hasDuplicate)
		{
			phraseIndex.clear();
			coral::phonetics::ParsePhrases uniqueVec;
			uniqueVec.reserve(phraseVec.size());
			for (auto& item : phraseVec)
			{
				auto [it, inserted] = phraseIndex.try_emplace(item.phrase, uniqueVec.size());
				if (inserted)
				{
					uniqueVec.push_back(std::move(item));
					continue;
				}
				// 相同词，合并信息：保留权重更高的那个
				auto& current = uniqueVec[it->second];
				current.phrase_weight = std::max(current.phrase_weight, item.phrase_weight);
				current.phrase_backprob = std::max(current.phrase_backprob, item.phrase_backprob);
				current.b_fre_highest |= item.b_fre_highest;
				current.b_in_m2 |= item.b_in_m2;
				// 低id为主词典，中间id为云词典，INT_MAX为用户词典
				current.id = std::min(current.id, item.id);
				if (isShowInfos)
				{
				}
			}
			phraseVec = std::move(uniqueVec);
		}
	}
	// 排序与截断合并：只有前 keep 个结果会被下游使用，排序键只需保证前 keep 个有序
	// （partial_sort 复杂度 O(n log keep)），被丢弃的尾部也不必再搬移和加惩罚
	const size_t keep = limit < 0
		? phraseVec.size()
		: std::min(static_cast<size_t>(limit), phraseVec.size());
	PERF_ADD(phrasesEmitted, phraseVec.size());
	PERF_ADD(phrasesAfterLimit, keep);
#if LANGENGINE_PERF_COUNTERS
	{
		const size_t got = phraseVec.size();
		if (limit < 0)
		{
			PERF_INC(spanUnlimited);
			PERF_ADD(phrasesFromUnlimited, got);
		}
		if (got > 50)
			PERF_ADD(phrasesOver50, got - 50);
		size_t bucket = 0;
		if (got >= 1) { bucket = 1; size_t bound = 4; while (bucket < 7 && got > bound) { ++bucket; bound *= 4; } }
		PERF_ADD(spanBucket[bucket], 1);
	}
#endif
	sortAndTruncatePhrases(phraseVec, keep);
	if (isAbbreviatehasFull)
	{
		std::for_each(phraseVec.begin(), phraseVec.end(), [](auto& item)
		{
			item.phrase_weight += kAbbrPenaltyValue;
		});
	}
#ifdef _DEBUG
	std::cout << "[Lexicon] " << "Get " << phraseVec.size() << " phrases for pinyin: " << pinyin_sequence.to_debug_string() << std::endl; 
#endif
}

void coral::phonetics::LexiconCatalog::collect_repaired_candidates(const coral::phonetics::ParseTextViews& pinyin_sequence
	, coral::phonetics::ParsePhrases& phraseVec
	, coral::LanguageVariant mainType
	, int limit
	, bool isMaxPinyinSequence
)
{
	phraseVec.clear();
	if (coral::Engine::compatInstance().config().isShowInfos())
	{
		primary_lexicon_.set_is_corrected(true);
	}
	primary_lexicon_.collect_repaired_candidates(pinyin_sequence, phraseVec, limit, isMaxPinyinSequence);
}

void coral::phonetics::LexiconCatalog::reset_storage()
{
	primary_lexicon_.reset_storage();
	user_lexicon_.reset_storage();
}

int coral::phonetics::LexiconCatalog::max_length() const {
	return std::max({ primary_lexicon_.max_length(), user_lexicon_.max_length(), english_lexicon_.max_length() });
}

bool coral::phonetics::LexiconCatalog::update_user_lexicon(const string& py, const string& sent) 
{
	return user_lexicon_.update_user_lexicon(py, sent, get_language_variant());
}

bool coral::phonetics::LexiconCatalog::store_lexicon() const
{
	return user_lexicon_.store_lexicon();
}

bool coral::phonetics::LexiconCatalog::clear_lexicon()
{
	return user_lexicon_.clear_user_lexicon();
}

void coral::phonetics::LexiconCatalog::get_english_phrases(const std::string& originalInput, coral::phonetics::ParsePhrases& phrases, int limit) {
	// 在应用共享权重之后排序，而不是限制 trie 遍历。
	english_lexicon_.get_phrases(originalInput, phrases, std::numeric_limits<int>::max());
	for (auto& phrase : phrases) primary_lexicon_.find_english_word(phrase.phrase, phrase);
	std::stable_sort(phrases.begin(), phrases.end(), [&](const coral::phonetics::LexiconPhrase& a, const coral::phonetics::LexiconPhrase& b) {
		const bool aExact = hlp::toLower_Optimized(std::string(a.phrase)) == hlp::toLower_Optimized(originalInput);
		const bool bExact = hlp::toLower_Optimized(std::string(b.phrase)) == hlp::toLower_Optimized(originalInput);
		return aExact != bExact ? aExact : a.phrase_weight > b.phrase_weight;
	});
	if (limit >= 0 && phrases.size() > static_cast<size_t>(limit)) phrases.resize(limit);
}

bool coral::phonetics::LexiconCatalog::get_eng_word_info(const std::string_view word, coral::phonetics::LexiconPhrase& info) const
{
	if (!english_lexicon_.get_word_info(word, info)) return false;
	primary_lexicon_.find_english_word(info.phrase, info);
	return true;
}

string coral::phonetics::LexiconCatalog::get_user_lexicon_path() const
{
	auto [sPath, tPath] = user_lexicon_.get_user_lexicon_path();
	return coral::inputModeState().isTraditionalChinese() ? tPath : sPath;
}

bool coral::phonetics::LexiconCatalog::reload_user_lexicon(const syllable_strview_map& syllables)
{
	return user_lexicon_.re_connect_user_lexicon();
}

bool coral::phonetics::LexiconCatalog::maintain_lexicon(
	const syllable_strview_map& syllables)
{
	static_cast<void>(syllables);
	return user_lexicon_.maintain_user_data();
}

bool coral::phonetics::LexiconCatalog::export_user_lexicon(const filesystem::path& lexiconPath) const
{
	return user_lexicon_.export_unified_user_lexicon_as_csv(lexiconPath);
}

hlp::ImportResult coral::phonetics::LexiconCatalog::import_user_lexicon(const filesystem::path& lexiconPath, const syllable_strview_map& syllables)
{
	return user_lexicon_.import_unified_user_lexicon_from_csv(lexiconPath, syllables);
}

bool coral::phonetics::LexiconCatalog::revert_last_action_for_phrase(const std::string& delPhrase)
{
	return user_lexicon_.revert_last_action_for_phrase(delPhrase);
}

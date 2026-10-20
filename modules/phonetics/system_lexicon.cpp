#include "../runtime/resource_access.h"
#include "pch.h"
#include "../runtime/runtime_context.h"
#include "phonetic_types.h"
#include "system_lexicon.h"
#include "latin_identity.h"
#include "syllable_lattice.h"
#include "support/parse_metrics.hpp"
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>

namespace
{
	// 将拼音字符串转换为简拼形式
	std::string pinyinToSimple(const std::string& pinyin_str) {
		if (pinyin_str.empty()) {
			return "";
		}

		std::string result;
		for (size_t i = 0; i < pinyin_str.length(); ++i) {
			char current_char = pinyin_str[i];
			if (i == 0 || pinyin_str[i - 1] == '\'')
			{
				result += current_char;
			}
			else if (current_char == '\'')
			{
				result += current_char;
			}
		}
		return result;
	}

	/**
	 * 主词库同键排序：
	 * 词频最高为冠军
	 * 结果[冠军][m2链降序][非m2链降序]
	 */
	void sortPrimaryPhrasesByInM2AndWeight(std::vector<coral::storage::OwnedPhraseRecord>& phrases)
	{
		if (phrases.size() <= 1)
		{
			return;
		}

		coral::storage::OwnedPhraseRecord champion = std::move(phrases[0]);
		std::vector<coral::storage::OwnedPhraseRecord> m2Items;
		std::vector<coral::storage::OwnedPhraseRecord> normalItems;
		m2Items.reserve(phrases.size());
		normalItems.reserve(phrases.size());

		for (size_t i = 1; i < phrases.size(); ++i)
		{
			if (phrases[i].b_in_m2)
			{
				m2Items.push_back(std::move(phrases[i]));
			}
			else
			{
				normalItems.push_back(std::move(phrases[i]));
			}
		}

		phrases.clear();
		phrases.push_back(std::move(champion));
		phrases.insert(phrases.end(), std::make_move_iterator(m2Items.begin()),
					  std::make_move_iterator(m2Items.end()));
		phrases.insert(phrases.end(), std::make_move_iterator(normalItems.begin()),
					  std::make_move_iterator(normalItems.end()));
}
}

bool coral::phonetics::SystemLexicon::load(const coral::phonetics::ResourceLocation &trieFile, const coral::phonetics::ResourceLocation &infoFile, const string &versionId)
{
	// 主词典唯一查询路径：mainlexicon.coral 扁平二分（与 marisa+dat 逐位等价，
	// 键集合/条目顺序/字段全量对拍与 99668 样本准确率测试均验证一致）。
	// bin 缺失时从同目录 primary.marisa + primary.dat 现场生成一次（生成后全量校验）。
	const auto binPath = trieFile.path.parent_path() / "mainlexicon.coral";
	if (!std::filesystem::exists(binPath))
	{
		LOG_INFO("Langengine: mainlexicon.coral missing, generating from marisa+dat: " + binPath.string());
		if (!import_legacy_lexicon_index(trieFile.path, infoFile.path, binPath))
		{
			LOG_ERROR("Langengine: failed to generate mainlexicon.coral from "
				+ trieFile.path.string() + " + " + infoFile.path.string());
			return false;
		}
	}
	if (!bin_mapper_.load(hlp::buildSHNameW(versionId, name_ + "GBin"), binPath, "", coral::runtime::resource_access()))
	{
		LOG_ERROR("Langengine: primary lexicon flat bin mapper load failed: " + binPath.string());
		return false;
	}
	if (!primary_bin_.bind_data(bin_mapper_.data(), bin_mapper_.size()))
	{
		LOG_ERROR("Langengine: primary lexicon flat bin bindData failed: " + binPath.string());
		bin_mapper_.reset();
		return false;
	}
	auto englishWords = std::make_shared<EnglishWords>();
	const auto* entries = primary_bin_.entries();
	for (uint32_t k = 0; k < primary_bin_.key_count(); ++k) {
		uint32_t off = 0, count = 0;
		primary_bin_.key_entries(k, off, count);
		for (uint32_t i = 0; i < count; ++i) {
			const auto& entry = entries[off + i];
			const auto word = primary_bin_.entry_word(entry);
			const auto key = coral::english_identity_key(word);
			if (key.empty()) continue;
			auto [it, inserted] = englishWords->emplace(key, std::make_pair(entry.phrase_id, entry.weight));
			// 旧词包可能有多个 ID。按构建器的首词标识匹配。
			if (!inserted && entry.phrase_id < it->second.first)
				it->second = { entry.phrase_id, entry.weight };
		}
	}
	// 主加载可在后台进行，此时英文词库已可用。
	std::shared_ptr<const EnglishWords> englishSnapshot = std::move(englishWords);
	std::atomic_store_explicit(&english_words_, std::move(englishSnapshot), std::memory_order_release);
	// 索引只持有原条目下标，不复制词库文本，也不改变磁盘格式。
    text_index_.resize(primary_bin_.entry_count());
    std::iota(text_index_.begin(), text_index_.end(), 0u);
    const auto less = [&](uint32_t a, uint32_t b) {
        const auto wa = primary_bin_.entry_word(entries[a]), wb = primary_bin_.entry_word(entries[b]);
        if (wa != wb) return wa < wb;
        const auto pa = primary_bin_.entry_pinyin(entries[a]), pb = primary_bin_.entry_pinyin(entries[b]);
        if (pa != pb) return pa < pb;
        return entries[a].weight > entries[b].weight;
    };
    std::sort(text_index_.begin(), text_index_.end(), less);
    text_index_.erase(std::unique(text_index_.begin(), text_index_.end(), [&](uint32_t a, uint32_t b) {
        return primary_bin_.entry_word(entries[a]) == primary_bin_.entry_word(entries[b]) &&
            primary_bin_.entry_pinyin(entries[a]) == primary_bin_.entry_pinyin(entries[b]);
    }), text_index_.end());
    set_max_length(30);
	loaded_ = true;
	LOG_INFO("Langengine: primary lexicon flat bin enabled: " + binPath.string());
	return true;
}

void coral::phonetics::SystemLexicon::get_phrases(
	const coral::phonetics::PhoneticSequence& pinyin_sequence,
	const coral::phonetics::support::KeyProbe& queryKey,
	coral::phonetics::ParsePhrases& phrases,
	const int limit,
	bool isMaxPinyinSequence)
{
	if (!loaded_)
		return;
	// 查询拼音，对于含有简拼的情况，使用的是简拼
	const auto& queryPinyin = pinyin_sequence.sub_sequences_.empty() ? pinyin_sequence.sequences_ : pinyin_sequence.sub_sequences_;
	// 实际输入的拼音，需要确保查询得到的词的拼音是包含实际输入的拼音的
	const auto& actualInputPinyin = pinyin_sequence.sequences_;
	bool isShowInfos = coral::Engine::compatInstance().config().isShowInfos();
	PERF_INC(primaryLookups);
	uint32_t entry_offset = 0, entry_count = 0;
	if (primary_bin_.find_key(queryKey.key.data(), queryKey.key.size(), entry_offset, entry_count))
	{
		PERF_INC(primaryHits);
		const bool isMixed = queryPinyin != actualInputPinyin;
		bool currentFirstAdded = true;
		const coral::storage::LexiconEntryRecord* binEntries = primary_bin_.entries();
		for (uint32_t i = 0; i < entry_count; ++i)
		{
			if (limit > 0 && phrases.size() >= static_cast<size_t>(limit))
				break;
			const coral::storage::LexiconEntryRecord& e = binEntries[entry_offset + i];
			const auto word = primary_bin_.entry_word(e);
			const auto pinyin = primary_bin_.entry_pinyin(e);
			if (isMixed && !coral::phonetics::syntax::compare_phrase_pinyin(actualInputPinyin, pinyin, false))
				continue;
			if (isShowInfos)
			{
				int frequency = static_cast<int>(exp(e.weight) / kInvMaxcount);
				phrases.emplace_back(word, pinyin, e.weight, e.phrase_id, e.backprob,
					currentFirstAdded, e.b_in_m2 != 0, false,
					is_corrected_);
			}
			else
			{
				phrases.emplace_back(word, pinyin, e.weight, e.phrase_id, e.backprob,
					currentFirstAdded, e.b_in_m2 != 0, false,
					false);
			}
			currentFirstAdded = false;
		}
	}
	if (isMaxPinyinSequence && queryPinyin.size() >= kMinPinyinLengthForExtendedMatch)
	{
		PERF_INC(predictiveCalls);
		predictive_search_bin(queryKey.key, "", phrases);
	}
}

void coral::phonetics::SystemLexicon::get_combined_phrases(
	const coral::phonetics::PhoneticSequence& pinyin_sequence,
	const coral::phonetics::support::KeyProbe& queryKey,
	coral::phonetics::ParsePhrases& phrases,
	int limit)
{
#ifdef _DEBUG
	auto phraseVecSizeBefore = phrases.size();
#endif
	if (!is_combined_loaded_)
		return;

	const auto& queryPinyin = pinyin_sequence.sub_sequences_.empty() ? pinyin_sequence.sequences_ : pinyin_sequence.sub_sequences_;
	const auto& actualInputPinyin = pinyin_sequence.sequences_;
	bool isShowInfos = coral::Engine::compatInstance().config().isShowInfos();
	PERF_INC(combinedLookups);
	if (!combined_key_filter_.may_contain_hash(queryKey.hash))
		return;
	PERF_INC(combinedFilterPass);
	marisa::Agent& agent = combined_agent_;
	agent.set_query(queryKey.key.data(), queryKey.key.size());
	if (!combined_trie_.lookup(agent))
		return;
	PERF_INC(combinedHits);

	size_t rangeIdx = agent.key().id();
	coral::foundation::SliceView<const coral::storage::PhraseRecord> innerPackerInfos = combined_data_view_[rangeIdx];
	const int copyNums = (limit == -1 || limit > static_cast<int>(innerPackerInfos.size())) ? static_cast<int>(innerPackerInfos.size()) : limit;
	for (int i = 0; i < copyNums; ++i)
	{
		auto [word, pinyin, weight, id, backprob, b_in_m2] = combined_data_view_.get_phrase_view(innerPackerInfos[i]);
		if (queryPinyin != actualInputPinyin)
		{
			if (!coral::phonetics::syntax::compare_phrase_pinyin(actualInputPinyin, pinyin, false))
				continue;
		}

		if (isShowInfos)
		{
			int frequency = static_cast<int>(exp(weight) / kInvMaxcount);
			phrases.emplace_back(word, pinyin, weight, id, backprob,
				true, false, false,
					is_corrected_);
		}
		else
		{
			phrases.emplace_back(word, pinyin, weight, id, backprob,
				true, false, false,
					false);
		}
	}
#ifdef _DEBUG
	std::cout << "[Lexicon:Cell] " << "Get " << phrases.size() - phraseVecSizeBefore << " phrases for pinyin: " << pinyin_sequence.to_debug_string() << std::endl; 
#endif
}

void coral::phonetics::SystemLexicon::collect_repaired_candidates(
	const coral::phonetics::ParseTextViews& pinyin_sequence,
	coral::phonetics::ParsePhrases& phrases,
	int limit,
	bool isMixedPinyin)
{
	if (!loaded_) {
		return;
	}
	string key;
	if (isMixedPinyin) {
		// 混拼时用子码（简拼）查词，覆盖更多候选
		coral::phonetics::ParseTextViews subPinyinSeq;
		coral::phonetics::SyllableLattice::get_sub_pinyin_sequences(pinyin_sequence, subPinyinSeq, 0);
		key = coral::phonetics::syntax::join_string_views('\'', subPinyinSeq);
	}
	else {
		key = coral::phonetics::syntax::join_string_views('\'', pinyin_sequence);
	}
	bool isShowInfos = coral::Engine::compatInstance().config().isShowInfos();
	{
		uint32_t entry_offset = 0, entry_count = 0;
		if (primary_bin_.find_key(key.data(), key.size(), entry_offset, entry_count))
		{
			const coral::storage::LexiconEntryRecord* binEntries = primary_bin_.entries();
			for (uint32_t i = 0; i < entry_count; ++i)
			{
				const coral::storage::LexiconEntryRecord& e = binEntries[entry_offset + i];
				const auto word = primary_bin_.entry_word(e);
				const auto pinyin = primary_bin_.entry_pinyin(e);
				if (isMixedPinyin && !coral::phonetics::syntax::compare_phrase_pinyin(pinyin_sequence, pinyin, false))
					continue;
				if (isShowInfos)
				{
					int frequency = static_cast<int>(exp(e.weight) / kInvMaxcount);
					phrases.emplace_back(word, pinyin, e.weight, e.phrase_id, e.backprob,
						true, e.b_in_m2 != 0, false,
						is_corrected_);
				}
				else
				{
					phrases.emplace_back(word, pinyin, e.weight, e.phrase_id, e.backprob,
						true, e.b_in_m2 != 0, false,
						false);
				}
				if (limit != -1 && static_cast<int>(phrases.size()) >= limit)
					break;
			}
		}
	}
}

bool coral::phonetics::SystemLexicon::update_combined_lexicon()
{
	const auto currentVersion = coral::Engine::compatInstance().config().combined_lexicon_version();
	if (currentVersion == combined_lexicon_version_)
	{
		LOG_TRACE("Combined lexicon is already up to date, current version: " + std::to_string(currentVersion) + ", loaded version: " + std::to_string(combined_lexicon_version_));
		return true;
	}
	is_combined_loaded_ = false;
	LOG_INFO("Updating combined lexicon from version " + std::to_string(combined_lexicon_version_) + " to " + std::to_string(currentVersion));
	combined_lexicon_version_ = currentVersion;	// 无论如何，先更新版本号，避免重复加载。即使失败了，重复加载也没意义
	auto trieFilePath = hlp::getLocalLowCoralPinyinPath() / "cell" / "combinedLexicon.trie";
	auto dataFilePath = hlp::getLocalLowCoralPinyinPath() / "cell" / "combinedLexicon.data";
	LOG_INFO("Trie file path: " + trieFilePath.string() + ", Data file path: " + dataFilePath.string());
	if (!std::filesystem::exists(trieFilePath) || !std::filesystem::exists(dataFilePath))
		return false;

	// 加载 trie（内存映射与 trie 表）
	const auto combinedTrieName = name_ + "CombinedModel";
	combined_trie_mapper_.reset();
	if (!combined_trie_mapper_.load(hlp::buildSHNameW(std::to_string(currentVersion), combinedTrieName), trieFilePath, "", coral::runtime::resource_access())) {
		LOG_TRACE("Langengine: Failed to load combined lexicon trie model:" + combinedTrieName);	// level为TRACE，避免出错后频繁日志记录，影响普通用户
		assert(false);
		return false;
	}
	LOG_INFO("Langengine: Successfully loaded combined lexicon trie model: " + combinedTrieName);
	combined_trie_.map(combined_trie_mapper_.data(), combined_trie_mapper_.size());
	// 加载数据
	const auto combinedSharedMemoryName = name_ + "CombinedSharedMemory";
	combined_data_view_.reset();
	// 细胞词库不启用inm2
	combined_data_view_.set_enable_in_m2(false);
	if (!combined_data_view_.map(dataFilePath.wstring(), hlp::buildSHNameW(std::to_string(currentVersion), combinedSharedMemoryName), ""))
		return false;
	LOG_INFO("Langengine: Successfully loaded combined lexicon shared memory: " + combinedSharedMemoryName);

	combined_key_filter_.build(combined_trie_);
	is_combined_loaded_ = true;
	return true;
}

void coral::phonetics::SystemLexicon::predictive_search_bin(std::string_view prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases)
{
	// 与 marisa predictive_search 逐位等价：键按字典序枚举，跳过与 prefixKey 全等的键，
	// 最多扩展 MAX_EXTENDED_KEYS 个键、跟入 MAX_EXTENDED_PHRASES 个扩展词。
	const int MAX_EXTENDED_KEYS = 5;
	const int MAX_EXTENDED_PHRASES = 2;
	int extendedCount = 0;
	int addedPhraseCount = 0;
	uint32_t idx = primary_bin_.lower_bound(prefixKey.data(), prefixKey.size());
	while (extendedCount < MAX_EXTENDED_KEYS && primary_bin_.key_has_prefix(idx, prefixKey.data(), prefixKey.size()))
	{
		if (primary_bin_.key_at(idx) == prefixKey)
		{
			++idx;
			continue;
		}
		uint32_t entry_offset = 0, entry_count = 0;
		if (primary_bin_.key_entries(idx, entry_offset, entry_count))
		{
			const coral::storage::LexiconEntryRecord* binEntries = primary_bin_.entries();
			for (uint32_t i = 0; i < entry_count; ++i)
			{
				const coral::storage::LexiconEntryRecord& e = binEntries[entry_offset + i];
				const auto word = primary_bin_.entry_word(e);
				if (word.size() < prefixValue.size() || word.compare(0, prefixValue.size(), prefixValue) != 0)
					continue;
				if (addedPhraseCount >= MAX_EXTENDED_PHRASES)
					break;
				phrases.emplace_back(word, primary_bin_.entry_pinyin(e), kLogInfiniteSimal, kExtendedMatchId, e.backprob);
				addedPhraseCount++;
			}
		}
		++idx;
		extendedCount++;
		if (addedPhraseCount >= MAX_EXTENDED_PHRASES)
			break;
	}
}

void coral::phonetics::SystemLexicon::for_each_single_char_entry(const std::function<void(std::string_view, std::string_view, float)>& visit) const
{
	if (!loaded_)
		return;
	const coral::storage::LexiconEntryRecord* binEntries = primary_bin_.entries();
	for (uint32_t k = 0; k < primary_bin_.key_count(); ++k)
	{
		// 单音节 key 下才有单字；简拼 key（如 "z"）同样收录单字，靠 entry 的全拼区分读音
		if (primary_bin_.key_at(k).find('\'') != std::string_view::npos)
			continue;
		uint32_t entry_offset = 0, entry_count = 0;
		if (!primary_bin_.key_entries(k, entry_offset, entry_count))
			continue;
		for (uint32_t i = 0; i < entry_count; ++i)
		{
			const coral::storage::LexiconEntryRecord& e = binEntries[entry_offset + i];
			const auto word = primary_bin_.entry_word(e);
			const auto pinyin = primary_bin_.entry_pinyin(e);
			if (word.empty() || pinyin.empty() || pinyin.find('\'') != std::string_view::npos)
				continue;
			const auto lead = static_cast<unsigned char>(word[0]);
			const size_t charBytes = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
			if (word.size() == charBytes)
				visit(word, pinyin, e.weight);
		}
	}
}

bool coral::phonetics::SystemLexicon::find_phrase(std::string_view key, std::string_view word, float& weight) const
{
	if (!loaded_)
		return false;
	uint32_t entry_offset = 0, entry_count = 0;
	if (!primary_bin_.find_key(key.data(), key.size(), entry_offset, entry_count))
		return false;
	const coral::storage::LexiconEntryRecord* binEntries = primary_bin_.entries();
	for (uint32_t i = 0; i < entry_count; ++i)
	{
		const coral::storage::LexiconEntryRecord& e = binEntries[entry_offset + i];
		if (primary_bin_.entry_word(e) == word)
		{
			weight = e.weight;
			return true;
		}
	}
	return false;
}

uint32_t coral::phonetics::SystemLexicon::find_context_id(std::string_view context) const
{
    const auto* entries = primary_bin_.entries();
    const auto begin = context.size() > 48 ? context.size() - 48 : 0;
    for (std::size_t start = begin; start < context.size(); ++start) {
        if ((static_cast<unsigned char>(context[start]) & 0xC0) == 0x80) continue;
        const auto suffix = context.substr(start);
        auto found = std::lower_bound(text_index_.begin(), text_index_.end(), suffix,
            [&](uint32_t index, std::string_view text) { return primary_bin_.entry_word(entries[index]) < text; });
        if (found != text_index_.end() && primary_bin_.entry_word(entries[*found]) == suffix)
            return entries[*found].phrase_id;
    }
    return kInvalidPhraseId;
}

void coral::phonetics::SystemLexicon::collect_continuations(std::string_view context,
    std::string_view pinyin, std::vector<coral::phonetics::LexiconPhrase>& phrases) const
{
    if (pinyin.size() < 2) return;
    const auto* entries = primary_bin_.entries();
    const auto begin = context.size() > 48 ? context.size() - 48 : 0;
    std::string input(pinyin);
    input.erase(std::remove(input.begin(), input.end(), '\''), input.end());
    for (std::size_t start = begin; start < context.size(); ++start) {
        if ((static_cast<unsigned char>(context[start]) & 0xC0) == 0x80) continue;
        const auto prefix = context.substr(start);
        auto found = std::lower_bound(text_index_.begin(), text_index_.end(), prefix,
            [&](uint32_t index, std::string_view text) { return primary_bin_.entry_word(entries[index]) < text; });
        // 只有至少两个汉字的上下文才尝试词库续接；搜索与返回数量都有上限。
        if (prefix.size() < 6) break;
        std::size_t scanned = 0;
        for (; found != text_index_.end() && scanned < 2048; ++found, ++scanned) {
            const auto& entry = entries[*found];
            const auto word = primary_bin_.entry_word(entry);
            if (word.substr(0, prefix.size()) != prefix) break;
            if (word.size() <= prefix.size()) continue;
            std::size_t chars = 0;
            for (unsigned char c : prefix) if ((c & 0xC0) != 0x80) ++chars;
            auto full_pinyin = primary_bin_.entry_pinyin(entry);
            std::size_t pos = 0;
            for (std::size_t i = 0; i < chars && pos != std::string_view::npos; ++i) {
                const auto separator = full_pinyin.find('\'', pos);
                pos = separator == std::string_view::npos ? separator : separator + 1;
            }
            if (pos == std::string_view::npos) continue;
            const auto remaining = full_pinyin.substr(pos);
            std::string letters(remaining);
            letters.erase(std::remove(letters.begin(), letters.end(), '\''), letters.end());
            if (letters != input) continue;
            phrases.emplace_back(word.substr(prefix.size()), remaining, entry.weight,
                kInvalidPhraseId, entry.backprob, false, false, false, false);
            if (phrases.size() >= 32) return;
        }
        if (!phrases.empty()) return;
    }
}

bool coral::phonetics::SystemLexicon::find_english_word(std::string_view word, coral::phonetics::LexiconPhrase& info) const
{
	const auto key = coral::english_identity_key(word);
	const auto words = std::atomic_load_explicit(&english_words_, std::memory_order_acquire);
	if (!words) return false;
	const auto it = words->find(key);
	if (it == words->end()) return false;
	info.id = it->second.first;
	info.phrase_weight = it->second.second;
	return true;
}

bool coral::phonetics::SystemLexicon::predictive_search(marisa::Agent& agent, const string& prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases)
{
	// marisa 查询路径已下线：主词典前缀扩展统一由 predictiveSearchBin（mainlexicon.coral）完成。
	// 接口保留以兼容基类/云词典调用，云词典前缀扩展随 marisa 路径一并停用。
	(void)agent; (void)prefixKey; (void)prefixValue; (void)phrases;
	return false;
}

void coral::phonetics::SystemLexicon::reset_storage()
{

}

bool coral::phonetics::SystemLexicon::build_lexicon(const filesystem::path &rawDataPath,
								   const filesystem::path &triePath, const filesystem::path &dataPath)
{
	struct DictionaryEntry
	{
		std::string pinyin;
		std::string word;
		std::string fullPinyin;
		uint32_t id{};
		bool b_in_m2{};
		int frequency{};
	};
	std::ifstream file(rawDataPath);
	if (!file.is_open())
	{
		return false;
	}

	std::string line;
	if (!std::getline(file, line))
	{
		file.close();
		return false;
	}

	int lineNumber = 1;
	using RawDict = std::map<std::string, std::vector<coral::storage::OwnedPhraseRecord>>;
	RawDict raw_data;
	while (std::getline(file, line))
	{
		lineNumber++;	// 前两行跳过
		if (line.empty() || lineNumber <= 3)
		{
			continue;
		}

		std::istringstream iss(line);
		std::string token;
		std::vector<std::string> columns;

		while (std::getline(iss, token, ','))
		{
			columns.push_back(token);
		}

		if (columns.size() < 5)
		{
			std::cerr << "Warning: Line " << lineNumber << " has " << columns.size()
					  << " columns, expected at least 5. Skipping line: '" << line << "'"
					  << std::endl;
			continue;
		}
		DictionaryEntry entry;

		entry.id = std::stoi(columns[0]);
		entry.pinyin = columns[1];
		entry.frequency = std::stoi(columns[2]);
		entry.word = columns[3];
		entry.fullPinyin = columns[4];

		coral::phonetics::Score backprob = kMinBackprob; // 默认回退概率
		if (columns.size() >= 6)
		{
			backprob = std::stof(columns[5]);
		}
		if (columns.size() >= 7)
		{
			entry.b_in_m2 = std::stoi(columns[6]) != 0;
		}
		coral::storage::OwnedPhraseRecord phinfo{entry.word, entry.fullPinyin,
							static_cast<coral::phonetics::Score>(log(entry.frequency * kInvMaxcount)), entry.id,
							backprob, entry.b_in_m2};
		raw_data[entry.pinyin].push_back(phinfo);
	}
	file.close();

	marisa::Keyset keyset;
	std::vector<std::vector<coral::storage::OwnedPhraseRecord>> phraseInfo;
	phraseInfo.resize(raw_data.size());
	for (const auto &pair : raw_data)
	{
		const std::string &pinyinKeyStr = pair.first;
		marisa::Key key;
		key.set_str(static_cast<const char *>(pinyinKeyStr.data()),
					pinyinKeyStr.size());
		keyset.push_back(key);
	}

	trie_.build(keyset);

	for (const auto &pair : raw_data)
	{
		const std::string &pinyinKeyStr = pair.first;
		std::vector<coral::storage::OwnedPhraseRecord> currentPhrases = pair.second;
		std::sort(currentPhrases.begin(), currentPhrases.end(),
				  [](const coral::storage::OwnedPhraseRecord &a, const coral::storage::OwnedPhraseRecord &b)
				  { return a.phrase_weight > b.phrase_weight; });
		sortPrimaryPhrasesByInM2AndWeight(currentPhrases);

		marisa::Agent agent;
		agent.set_query(pinyinKeyStr.data(), pinyinKeyStr.size());
		trie_.lookup(agent);
		marisa::UInt32 resultIndex = agent.key().id();

		if (resultIndex < phraseInfo.size())
		{
			phraseInfo[resultIndex] = std::move(currentPhrases);
		}
	}
	trie_.save(triePath.string().c_str());
	dump_lexicon(phraseInfo, dataPath, enable_in_m2_);
	return true;
}

bool coral::phonetics::SystemLexicon::compile_lexicon_directory(const filesystem::path &dirPath)
{
	struct DictionaryEntry
	{
		std::string pinyin;
		std::string word;
		std::string fullPinyin;
		uint32_t id{};
		int frequency{};
	};
	// 遍历 dirPath 下所有 .csv 文件，合并为一个 combined.kcell.tmp 文件
	// 然后用合并后的文件调用 buildLexicon
	if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
		return false;

	// 原始数据用两个 std::string 作键。键：拼音 + '|' + 词
	using RawDict = std::unordered_map<std::string, DictionaryEntry>;
	RawDict raw_data;
	for (const auto& fileEntry : std::filesystem::directory_iterator(dirPath))
	{
		if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".csv")
		{
			const auto& csvFilePath = fileEntry.path();
			LOG_TRACE("Found CSV file: " + csvFilePath.string());

			std::ifstream file(csvFilePath);
			if (!file.is_open())
				continue;
			std::string line;
			if (!std::getline(file, line))
			{
				file.close();
				continue;
			}
			int lineNumber = 0;
			while (std::getline(file, line))
			{
				lineNumber++;
				if (line.empty())
					continue;

				std::istringstream iss(line);
				std::string token;
				std::vector<std::string> columns;

				while (std::getline(iss, token, ','))
					columns.push_back(token);

				if (columns.size() < 5)
				{
					LOG_TRACE("Warning: Line " + std::to_string(lineNumber) + " in file " + csvFilePath.string() +
							  " has " + std::to_string(columns.size()) +
							  " columns, expected at least 5. Skipping line: '" + line + "'");
					continue;
				}
				DictionaryEntry entry;

				try
				{
					entry.id = std::stoi(columns[0]);
					entry.pinyin = columns[1];
					entry.frequency = std::stoi(columns[2]);
					entry.word = columns[3];
					entry.fullPinyin = columns[4];
				}
				catch (const std::exception& e)
				{
					LOG_TRACE("Warning: Failed to parse line " + std::to_string(lineNumber) + " in file " + csvFilePath.string() +
							  ". Error: " + e.what() + ". Skipping line: '" + line + "'");
					continue;
				}

				auto key = entry.pinyin + '|' + entry.word;
				if (raw_data.find(key) != raw_data.end())
				{
					// 已存在时比较权重
					auto& existingEntry = raw_data[key];
					existingEntry.frequency += entry.frequency;
				}
				else
				{
					raw_data[key] = entry;
				}
			}
			file.close();
		}
	}
	// 将 combined.kcell.tmp 输出到 dirPath/combined.kcell.tmp
	const auto combinedFilePath = dirPath / "combined.kcell.tmp";
	std::ofstream combinedFile(combinedFilePath, std::ios::binary);
	if (!combinedFile.is_open())
		return false;
	// 写文件头
	combinedFile << "ID,Pinyin,Frequency,Word,FullPinyin,BackProb\n";
	combinedFile << "PlaceholderHeaderLine\n"; // 第二行的占位内容，构建词库时会被忽略
	for (const auto& pair : raw_data)
	{
		const auto& entry = pair.second;
		// 将 GBK 编码的 word 字段转换为 UTF-8
		combinedFile << entry.id << ','
					 << entry.pinyin << ','
					 << entry.frequency << ','
					 << hlp::wstr2str(hlp::ansi2Wstr(entry.word)) << ','
					 << entry.fullPinyin << ','
					 << kMinBackprob << '\n';
	}
	combinedFile.close();
	// 构建细胞词库不启用inm2
	enable_in_m2_ = false;
	auto result = build_lexicon(combinedFilePath, dirPath / (name_ + ".trie"), dirPath / (name_ + ".data"));
	enable_in_m2_ = true;
	if (!result)
	{
		LOG_ERROR("Failed to build lexicon from combined CSV file: " + combinedFilePath.string());
		assert(false);
	}
	return result;
}

bool coral::phonetics::SystemLexicon::build_lexicon_for_cn(const filesystem::path &rawDataPath,
										const filesystem::path &triePath, const filesystem::path &dataPath)
{
	struct DictionaryEntry
	{
		std::string word;
		uint32_t id{};
		int frequency{};
	};
	std::ifstream file(rawDataPath);
	if (!file.is_open())
		return false;

	std::string line;
	if (!std::getline(file, line))
	{
		file.close();
		return false;
	}

	int lineNumber = 1;
	using RawDict = std::map<std::string, std::vector<coral::storage::OwnedPhraseRecord>>;
	RawDict raw_data;

	while (std::getline(file, line))
	{
		lineNumber++;
		if (line.empty() || lineNumber <= 3)
			continue;

		std::istringstream iss(line);
		std::string token;
		std::vector<std::string> columns;

		while (std::getline(iss, token, ','))
		{
			columns.push_back(token);
		}

		if (columns.size() < 5)
		{
			std::cerr << "Warning: Line " << lineNumber << " has " << columns.size()
					  << " columns, expected at least 5. Skipping line: '" << line << "'"
					  << '\n';
			continue;
		}
		DictionaryEntry entry;

		entry.id = std::stoi(columns[0]);
		entry.frequency = std::stoi(columns[2]);
		entry.word = columns[3];
		float backprob = 0.0f;
		coral::storage::OwnedPhraseRecord cnInfo{
			entry.word,
			"",
			static_cast<float>(log(entry.frequency * kInvMaxcount)),
			entry.id,
			backprob};
		// 去重并筛选掉低频词
		if (raw_data.find(entry.word) == raw_data.end() && cnInfo.phrase_weight >= kPredictThreshold)
			raw_data[entry.word].push_back(cnInfo);
	}
	file.close();

	marisa::Keyset keyset;
	std::vector<std::vector<coral::storage::OwnedPhraseRecord>> phraseCNInfo;
	phraseCNInfo.resize(raw_data.size());
	for (const auto &pair : raw_data)
	{
		const std::string &CNKeyStr = pair.first;
		marisa::Key key;
		key.set_str(static_cast<const char *>(CNKeyStr.data()),
					CNKeyStr.size());
		keyset.push_back(key);
	}
	trie_.build(keyset);
	for (const auto &pair : raw_data)
	{
		const std::string &CNKeyStr = pair.first;
		std::vector<coral::storage::OwnedPhraseRecord> currentPhrases = pair.second;
		std::sort(currentPhrases.begin(), currentPhrases.end(),
				  [](const coral::storage::OwnedPhraseRecord &a, const coral::storage::OwnedPhraseRecord &b)
				  {
					  return a.phrase_weight > b.phrase_weight;
				  });

		marisa::Agent agent;
		agent.set_query(CNKeyStr.data(), CNKeyStr.size());
		trie_.lookup(agent);
		marisa::UInt32 resultIndex = agent.key().id();

		if (resultIndex < phraseCNInfo.size())
		{
			phraseCNInfo[resultIndex] = std::move(currentPhrases);
		}
	}
	trie_.save(triePath.string().c_str());
	dump_lexicon(phraseCNInfo, dataPath);
	return true;
}

void coral::phonetics::SystemLexicon::set_is_corrected(bool is_corrected)
{
	is_corrected_ = is_corrected;
}

void coral::phonetics::SystemLexicon::set_enable_in_m2(bool enabled)
{
	// marisa/dat 路径已下线，bin 的 bInM2 在构建时已按数据源固化；
	// 此处仅保留标志，供后续 buildLexicon/dumpLexicon 使用（云词典构建流程）。
	enable_in_m2_ = enabled;
}

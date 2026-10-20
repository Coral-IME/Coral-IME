#include "../runtime/resource_access.h"
#include "pch.h"
#include "phonetic_types.h"
#include "latin_lexicon.h"
#include "source_csv.h"
#include "runtime_context.h"
#include "latin_identity.h"
#include <sstream>  
#include <fstream> 
#include <algorithm>
#include <cmath>

void coral::phonetics::LatinLexicon::add_phrase_to_result(const std::string_view phrase,
	const std::string_view pinyinFormat,
	coral::phonetics::Score weight,
	coral::phonetics::ParsePhrases& phrases)
{
	persistent_strings_.push_back(phrase);
	persistent_strings_.push_back(pinyinFormat);
	const auto storedPhrase = persistent_strings_[persistent_strings_.size() - 2];
	const auto storedPinyin = persistent_strings_[persistent_strings_.size() - 1];

	if (coral::Engine::compatInstance().config().isShowInfos())
		phrases.emplace_back(storedPhrase
			, storedPinyin
			, weight
			, kInvalidPhraseId
			, 0.0f
			, false
			, false
			, true
			, false
			, -1);
	else
		phrases.emplace_back(storedPhrase.data()
			, storedPinyin
			, weight
			, kInvalidPhraseId
			, 0.0f
			, false
			, false
			, true
			, false
			, -1);
}

// 调用方给出的 buffer 至少要能放下 input.size() + 1 个字节
std::string_view coral::phonetics::LatinLexicon::to_lower_case_into(const std::string_view input, char* buffer)
{
	for (size_t i = 0; i < input.size(); ++i)
	{
		const char ch = input[i];
		buffer[i] = coral::phonetics::syntax::is_ascii_upper(ch) ? static_cast<char>(ch - 'A' + 'a') : ch;
	}
	buffer[input.size()] = '\0';
	return std::string_view(buffer, input.size());
}

std::string coral::phonetics::LatinLexicon::to_lower_case(const std::string_view input) const
{
	// 英文词典的 key 全是 ASCII，直接按范围换算即可。原实现走 locale 相关的 ::tolower，
	// 而这个函数在 isWordInLexicon / getWordInfo 里每个候选序列都会调用一次
	std::string result;
	result.resize(input.size());
	for (size_t i = 0; i < input.size(); ++i)
	{
		const char ch = input[i];
		result[i] = coral::phonetics::syntax::is_ascii_upper(ch) ? static_cast<char>(ch - 'A' + 'a') : ch;
	}
	return result;
}

bool coral::phonetics::LatinLexicon::load(const coral::phonetics::ResourceLocation& trieFile,
                           const coral::phonetics::ResourceLocation& infoFile,
                           const string& versionId)
{
	clear_persistent_strings();
	if (!trie_mapper_.load(hlp::buildSHNameW(versionId, "englishTrieModel"), trieFile.path, trieFile.identifier, coral::runtime::resource_access())) {
		LOG_ERROR("LangEngine: Failed to load English trie model via shared memory loader.");
		return false;
	}
	trie_.map(trie_mapper_.data(), trie_mapper_.size());
	if (!data_view_.map(infoFile.path.wstring(), hlp::buildSHNameW(versionId, "englishLexiconSharedMemory"), infoFile.identifier))
		return false;
	key_filter_.build(trie_);
	//英文单词长度
	set_max_length(20);
	loaded_ = true;
	return true;
}

void coral::phonetics::LatinLexicon::get_phrases(
	const std::string& inputString,
	coral::phonetics::ParsePhrases& phrases,
	const int limit)
{
	if (!loaded_) {
		return;
	}
	if (limit <= 0)
		return;

	check_and_cleanup_persistent_strings();
	std::string input = to_lower_case(inputString);
	if (inputString.length() <= kMinEnglishLexiconLength) {
		marisa::Agent agent;
		agent.set_query(input.c_str());
		if (!trie_.lookup(agent)) return;
	}
	
	persistent_strings_.reserve(persistent_strings_.size() + 20);
	marisa::Agent agent;
	agent.set_query(input.c_str());
	while(predictive_search(agent, input, inputString, phrases))
	{
		if (phrases.size() >= static_cast<size_t>(limit))
			break;
	}
}

bool coral::phonetics::LatinLexicon::predictive_search(marisa::Agent& agent, const string& prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases)
{
	if(!loaded_){
		return false;
	}
	bool found = trie_.predictive_search(agent);
	if(!found)
		return false;

	std::string_view prefixMatch(agent.key().ptr(), agent.key().length());
	marisa::UInt32 resultIndex = agent.key().id();
	coral::foundation::SliceView<const coral::storage::PhraseRecord> innerPackerInfos = data_view_[resultIndex];
	coral::phonetics::Score weight = -5.0;
	std::string_view resultPhrase;
	if (resultIndex < data_view_.outer_size() && data_view_[resultIndex].size() > 0)
	{
		auto phraseInfo = data_view_.get_phrase_view(innerPackerInfos[0]);
		weight = phraseInfo.weight;
		resultPhrase = phraseInfo.word;
	}
	else
		resultPhrase = prefixMatch;
	add_phrase_to_result(resultPhrase, prefixValue, weight, phrases);
	return found;
}

void coral::phonetics::LatinLexicon::reset_storage()
{
	clear_persistent_strings();
}

bool coral::phonetics::LatinLexicon::build_lexicon(const filesystem::path& rawDataPath,
	const filesystem::path& triePath,
	const filesystem::path& dataPath)
{
	struct EnglishDictionaryEntry {
		std::string word;
		int frequency{};
		bool canSplitIntoSyllables{};
	};
	std::ifstream file(rawDataPath, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}
	using RawDict = std::map<std::string, std::vector<coral::storage::OwnedPhraseRecord>>;
	RawDict raw_data;
	try {
		coral::data::read_source_csv_header(file, {"search_word", "display_word", "frequency", "can_split"});
		std::vector<std::string> fields;
		while (coral::data::read_source_csv_row(file, fields)) {
			if (fields.size() == 1 && fields[0].empty()) continue;
			if (fields.size() != 4 || fields[0].empty() || fields[1].empty())
				throw std::runtime_error("Expected search_word,display_word,frequency,can_split in English CSV");
			size_t end = 0;
			const int frequency = std::stoi(fields[2], &end);
			if (end != fields[2].size() || frequency <= 0 || (fields[3] != "0" && fields[3] != "1"))
				throw std::runtime_error("Invalid English frequency or can_split value");
			const auto& search_word = fields[0];
			const auto& display_word = fields[1];
			const int canSplit = fields[3] == "1" ? 1 : 0;
			std::string pinyinKey = to_lower_case(search_word);  // 使用第一列作为查找键
			EnglishDictionaryEntry entry;
			entry.word = display_word;  // 存储第二列的显示词
			entry.frequency = frequency;
			entry.canSplitIntoSyllables = (canSplit == 1);
			coral::storage::OwnedPhraseRecord phinfo{ entry.word, pinyinKey,  // entry.word现在是显示词
								static_cast<coral::phonetics::Score>(log(entry.frequency * kInvMaxcount)),
								kInvalidPhraseId,
								entry.canSplitIntoSyllables ? 1.0f : 0.0f };
			raw_data[pinyinKey].push_back(phinfo);
		}
		if (raw_data.empty()) throw std::runtime_error("English CSV has no entries");
	} catch (const std::exception& error) {
		std::cerr << rawDataPath.string() << ": " << error.what() << '\n';
		return false;
	}
	file.close();

	marisa::Keyset keyset;
	std::vector<std::vector<coral::storage::OwnedPhraseRecord>> phraseInfo;
	phraseInfo.resize(raw_data.size());
	for (const auto& pair : raw_data) {
		const std::string& pinyinKeyStr = pair.first;
		marisa::Key key;
		key.set_str(static_cast<const char*>(pinyinKeyStr.data()),
			pinyinKeyStr.size());
		keyset.push_back(key);
	}
	trie_.build(keyset);
	for (const auto& pair : raw_data) {
		const std::string& pinyinKeyStr = pair.first;
		std::vector<coral::storage::OwnedPhraseRecord> current_phrases = pair.second;
		std::sort(current_phrases.begin(), current_phrases.end(),
			[](const coral::storage::OwnedPhraseRecord& a, const coral::storage::OwnedPhraseRecord& b) {
				return a.phrase_weight > b.phrase_weight; // 降序排列
			});
		marisa::Agent lookup_agent;
		lookup_agent.set_query(pinyinKeyStr.data(), pinyinKeyStr.size());
		trie_.lookup(lookup_agent);
		marisa::UInt32 resultIndex = lookup_agent.key().id();
		if (resultIndex < phraseInfo.size()) {
			phraseInfo[resultIndex] = std::move(current_phrases);
		}
	}
	trie_.save(triePath.string().c_str());
	dump_lexicon(phraseInfo, dataPath);
	return true;
}

bool coral::phonetics::LatinLexicon::is_word_in_lexicon(const std::string_view word) const
{
	if (!loaded_) {
		return false;
	}
	// 词典里最长的词就是 MAX_ENGLISH_LEXICON_LENGTH，更长的拼音串不可能命中。
	// 取词阶段每个查不到词的 span 都会走到这里，越早退出越好
	if (word.length() < kMinEnglishLexiconLength || word.length() > kMaxEnglishLexiconLength
		|| coral::english_identity_key(word).empty()) {
		return false;
	}
	char buffer[kMaxEnglishLexiconLength + 1];
	const std::string_view input = to_lower_case_into(word, buffer);
	if (!key_filter_.may_contain(input))
		return false;
	marisa::Agent& agent = agent_;
	agent.set_query(input.data(), input.size());
	return trie_.lookup(agent);
}

bool coral::phonetics::LatinLexicon::get_word_info(const std::string_view word, coral::phonetics::LexiconPhrase& info) const
{
	if (!loaded_ || word.length() > kMaxEnglishLexiconLength || coral::english_identity_key(word).empty())
		return false;
	char buffer[kMaxEnglishLexiconLength + 1];
	const std::string_view input = to_lower_case_into(word, buffer);
	if (!key_filter_.may_contain(input))
		return false;
	marisa::Agent& agent = agent_;
	agent.set_query(input.data(), input.size());
	if (trie_.lookup(agent))
	{
		marisa::UInt32 resultIndex = agent.key().id();
		if (resultIndex < data_view_.outer_size() && data_view_[resultIndex].size() > 0)
		{
			coral::foundation::SliceView<const coral::storage::PhraseRecord> innerPackerInfos = data_view_[resultIndex];
			auto phraseView = data_view_.get_phrase_view(innerPackerInfos[0]);
			// 旧的 english.dat 在 id 中存的是词频；绝不能把它解释成模型 ID。
			info = coral::phonetics::LexiconPhrase(phraseView.word, phraseView.pinyin, phraseView.weight, kInvalidPhraseId, 0.0f, false, false, true);
			return true;
		}
	}
	return false;
}

void coral::phonetics::LatinLexicon::clear_persistent_strings()
{
	persistent_strings_.clear();
	persistent_strings_.shrink_to_fit(); 
}

void coral::phonetics::LatinLexicon::common_prefix_lengths(std::string_view input, std::vector<size_t>& lengths) const
{
	lengths.clear();
	if (!loaded_) return;
	size_t count = 0;
	while (count < input.size() && count < kMaxEnglishLexiconLength) {
		const unsigned char ch = input[count];
		if (ch < 0x21 || ch > 0x7e || ch == '\'') break;
		++count;
	}
	char buffer[kMaxEnglishLexiconLength + 1];
	const auto key = to_lower_case_into(input.substr(0, count), buffer);
	marisa::Agent agent;
	agent.set_query(key.data(), key.size());
	while (trie_.common_prefix_search(agent)) {
		const size_t size = agent.key().length();
		// 单字母仍是拼音；两字母词（AI/do/in）是合法词元。
		if (size >= 2) lengths.push_back(size);
	}
}

void coral::phonetics::LatinLexicon::check_and_cleanup_persistent_strings()
{
	if (persistent_strings_.size() > kMaxPersistentStringsSize) {
		clear_persistent_strings();
	}
}

bool coral::phonetics::LatinLexicon::is_full_pinyin_word(const std::string_view word) const
{
	if(!loaded_){
		return false;
	}
	std::string input = to_lower_case(word);
	if (!key_filter_.may_contain(input))
		return false;
	marisa::Agent& agent = agent_;
	agent.set_query(input.c_str());
	if (trie_.lookup(agent)) {
		marisa::UInt32 resultIndex = agent.key().id();
		if (resultIndex < data_view_.outer_size() && data_view_[resultIndex].size() > 0) {
			coral::foundation::SliceView<const coral::storage::PhraseRecord> innerPackerInfos = data_view_[resultIndex];
			auto phraseInfo = data_view_.get_phrase_view(innerPackerInfos[0]);
			// backprob字段存储了音节分割信息：1.0表示可分音节，0.0表示不可分音节
			return phraseInfo.backprob > 0.5;
		}
	}
	return false;
}

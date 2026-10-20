#pragma once
#ifndef CORAL_MODULES_PHONETICS_LATIN_LEXICON_H_INCLUDED
#define CORAL_MODULES_PHONETICS_LATIN_LEXICON_H_INCLUDED

#include "lexicon_source.h"
#include "phonetic_types.h"
#include "verified_resource.h"
#include "../storage/phrase_archive_view.hpp"
#include "support/lookup_filter.hpp"
#include <marisa.h>

namespace coral::phonetics {
class LatinLexicon : public coral::phonetics::LexiconSource
{
public:
	LatinLexicon() = default;

	bool load(const coral::phonetics::ResourceLocation& trieFile,
	          const coral::phonetics::ResourceLocation& infoFile,
	          const string& versionId) override;
	void reset_storage() override;
	bool build_lexicon(const filesystem::path& rawDataPath,
		const filesystem::path& triePath,
		const filesystem::path& dataPath) override;

	bool is_word_in_lexicon(const std::string_view word) const;
	bool get_word_info(const std::string_view word, coral::phonetics::LexiconPhrase& info) const;
	void clear_persistent_strings();
	void get_phrases(
		const std::string& inputString,
		coral::phonetics::ParsePhrases& phrases,
		const int limit);
	bool predictive_search(marisa::Agent& agent, const string& prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases) override;
	bool is_full_pinyin_word(const std::string_view word) const;
	// 短词典键在拼音句子中过于含糊。整段输入的英文查找
	// 与补全不受此混合输入限制。
	static bool allows_mixed_word(std::string_view word) noexcept { return word.size() >= 5; }
	// 对原始输入后缀做精确匹配，独立于拼音边界。
	void common_prefix_lengths(std::string_view input, std::vector<size_t>& lengths) const;

private:
	void add_phrase_to_result(const std::string_view phrase,
		const std::string_view pinyinFormat,
		coral::phonetics::Score weight,
		coral::phonetics::ParsePhrases& phrases);
	std::string to_lower_case(const std::string_view input) const;
	// 定长键的小写化，写进调用方的栈缓冲，省掉查词路径上每次一个 std::string
	static std::string_view to_lower_case_into(const std::string_view input, char* buffer);
	void check_and_cleanup_persistent_strings();

private:
	coral::storage::PhraseArchiveView data_view_;
	coral::storage::VerifiedResource trie_mapper_;
	// 复用的查询代理：Agent 首次查询时会 new 出内部 State，之后 set_query 只做 reset。
	// isWordInLexicon 是逐词条调用的，每次新建一个 Agent 的分配开销不可忽略。
	mutable marisa::Agent agent_;
	// 中文候选查不到词时才回退查英文词典，实测 376 万次查询只命中 4288 次，
	// 先过一遍键存在性过滤器，绝大多数落空的查询不必进 trie（详见 utils/keyfilter.hpp）
	coral::phonetics::support::KeyFilter key_filter_;
	// 词典对象跨解析存活，这里不能用 arena 分配器
	coral::phonetics::PersistentTextViews persistent_strings_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // ENGLISHLEXICON_H

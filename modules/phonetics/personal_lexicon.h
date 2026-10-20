#pragma once

#include "lexicon_source.h"
#include "coral/vocabulary/lexicon_types.h"
#include "coral/vocabulary/phrase_override.h"
#include "ini_store.h"
#include "../learning/interaction_history_manager.h"
#include "../ranking/candidate_rules_manager.h"
#include <deque>
#include <memory>

namespace coral::phonetics {
class LatinLexicon;
} // 命名空间 coral::phonetics 结束（批次 2）


// 自动历史（含短拼音条目）共享同一预算。
static constexpr int MAX_CAPACITY = 100000;

// 协调历史与手动规则；各管理器持有自己的持久化。
namespace coral::phonetics {
class PersonalLexicon
{
public:
    PersonalLexicon()
        : history_(std::make_unique<coral::history::Manager>()),
          candidate_policy_(std::make_unique<coral::candidate_policy::Manager>()) {}
    ~PersonalLexicon() { shutdown(); }

    void set_english_lexicon(coral::phonetics::LatinLexicon* lexicon) { english_lexicon_ = lexicon; }
	void set_is_corrected(bool val);
	void reset_storage() { phrase_storage_.clear(); }

	[[nodiscard]] int max_length() const
	{
		return max_len_;
	}

	void set_max_length(const int len)
	{
		max_len_ = len;
	}

	void get_phrases(
		const coral::phonetics::PhoneticSequence& pinyin_sequence,
		coral::phonetics::ParsePhrases& phrases,
		coral::LanguageVariant variant);

	bool load(
		const filesystem::path& userLexiconBinPath,
		const syllable_strview_map& syllables);

	bool update_user_lexicon(
		const string& py,
		const string& sent, coral::LanguageVariant variant);

    bool pin_candidate_first(const string& pinyin, const string& word,
                           const string& fullPinyin, coral::LanguageVariant variant);
    bool unpin_candidate_first(const string& pinyin, const string& word,
                             coral::LanguageVariant variant);
    bool disable_candidate(const string& pinyin, const string& word,
                          const string& fullPinyin, coral::LanguageVariant variant);

	std::vector<coral::candidate_policy::Rule> get_candidate_policies(
		const std::string& lookupKey,
		coral::LanguageVariant variant) const;

	// 常用语同时写入简体与繁体两套规则，不随当前简繁模式变化。
	bool add_phrase_shortcut(const std::string& code, const std::string& phrase);
	bool remove_phrase_shortcut(const std::string& code, const std::string& phrase);
	std::vector<CustomPhraseEntry> list_phrase_shortcuts() const;

	bool store_lexicon() const;

	bool clear_user_lexicon();

	std::pair<string, string> get_user_lexicon_path() const;

	static bool upload_user_dictionary(coral::LanguageVariant variant);

	bool load_by_line(
		const string& pinyinCode,
		const string& quanPyCode,
		const string& sent,
		coral::phonetics::Score dee,
		int tickTime,
		const syllable_strview_map& syllables,
		coral::LanguageVariant variant);

	void begin_load_by_line() {
		is_alive_ = true;
	}

	void end_load_by_line() {
		is_alive_ = false;
	}

    bool maintain_user_data();

	bool re_connect_user_lexicon();
	bool export_unified_user_lexicon_as_csv(const std::filesystem::path& exportPath) const;
	hlp::ImportResult import_unified_user_lexicon_from_csv(const std::filesystem::path& importPath,
										  const syllable_strview_map& syllables);

	// 词库导入：批量写入/撤销用户历史并立即落盘
	bool is_alive() const { return is_alive_; }
	std::vector<coral::history::Entry> history_entries() const;
	std::uint64_t history_sequence() const;
	bool import_history_entries(std::vector<coral::history::Entry> entries);
	bool remove_history_entries(const std::vector<coral::history::EntryIdentity>& identities);
	static bool get_jian_pin_code(const string& py, string& jianPin, bool filterYuan = false);

	bool revert_last_action_for_phrase(const std::string& delPhrase);
	void shutdown();

private:
    static void prepare_commit_phrases(vector<string>& pys, vector<string>& phrases,
                                     const string& pinyin, const string& sentence);

    int max_len_{30};
    int size_{0};
    bool is_corrected_{false};
    bool is_alive_{false};
    filesystem::path legacy_base_path_;
    coral::phonetics::LatinLexicon* english_lexicon_{nullptr};
    std::deque<std::string> phrase_storage_;
    std::unique_ptr<coral::history::Manager> history_;
    std::unique_ptr<coral::candidate_policy::Manager> candidate_policy_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


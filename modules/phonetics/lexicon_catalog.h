/*
* @author: huangzhishou
* @date: 2021-5-28 18:02:25
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_LEXICON_CATALOG_H_INCLUDED
#define CORAL_MODULES_PHONETICS_LEXICON_CATALOG_H_INCLUDED

#if defined(_WIN32) || defined(_MSC_VER)
#include <thread>
#include <atomic>
#endif
#include "lexicon_source.h"
#include "system_lexicon.h"
#include "personal_lexicon.h"
#include "latin_lexicon.h"
#include "mode_selection.h"
#include <ctime>
#include <vector>

inline coral::LanguageVariant get_language_variant() {
	// 当前生效值（含 TSF 临时切换）由 InputModeState 持有
	return coral::inputModeState().isTraditionalChinese() ? coral::LanguageVariant::Traditional : coral::LanguageVariant::Simplified;
}

namespace coral::phonetics {
class LexiconCatalog
{
public:
	LexiconCatalog() = default;
	~LexiconCatalog();


	bool load_pinyin_model(const coral::phonetics::LexiconLocations& path, const syllable_strview_map& syllables);
	bool load_primary_lexicon(const coral::phonetics::LexiconLocations& path);

	void collect_candidates(
		const coral::phonetics::PhoneticSequence& pinyin_sequence,
		coral::phonetics::ParsePhrases& phraseVec,
		const int limit = -1,
		bool isShowInfos = false,
		bool is_corrected = false,
		bool isAbbreviatehasFull = false,
		bool isMaxPinyinSequence = false
	);
	void collect_repaired_candidates(
		const coral::phonetics::ParseTextViews& pinyin_sequence,
		coral::phonetics::ParsePhrases& phraseVec,
		coral::LanguageVariant mainType,
		int limit,
		bool isMixedPinyin = false
	);

	void reset_storage();	// 重置所有词典的临时存储，主要是指防止string_view悬空而保存的string
	int max_length() const;

	bool update_user_lexicon(
		const string& py,
		const string& sent);

	bool store_lexicon() const;
	bool clear_lexicon();
	string get_user_lexicon_path() const;
	bool reload_user_lexicon(const syllable_strview_map& syllables);

	bool upload_user_dictionary()
	{
		//添加繁体字典的判断
		return user_lexicon_.upload_user_dictionary(get_language_variant());
	}

	bool load_user_lexicon_by_line(
		const std::string& pinyinCode,
		const std::string& quanPyCode,
		const std::string& sent,
		coral::phonetics::Score dee,
		int tickTime,
		const syllable_strview_map& syllables) 
	{
		return user_lexicon_.load_by_line(
			pinyinCode, quanPyCode, sent, dee, tickTime, syllables, get_language_variant()
		);
	}

	void begin_load_user_lexicon_by_line() 
	{
		user_lexicon_.begin_load_by_line();
	}

	void end_load_user_lexicon_by_line() 
	{
		// 不可能同时使用繁体和简体， 使用一把锁即可
		user_lexicon_.end_load_by_line();
	}

	bool maintain_lexicon(
		const syllable_strview_map& syllables
	);
	bool revert_last_action_for_phrase(const std::string& delPhrase);
	void shutdown_user_history() { user_lexicon_.shutdown(); }
	bool export_user_lexicon(const filesystem::path& lexiconPath) const;
	hlp::ImportResult import_user_lexicon(const filesystem::path& lexiconPath, const syllable_strview_map& syllables);
	void get_english_phrases(const std::string& originalInput, coral::phonetics::ParsePhrases& phrases, int limit);
	bool get_eng_word_info(const std::string_view word, coral::phonetics::LexiconPhrase& info) const;
	coral::phonetics::SystemLexicon& get_primary_lexicon() { return primary_lexicon_; }
	const coral::phonetics::LatinLexicon& get_english_lexicon() const { return english_lexicon_; }
	coral::phonetics::PersonalLexicon& get_user_lexicon() { return user_lexicon_; }

private:
	coral::phonetics::SystemLexicon primary_lexicon_;
	coral::phonetics::PersonalLexicon user_lexicon_;
	coral::phonetics::LatinLexicon english_lexicon_;
	string user_conifig_file_;
	// getPhrases 去重用的临时表。一次解析要调用上百次 getPhrases，做成成员复用桶数组，
	// 避免每次都重新分配；与解析流程一样假定单线程调用。
	flat_hash_map<std::string_view, size_t> dedup_index_;
	// 同理，各词典共用的查询 key 拼接缓冲
	std::string query_key_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // LEXICONMANAGER_H

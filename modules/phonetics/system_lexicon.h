/*
 * @author: huangzhishou
 * @date: 2021-5-28 18:02:25
 */
#pragma once
#ifndef CORAL_MODULES_PHONETICS_SYSTEM_LEXICON_H_INCLUDED
#define CORAL_MODULES_PHONETICS_SYSTEM_LEXICON_H_INCLUDED

#include "lexicon_source.h"
#include <marisa.h>
#include "verified_resource.h"
#include "lexicon_table.h"
#include "../storage/phrase_archive_view.hpp"
#include "support/lookup_filter.hpp"
#include <functional>
#include <string_view>
#include <memory>
#include <atomic>

namespace coral::phonetics {
class SystemLexicon : public coral::phonetics::LexiconSource
{
public:
	SystemLexicon() : name_("PrimaryLexicon") {}
	explicit SystemLexicon(std::string name) : name_(std::move(name)) {}
	~SystemLexicon() override = default;

	// queryKey 是 pinyinSequence 的查询拼音拼接结果，由调用方拼好后各词典共用，
	// 避免主词典/细胞词库/云词典各拼一遍同样的字符串
	void get_phrases(
		const coral::phonetics::PhoneticSequence& pinyin_sequence,
		const coral::phonetics::support::KeyProbe& queryKey,
		coral::phonetics::ParsePhrases& phrases,
		const int limit,
		bool isMaxPinyinSequence);

	void collect_repaired_candidates(
		const coral::phonetics::ParseTextViews& pinyin_sequence,
		coral::phonetics::ParsePhrases& phrases,
		int limit,
		bool isMixedPinyin);

	void get_combined_phrases(
		const coral::phonetics::PhoneticSequence& pinyin_sequence,
		const coral::phonetics::support::KeyProbe& queryKey,
		coral::phonetics::ParsePhrases& phrases,
		int limit);

	bool predictive_search(marisa::Agent& agent, const string& prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases) override;
	// mainlexicon.coral 路径的前缀扩展查询，枚举顺序与 marisa predictive_search 逐位一致（已对拍验证）
	void predictive_search_bin(std::string_view prefixKey, const string& prefixValue, coral::phonetics::ParsePhrases& phrases);
	bool load(const coral::phonetics::ResourceLocation& trieFile, const coral::phonetics::ResourceLocation& infoFile, const string& versionId) override;
	bool build_lexicon(
		const std::filesystem::path& rawDataPath,
		const std::filesystem::path& triePath,
		const std::filesystem::path& dataPath) override;
	bool compile_lexicon_directory(const std::filesystem::path& dirPath);
	bool update_combined_lexicon();
	void reset_storage() override;
	bool build_lexicon_for_cn(
		const std::filesystem::path &rawDataPath,
		const std::filesystem::path &triePath,
		const std::filesystem::path &dataPath);
	const string& get_lexicon_name() const
	{
		return name_;
	}
	void set_is_corrected(bool is_corrected);
	void set_enable_in_m2(bool enabled);

	// 词库导入注音用
	bool is_loaded() const { return loaded_; }
	// 遍历所有单字条目（字, 全拼, 权重）；同一字的不同读音各回调一次
	void for_each_single_char_entry(const std::function<void(std::string_view, std::string_view, float)>& visit) const;
	// key（全拼，如 "chong'qing"）下是否收录 word，命中时带出权重
	bool find_phrase(std::string_view key, std::string_view word, float& weight) const;
	uint32_t find_context_id(std::string_view context) const;
    void collect_continuations(std::string_view context, std::string_view pinyin,
        std::vector<coral::phonetics::LexiconPhrase>& phrases) const;
    bool find_english_word(std::string_view word, coral::phonetics::LexiconPhrase& info) const;

private:
	std::string name_;
    std::vector<uint32_t> text_index_;
	// 主词典唯一查询路径：扁平二分布局（mainlexicon.coral），load 时缺失则从 marisa+dat 现场生成
	coral::phonetics::LexiconTable primary_bin_;
	// 英文拼写与拼音别名共享同一主词条/模型标识。
	using EnglishWords = std::unordered_map<std::string, std::pair<uint32_t, float>>;
	std::shared_ptr<const EnglishWords> english_words_;
	coral::storage::VerifiedResource bin_mapper_;
	coral::storage::PhraseArchiveView combined_data_view_;
	coral::storage::VerifiedResource combined_trie_mapper_;
	marisa::Trie combined_trie_;
	// 细胞词库（combined）仍走 marisa：独立数据文件，与主词典路径无关
	marisa::Agent combined_agent_;
	coral::phonetics::support::KeyFilter combined_key_filter_;
	int combined_lexicon_version_{ -1 };
	bool is_combined_loaded_{ false };
	bool is_corrected_{ false };
	bool enable_in_m2_{ true };
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // PRIMARYLEXICON_H

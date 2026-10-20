#pragma once
#ifndef CORAL_MODULES_PHONETICS_LEXICON_IMPORTER_H_INCLUDED
#define CORAL_MODULES_PHONETICS_LEXICON_IMPORTER_H_INCLUDED

#include "coral/vocabulary/lexicon_import.h"
#include "../learning/interaction_history_types.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/*
 * 用户词库导入（预览 + 写入）。
 * 自动注音不引入额外数据：单字读音取自主词典的单字条目（按词频排序），
 * 多字词先用读音组合回查主词典确认整词读音，查不到时按主词典词语切分，
 * 剩余单字取最常用读音，并对读音接近的多音字打标供用户检查。
 */
namespace coral::phonetics {
class LexiconImporter
{
public:
	static LexiconImporter& instance();

	// 词库尚未装载完成时返回 false
	bool prepare(std::vector<UserLexiconImportItem>& items, const UserLexiconImportOptions& options);
	UserLexiconImportReport commit(const std::vector<UserLexiconImportItem>& items);
	bool revert_last();

private:
	struct Reading
	{
		std::string pinyin;
		float weight;
	};

	LexiconImporter() = default;
	bool ensure_readings();
	void prepare_item(UserLexiconImportItem& item, const UserLexiconImportOptions& options) const;
	bool annotate(const std::vector<std::string>& chars, std::string& pinyin, bool& polyphonic, std::string& missing) const;
	bool dictionary_pinyin(const std::vector<std::string>& chars, size_t from, size_t to, size_t comboLimit,
		std::vector<std::string>& out) const;
	bool is_ambiguous(const std::string& ch) const;
	bool normalize_pinyin(const std::string& raw, size_t count, std::vector<std::string>& syllables) const;
	bool split_syllables(const std::string& text, size_t count, std::vector<std::string>& out) const;
	std::string canonical_syllable(const std::string& token) const;

	std::mutex mutex_;
	bool readings_ready_ = false;
	std::unordered_map<std::string, std::vector<Reading>> readings_; // 字 → 全拼读音（权重降序）
	std::vector<coral::history::EntryIdentity> last_import_;
};
} // 命名空间 coral::phonetics 结束（批次 2）


#endif // USERLEXICONIMPORTER_H

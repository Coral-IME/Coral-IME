#pragma once
#ifndef CORAL_INCLUDE_CORAL_VOCABULARY_LEXICON_IMPORT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_VOCABULARY_LEXICON_IMPORT_H_INCLUDED

#include "coral/vocabulary/lexicon_types.h"

#include <string>
#include <utility>
#include <vector>

// 用户词库导入：设置界面负责读文件、识别编码/分隔符/字段，引擎负责
// 规范化拼音、自动注音、查重与写入。字符串均为 UTF-8。

enum class UserLexiconImportStatus : int {
	Ready = 0,     // 可导入
	Exists = 1,    // 用户词库已有，跳过
	Repeated = 2,  // 与文件中前面的词条重复，跳过
	Invalid = 3,   // 异常，无法导入
};

enum class UserLexiconPinyinSource : int {
	None = 0,
	File = 1,       // 使用文件中的拼音
	Generated = 2,  // 由主词典自动生成
};

struct UserLexiconImportItem {
	int line = 0;               // 源文件行号（从 1 开始）
	std::string phrase;
	std::string rawPinyin;      // 文件中的拼音原文；自有 CSV 为 FullPinyin 字段
	std::string rawLookupKey;   // 仅自有 CSV：Pinyin 字段（可能是简拼），非空即按原字段写入
	coral::LanguageVariant charset = coral::LanguageVariant::Simplified;

	// 以下由 prepare_user_import 填写
	std::string pinyin;         // 规范化全拼，如 chong'qing
	std::string lookupKey;      // 写入用户词库的查询键
	UserLexiconImportStatus status = UserLexiconImportStatus::Ready;
	UserLexiconPinyinSource source = UserLexiconPinyinSource::None;
	bool polyphonic = false;    // 自动生成的拼音里有词典无法确认的多音字
	std::string message;        // 异常原因
};

struct UserLexiconImportOptions {
	bool autoPinyin = true;     // 没有拼音时自动生成
	bool skipExisting = true;   // 跳过用户词库中已有的词条
};

struct UserLexiconImportReport {
	int imported = 0;           // 写入的词条数（不含自动补充的简拼索引）
	int skipped = 0;            // 已存在 + 文件内重复
	int failed = 0;             // 异常 + 写入失败
	std::vector<std::pair<int, std::string>> failures; // 写入阶段失败的 {行号, 原因}
};

#endif // USERLEXICONIMPORT_H

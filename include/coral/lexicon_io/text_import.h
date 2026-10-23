#pragma once
#ifndef CORAL_INCLUDE_CORAL_LEXICON_IO_TEXT_IMPORT_H_INCLUDED
#define CORAL_INCLUDE_CORAL_LEXICON_IO_TEXT_IMPORT_H_INCLUDED

#include "coral/vocabulary/lexicon_import.h"

#include <string>
#include <vector>

// 读取用户提供的文本词库：识别编码、分隔符与字段，产出待引擎校验的词条。
namespace coral::lexicon_io
{
enum class ImportEncoding { kUtf8, kUtf16Le, kUtf16Be, kGb18030 };
enum class ImportLayout { kWords, kWordsAndPinyin, kCoralCsv, kMigrationTsv };

struct ImportedText
{
    std::wstring path;
    std::wstring file_name;
    ImportEncoding encoding = ImportEncoding::kUtf8;
    ImportLayout format = ImportLayout::kWords;
    wchar_t delimiter = 0;             // 0：整行即词语
    std::vector<UserLexiconImportItem> items;
    std::vector<std::wstring> lines;   // 与 items 一一对应的原始行，供预览展示
    std::wstring parse_error;          // 命名列文本校验失败时不提交部分词条
};

// charset：纯文本词库写入的字符集；自有 CSV 保留 Charset 字段。失败时 error 可直接展示。
bool read_import_file(const std::wstring& path, coral::LanguageVariant charset, ImportedText& out, std::wstring& error);
// 已解码的文本（测试与拖放复用）
void parse_import_text(const std::wstring& text, coral::LanguageVariant charset, ImportedText& out);

std::wstring encoding_label(ImportEncoding encoding);
std::wstring layout_label(ImportLayout format);
std::wstring delimiter_label(wchar_t delimiter);
}

#endif // CORAL_INCLUDE_CORAL_LEXICON_IO_TEXT_IMPORT_H_INCLUDED

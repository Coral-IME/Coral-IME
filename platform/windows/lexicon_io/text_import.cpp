#include "coral/lexicon_io/text_import.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <cstdint>
#include <iterator>
#include <utility>

namespace coral::lexicon_io
{
namespace
{
    constexpr std::uintmax_t kMaximumTextBytes = 64ull * 1024 * 1024;
    constexpr size_t kDelimiterSampleLines = 500;
    constexpr UINT kGb18030CodePage = 54936;
    const wchar_t kExportHeader[] = L"phrase,pinyin,fullpinyin,charset";
    const wchar_t kDelimiters[] = { L'\t', L',', L'\uFF0C', L'|', L' ' };

    bool is_import_space(wchar_t c)
    {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\u3000' || c == L'\uFEFF' || c == L'\u00A0';
    }

    std::wstring trim_field(const std::wstring& text)
    {
        size_t begin = 0, end = text.size();
        while (begin < end && is_import_space(text[begin])) ++begin;
        while (end > begin && is_import_space(text[end - 1])) --end;
        return text.substr(begin, end - begin);
    }

    std::wstring unquote_field(const std::wstring& text)
    {
        std::wstring value = trim_field(text);
        if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
            value = value.substr(1, value.size() - 2);
        return trim_field(value);
    }

    std::wstring lower_ascii(std::wstring text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
            return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
        });
        return text;
    }

    std::string encode_utf8(const std::wstring& text)
    {
        if (text.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>((std::max)(size, 0)), '\0');
        if (size > 0)
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    bool decode_code_page(const std::string& bytes, UINT code_page, std::wstring& out)
    {
        if (bytes.empty()) { out.clear(); return true; }
        const int size = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (size <= 0) return false;
        out.assign(static_cast<size_t>(size), L'\0');
        return MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), out.data(), size) == size;
    }

    std::wstring decode_utf16(const std::string& bytes, size_t offset, bool big_endian)
    {
        std::wstring out;
        out.reserve((bytes.size() - offset) / 2);
        for (size_t i = offset; i + 1 < bytes.size(); i += 2)
        {
            const auto lo = static_cast<unsigned char>(bytes[i + (big_endian ? 1 : 0)]);
            const auto hi = static_cast<unsigned char>(bytes[i + (big_endian ? 0 : 1)]);
            out.push_back(static_cast<wchar_t>(lo | (hi << 8)));
        }
        return out;
    }

    bool decode_import_text(const std::string& bytes, std::wstring& text, ImportEncoding& encoding)
    {
        const auto at = [&](size_t i) { return static_cast<unsigned char>(bytes[i]); };
        if (bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF)
        {
            encoding = ImportEncoding::kUtf8;
            return decode_code_page(bytes.substr(3), CP_UTF8, text);
        }
        if (bytes.size() >= 2 && at(0) == 0xFF && at(1) == 0xFE)
        {
            encoding = ImportEncoding::kUtf16Le;
            text = decode_utf16(bytes, 2, false);
            return true;
        }
        if (bytes.size() >= 2 && at(0) == 0xFE && at(1) == 0xFF)
        {
            encoding = ImportEncoding::kUtf16Be;
            text = decode_utf16(bytes, 2, true);
            return true;
        }
        // 无 BOM 的 UTF-16：换行等 ASCII 字符的高字节为 0，而 UTF-8/GBK 文本不含 0 字节
        size_t even_zero = 0, odd_zero = 0;
        const size_t probe = (std::min)(bytes.size(), static_cast<size_t>(4096));
        for (size_t i = 0; i < probe; ++i)
            if (bytes[i] == 0) ++((i % 2) ? odd_zero : even_zero);
        if (probe >= 4 && (std::max)(odd_zero, even_zero) >= (std::max)(static_cast<size_t>(2), probe / 16))
        {
            encoding = odd_zero >= even_zero ? ImportEncoding::kUtf16Le : ImportEncoding::kUtf16Be;
            text = decode_utf16(bytes, 0, encoding == ImportEncoding::kUtf16Be);
            return true;
        }
        if (decode_code_page(bytes, CP_UTF8, text))
        {
            encoding = ImportEncoding::kUtf8;
            return true;
        }
        encoding = ImportEncoding::kGb18030;
        return decode_code_page(bytes, kGb18030CodePage, text);
    }

    bool is_han(wchar_t c)
    {
        return c == L'\u3007' || (c >= 0x3400 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) ||
            (c >= 0xD840 && c <= 0xD8BF); // 扩展 B 起的高代理
    }

    bool contains_han(const std::wstring& text)
    {
        return std::any_of(text.begin(), text.end(), is_han);
    }

    bool resembles_pinyin(const std::wstring& text)
    {
        static const std::wstring kToneLetters = L"āáǎàēéěèêīíǐìōóǒòūúǔùǖǘǚǜüÜńňǹḿĀÁǍÀĒÉĚÈĪÍǏÌŌÓǑÒŪÚǓÙ";
        bool letter = false;
        for (wchar_t c : text)
        {
            if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || kToneLetters.find(c) != std::wstring::npos)
                letter = true;
            else if (!(c == L'\'' || c == L'\u2019' || c == L'\u2018' || c == L':' || c == L'-' || c == L' ' ||
                (c >= L'0' && c <= L'9')))
                return false;
        }
        return letter;
    }

    std::vector<std::wstring> split_fields(const std::wstring& line, wchar_t delimiter)
    {
        std::vector<std::wstring> fields;
        if (delimiter == 0)
        {
            fields.push_back(trim_field(line));
            return fields;
        }
        std::wstring field;
        bool quoted = false;
        for (wchar_t c : line)
        {
            if (c == L'"' && delimiter == L',')
                quoted = !quoted;
            if (c == delimiter && !quoted)
            {
                fields.push_back(unquote_field(field));
                field.clear();
            }
            else
            {
                field.push_back(c);
            }
        }
        fields.push_back(unquote_field(field));
        if (delimiter == L' ')
            fields.erase(std::remove_if(fields.begin(), fields.end(), [](const std::wstring& f) { return f.empty(); }), fields.end());
        return fields;
    }

    wchar_t detect_delimiter(const std::vector<std::wstring>& lines, size_t first)
    {
        size_t samples = 0;
        size_t counts[std::size(kDelimiters)] = {};
        for (size_t i = first; i < lines.size() && samples < kDelimiterSampleLines; ++i)
        {
            if (lines[i].empty()) continue;
            ++samples;
            for (size_t d = 0; d < std::size(kDelimiters); ++d)
                if (lines[i].find(kDelimiters[d]) != std::wstring::npos) ++counts[d];
        }
        for (size_t d = 0; d < std::size(kDelimiters); ++d)
            if (samples > 0 && counts[d] * 2 >= samples) return kDelimiters[d];
        return 0;
    }

    bool is_comment(const std::wstring& line)
    {
        return line[0] == L'#' || line[0] == L';' || line.compare(0, 2, L"//") == 0;
    }
}

void parse_import_text(const std::wstring& text, coral::LanguageVariant charset, ImportedText& out)
{
    out.items.clear();
    out.lines.clear();
    out.delimiter = 0;
    out.format = ImportLayout::kWords;

    // 保留行号：空行、注释行用空串占位
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = trim_field(text.substr(start, end - start));
        if (!line.empty() && is_comment(line)) line.clear();
        lines.push_back(std::move(line));
        start = end + 1;
    }
    // Rime 词典 YAML 头：--- ... 之间不是词条
    size_t first = 0;
    while (first < lines.size() && lines[first].empty()) ++first;
    if (first < lines.size() && lines[first] == L"---")
    {
        size_t close = first + 1;
        while (close < lines.size() && lines[close] != L"...") ++close;
        if (close < lines.size())
        {
            for (size_t i = first; i <= close; ++i) lines[i].clear();
            first = close + 1;
        }
    }
    while (first < lines.size() && lines[first].empty()) ++first;
    if (first >= lines.size())
        return;

    if (lower_ascii(lines[first]).compare(0, std::size(kExportHeader) - 1, kExportHeader) == 0)
    {
        out.format = ImportLayout::kCoralCsv;
        out.delimiter = L',';
        for (size_t i = first + 1; i < lines.size(); ++i)
        {
            if (lines[i].empty()) continue;
            const auto fields = split_fields(lines[i], L',');
            UserLexiconImportItem item;
            item.line = static_cast<int>(i + 1);
            item.phrase = encode_utf8(fields[0]);
            if (fields.size() > 1) item.rawLookupKey = encode_utf8(fields[1]);
            if (fields.size() > 2) item.rawPinyin = encode_utf8(fields[2]);
            const wchar_t flag = fields.size() > 3 && !fields[3].empty() ? fields[3][0] : 0;
            item.charset = flag == L'T' || flag == L't' ? coral::LanguageVariant::Traditional
                : flag == L'S' || flag == L's' ? coral::LanguageVariant::Simplified : charset;
            out.items.push_back(std::move(item));
            out.lines.push_back(lines[i]);
        }
        return;
    }

    out.delimiter = detect_delimiter(lines, first);
    size_t with_pinyin = 0;
    for (size_t i = first; i < lines.size(); ++i)
    {
        if (lines[i].empty()) continue;
        // 个别行混用了其他分隔符（如全角逗号）时按该行实际的分隔符拆分
        wchar_t delimiter = out.delimiter;
        if (delimiter == 0 || lines[i].find(delimiter) == std::wstring::npos)
        {
            const auto other = std::find_if(std::begin(kDelimiters), std::end(kDelimiters),
                [&](wchar_t d) { return lines[i].find(d) != std::wstring::npos; });
            delimiter = other != std::end(kDelimiters) ? *other : 0;
        }
        const auto fields = split_fields(lines[i], delimiter);
        std::wstring phrase, pinyin;
        bool phrase_found = false;
        for (size_t f = 0; f < fields.size(); ++f)
        {
            if (!phrase_found && contains_han(fields[f]))
            {
                phrase = fields[f];
                phrase_found = true;
            }
            else if (pinyin.empty() && resembles_pinyin(fields[f]) && !std::all_of(fields[f].begin(), fields[f].end(), iswdigit))
            {
                pinyin = fields[f];
                // 空格分隔时拼音本身也会被拆开（如 "重庆 chong qing 100"）
                while (delimiter == L' ' && f + 1 < fields.size() && resembles_pinyin(fields[f + 1]) &&
                    !std::all_of(fields[f + 1].begin(), fields[f + 1].end(), iswdigit))
                    pinyin += L' ' + fields[++f];
            }
        }
        UserLexiconImportItem item;
        item.line = static_cast<int>(i + 1);
        item.phrase = encode_utf8(phrase);
        item.rawPinyin = encode_utf8(pinyin);
        item.charset = charset;
        if (!pinyin.empty()) ++with_pinyin;
        out.items.push_back(std::move(item));
        out.lines.push_back(lines[i]);
    }
    if (with_pinyin * 2 >= out.items.size() && with_pinyin > 0)
        out.format = ImportLayout::kWordsAndPinyin;
}

bool read_import_file(const std::wstring& path, coral::LanguageVariant charset, ImportedText& out, std::wstring& error)
{
    out = ImportedText{};
    out.path = path;
    const size_t slash = path.find_last_of(L"\\/");
    out.file_name = slash == std::wstring::npos ? path : path.substr(slash + 1);

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes) ||
        (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        error = L"无法打开文件";
        return false;
    }
    const std::uintmax_t size = (static_cast<std::uintmax_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
    if (size > kMaximumTextBytes)
    {
        error = L"文件过大（超过 64 MB），请拆分后再导入";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = L"无法打开文件，可能正被其他程序占用";
        return false;
    }
    std::string bytes(static_cast<size_t>(size), '\0');
    if (size > 0 && !input.read(bytes.data(), static_cast<std::streamsize>(size)))
    {
        error = L"读取文件失败";
        return false;
    }
    std::wstring text;
    if (!decode_import_text(bytes, text, out.encoding))
    {
        error = L"无法识别文件编码，请另存为 UTF-8 后重试";
        return false;
    }
    parse_import_text(text, charset, out);
    if (out.items.empty())
    {
        error = L"文件中没有可识别的词条";
        return false;
    }
    return true;
}

std::wstring encoding_label(ImportEncoding encoding)
{
    switch (encoding)
    {
    case ImportEncoding::kUtf16Le: return L"UTF-16 LE";
    case ImportEncoding::kUtf16Be: return L"UTF-16 BE";
    case ImportEncoding::kGb18030: return L"GBK/GB18030";
    default: return L"UTF-8";
    }
}

std::wstring layout_label(ImportLayout format)
{
    switch (format)
    {
    case ImportLayout::kWordsAndPinyin: return L"词语 + 拼音";
    case ImportLayout::kCoralCsv: return L"本输入法导出的 CSV";
    default: return L"纯词语";
    }
}

std::wstring delimiter_label(wchar_t delimiter)
{
    switch (delimiter)
    {
    case L'\t': return L"Tab 分隔";
    case L',': return L"逗号分隔";
    case L'\uFF0C': return L"中文逗号分隔";
    case L'|': return L"竖线分隔";
    case L' ': return L"空格分隔";
    default: return L"每行一个词";
    }
}
}

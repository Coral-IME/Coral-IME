#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_PHRASECODE_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_PHRASECODE_H_INCLUDED

#include <cwctype>
#include <string>

// 常用语输入码规则：默认取内容开头前 3 个汉字的拼音首字母，
// 或开头英文单词的前 3 个字母；自定义输入码只能由字母组成。
namespace phrase_code
{
constexpr std::size_t kSeedLength = 3;
constexpr std::size_t kMaxCodeLength = 24;

inline bool IsHan(wchar_t c)
{
	return (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0xF900 && c <= 0xFAFF);
}

inline bool IsLatin(wchar_t c)
{
	return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// 生成默认输入码所依据的片段：二者至多一个非空。
struct Seed
{
	std::wstring han;
	std::wstring latin;
};

// 跳过开头的数字、标点和空白，从第一个汉字或字母起截取。
inline Seed ExtractSeed(const std::wstring& content)
{
	Seed seed;
	std::size_t i = 0;
	while (i < content.size() && !IsHan(content[i]) && !IsLatin(content[i])) ++i;
	if (i == content.size()) return seed;
	if (IsHan(content[i]))
	{
		for (; i < content.size() && IsHan(content[i]) && seed.han.size() < kSeedLength; ++i)
			seed.han += content[i];
		return seed;
	}
	for (; i < content.size() && IsLatin(content[i]) && seed.latin.size() < kSeedLength; ++i)
		seed.latin += static_cast<wchar_t>(std::towlower(content[i]));
	return seed;
}

// "da'bu'liao" -> "dbl"（取各音节首字母）
inline std::wstring InitialsFromPinyin(const std::string& pinyin)
{
	std::wstring initials;
	bool start = true;
	for (const char c : pinyin)
	{
		if (c == '\'' || c == ' ') { start = true; continue; }
		if (start && c >= 'a' && c <= 'z') initials += static_cast<wchar_t>(c);
		start = false;
	}
	return initials;
}

// 去掉首尾空白并转小写；含非字母字符时返回空串。
inline std::wstring NormalizeCode(const std::wstring& input)
{
	const std::size_t first = input.find_first_not_of(L" \t\u3000");
	if (first == std::wstring::npos) return {};
	const std::size_t last = input.find_last_not_of(L" \t\u3000");
	std::wstring code;
	for (std::size_t i = first; i <= last; ++i)
	{
		if (!IsLatin(input[i])) return {};
		code += static_cast<wchar_t>(std::towlower(input[i]));
	}
	return code;
}

// u、v 开头会进入 U/V 模式，不会作为拼音查询。
inline bool StartsSpecialMode(const std::wstring& code)
{
	return !code.empty() && (code[0] == L'u' || code[0] == L'v');
}
} // 命名空间 phrase_code 结束

#endif // PHRASECODE_H

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// 剪贴板历史：coralpinyin.exe 监听系统剪贴板并独占写入，TSF 只读。
// 文件为 UTF-8 文本，首行是格式标记，之后每行一条 "<秒级时间戳>\t<转义文本>"，最新的在前。
namespace coral::clipboard
{
inline constexpr std::size_t kMaxEntries = 100;
inline constexpr std::size_t kMaxTextLength = 20000;
inline constexpr std::int64_t kRetentionSeconds = 90LL * 24 * 60 * 60;
inline constexpr char kFileMagic[] = "CORALCLIP1";

struct Entry
{
	std::int64_t time = 0;
	std::wstring text;
};

inline std::filesystem::path HistoryPath(const std::filesystem::path& userDir)
{
	return userDir / "clipboard" / "history.txt";
}

inline std::string ToUtf8(const std::wstring& value)
{
	if (value.empty()) return {};
	const int length = ::WideCharToMultiByte(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
	if (length <= 0) return {};
	std::string result(static_cast<std::size_t>(length), '\0');
	::WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		result.data(), length, nullptr, nullptr);
	return result;
}

inline std::wstring FromUtf8(const std::string& value)
{
	if (value.empty()) return {};
	const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<std::size_t>(length), L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		result.data(), length);
	return result;
}

inline std::string Escape(const std::string& value)
{
	std::string result;
	result.reserve(value.size());
	for (const char c : value)
	{
		switch (c)
		{
		case '\\': result += "\\\\"; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default: result += c; break;
		}
	}
	return result;
}

inline std::string Unescape(const std::string& value)
{
	std::string result;
	result.reserve(value.size());
	for (std::size_t i = 0; i < value.size(); ++i)
	{
		if (value[i] != '\\' || i + 1 == value.size())
		{
			result += value[i];
			continue;
		}
		switch (value[++i])
		{
		case 'n': result += '\n'; break;
		case 'r': result += '\r'; break;
		case 't': result += '\t'; break;
		default: result += value[i]; break;
		}
	}
	return result;
}

inline bool IsBlank(const std::wstring& text)
{
	return std::all_of(text.begin(), text.end(), [](wchar_t c) {
		return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000 || c == 0xA0;
	});
}

inline std::vector<Entry> Parse(const std::string& content, std::int64_t now)
{
	std::vector<Entry> entries;
	std::size_t position = 0;
	bool header = true;
	while (position < content.size() && entries.size() < kMaxEntries)
	{
		std::size_t end = content.find('\n', position);
		if (end == std::string::npos) end = content.size();
		std::string line = content.substr(position, end - position);
		position = end + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (header)
		{
			header = false;
			if (line != kFileMagic) return {};
			continue;
		}
		const std::size_t tab = line.find('\t');
		if (tab == std::string::npos || tab == 0) continue;
		Entry entry;
		try { entry.time = std::stoll(line.substr(0, tab)); }
		catch (...) { continue; }
		if (now > 0 && now - entry.time > kRetentionSeconds) continue;
		entry.text = FromUtf8(Unescape(line.substr(tab + 1)));
		if (!entry.text.empty()) entries.push_back(std::move(entry));
	}
	return entries;
}

inline std::string Serialize(const std::vector<Entry>& entries)
{
	std::string content = kFileMagic;
	content += '\n';
	for (const Entry& entry : entries)
	{
		content += std::to_string(entry.time);
		content += '\t';
		content += Escape(ToUtf8(entry.text));
		content += '\n';
	}
	return content;
}

// 读端允许写端同时以替换方式更新文件。
inline std::vector<Entry> Load(const std::filesystem::path& path, std::int64_t now)
{
	HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return {};
	std::string content;
	LARGE_INTEGER size{};
	if (::GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 64LL * 1024 * 1024)
	{
		content.resize(static_cast<std::size_t>(size.QuadPart));
		DWORD read = 0;
		if (!::ReadFile(file, content.data(), static_cast<DWORD>(content.size()), &read, nullptr))
			read = 0;
		content.resize(read);
	}
	::CloseHandle(file);
	return Parse(content, now);
}

inline bool Save(const std::filesystem::path& path, const std::vector<Entry>& entries)
{
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	const std::filesystem::path temporary = path.wstring() + L".tmp";
	HANDLE file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	const std::string content = Serialize(entries);
	DWORD written = 0;
	const bool ok = ::WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written,
		nullptr) && written == content.size();
	::CloseHandle(file);
	if (!ok) return false;
	for (int attempt = 0; attempt < 5; ++attempt)
	{
		if (::MoveFileExW(temporary.c_str(), path.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			return true;
		::Sleep(10);
	}
	return false;
}

// 记录一条新复制的文本；重复内容移到最前。返回 false 表示该文本不入库。
inline bool Remember(std::vector<Entry>& entries, std::wstring text, std::int64_t now)
{
	if (text.empty() || text.size() > kMaxTextLength || IsBlank(text)) return false;
	entries.erase(std::remove_if(entries.begin(), entries.end(),
		[&](const Entry& entry) { return entry.text == text; }), entries.end());
	entries.insert(entries.begin(), Entry{ now, std::move(text) });
	if (entries.size() > kMaxEntries) entries.resize(kMaxEntries);
	return true;
}

// 单行展示：换行、制表符折叠为一个空格，超长截断（省略号由绘制端按宽度追加）。
inline std::wstring DisplayLine(const std::wstring& text, std::size_t maxChars = 200)
{
	std::wstring line;
	line.reserve((std::min)(text.size(), maxChars));
	bool space = false;
	for (const wchar_t c : text)
	{
		if (line.size() >= maxChars) break;
		if (c == L'\r' || c == L'\n' || c == L'\t')
		{
			space = !line.empty();
			continue;
		}
		if (space) { line += L' '; space = false; }
		line += c;
	}
	return line;
}
} // 命名空间 coral::clipboard 结束

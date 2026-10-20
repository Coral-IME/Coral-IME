#include "coral/product/product_identity.h"

/*
* @author: huangzhishou
* @date: 2021-6-9 10:27:54
*/
#pragma once
#ifndef CORAL_MODULES_FOUNDATION_TEXT_FILE_HELPERS_HPP_INCLUDED
#define CORAL_MODULES_FOUNDATION_TEXT_FILE_HELPERS_HPP_INCLUDED

#include <algorithm>
#include <time.h>
#include <string>
#include <iostream>
#include <vector>
#include <filesystem>
#include <sstream>
#include <fstream>
#include "trace_log.h"
#include "test_environment.h"
#include "ini_store.h"
#include "preference_registry.h"
#include "thirdparty/hash-library/md5.h"

#if defined (__linux__) || defined(__ANDROID__)|| defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#include <utf8conv.h>
#elif defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <atlpath.h>
#include <shlobj.h>
#include <sddl.h>
#endif

#if defined _WIN32 || _WIN64
#define LOCALTIME_R(a, b) localtime_s(b, a)
#else
#define LOCALTIME_R(a, b) localtime_r(a, b)
#endif

namespace hlp {
	using namespace std;

	enum class UWPRunMode {
		NotUWP = 0,            // 非UWP环境
		ServiceAvailable = 1,  // UWP环境，服务可用（使用Open模式）
		Standalone = 2         // UWP环境，服务不可用（回退到自建共享内存模式）
	};

#if defined(_WIN32) || defined(_MSC_VER)
	inline BOOL isAppContainer()
	{
		static const BOOL isAppContainer = []() -> BOOL {
			BOOL isAppContainerResult = FALSE;
			HANDLE hToken = NULL;
			DWORD cb = 0;
			if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
				::GetTokenInformation(hToken, TokenIsAppContainer, &isAppContainerResult, sizeof(isAppContainerResult), &cb);
				::CloseHandle(hToken);
			}
			return isAppContainerResult;
		}();
		return isAppContainer;
	}

	inline UWPRunMode getUWPRunMode()
	{
		static UWPRunMode mode = []() -> UWPRunMode {
			if (!isAppContainer()) {
				return UWPRunMode::NotUWP;
			}
			SC_HANDLE hSCM = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
			if (!hSCM) {
				return UWPRunMode::Standalone;
			}
			SC_HANDLE hService = OpenServiceW(hSCM, coral::product::kServiceNameWide, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
			if (!hService) {
				CloseServiceHandle(hSCM);
				return UWPRunMode::Standalone;
			}
			DWORD dwBytesNeeded = 0;
			if (QueryServiceConfigW(hService, nullptr, 0, &dwBytesNeeded)) {
				std::vector<BYTE> buffer(dwBytesNeeded);
				LPQUERY_SERVICE_CONFIGW pServiceConfig = reinterpret_cast<LPQUERY_SERVICE_CONFIGW>(buffer.data());

				if (QueryServiceConfigW(hService, pServiceConfig, dwBytesNeeded, &dwBytesNeeded)) {
					if (pServiceConfig->dwStartType == SERVICE_DISABLED) {
						CloseServiceHandle(hService);
						CloseServiceHandle(hSCM);
						return UWPRunMode::Standalone;
					}
				}
				else {
					CloseServiceHandle(hService);
					CloseServiceHandle(hSCM);
					return UWPRunMode::Standalone;
				}
				SERVICE_STATUS status{};
				if (!QueryServiceStatus(hService, &status)) {
					CloseServiceHandle(hService);
					CloseServiceHandle(hSCM);
					return UWPRunMode::Standalone;
				}
			}

			CloseServiceHandle(hService);
			CloseServiceHandle(hSCM);
			return UWPRunMode::ServiceAvailable;
		}();
		
		return mode;
	}

	inline bool shouldAllowCreateSharedMemory()
	{
		UWPRunMode mode = getUWPRunMode();
		return mode == UWPRunMode::NotUWP || mode == UWPRunMode::Standalone;
	}

	inline bool shouldOnlyOpenSharedMemory()
	{
		return getUWPRunMode() == UWPRunMode::ServiceAvailable;
	}

#elif defined(__ANDROID__) || defined(__linux__) || defined(__APPLE__)
	inline bool shouldAllowCreateSharedMemory()
	{
		return true;
	}

	inline bool shouldOnlyOpenSharedMemory()
	{
		return false;
	}

	inline UWPRunMode getUWPRunMode()
	{
		return UWPRunMode::NotUWP;
	}

#endif

	template<typename ... Args>
	static string formatString(const char* szFormat, Args ... args)
	{
		const int max_buf = 2048;
		char msg[max_buf + 1] = { 0 };
		snprintf(msg, max_buf, szFormat, args...);
		return string(msg);
	}

	inline std::string getExePath()
	{
#if defined(_WIN32) || defined(_WIN64)
		CHAR chPath[MAX_PATH] = {0};
		DWORD dwRet = ::GetModuleFileNameA(NULL, chPath, MAX_PATH);
		return dwRet > 0 ? std::string(chPath, dwRet) : "";
#elif defined(__linux__) || defined(__APPLE__)
		char buf[PATH_MAX] = {0};
		ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
		return len != -1 ? std::string(buf, len) : "";
#endif
	}

#if defined(_WIN32) || defined(_MSC_VER)
	inline LPSECURITY_ATTRIBUTES NtLPC_GetDefaultSecurity()
    {
        static PSECURITY_DESCRIPTOR pSD = nullptr;
        static SECURITY_ATTRIBUTES sa = {};

        if (pSD == nullptr)
        {
            LPCWSTR szSDDL = L"S:(ML;;;;;LW)D:(A;;GA;;;WD)(A;;GA;;;AC)";
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                szSDDL,
                SDDL_REVISION_1,
                &pSD,
                nullptr))
            {
                return nullptr;
            }

            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.lpSecurityDescriptor = pSD;
            sa.bInheritHandle = FALSE;
        }
        return &sa;
    }

	
#endif

	inline std::string buildSHName(const std::string& version,
								 const std::string& filename) {
		std::string path_str = "CORALPY";
		std::stringstream ss;
		ss << path_str << "_" << version << "_" << filename;
		const std::string core_name = coral::testing::objectName(ss.str());
		return core_name;
	}
#if defined(_WIN32) || defined(_MSC_VER)
	inline bool ShmExistsByName(const std::string& name) {
		HANDLE h = OpenFileMappingW(FILE_MAP_READ, FALSE, std::wstring(name.begin(), name.end()).c_str());
		if (h) { CloseHandle(h); return true; }
		return false;
	}
#elif defined(__ANDROID__) || defined(__linux__)//安卓的设置不走共享内存
	inline bool ShmExistsByName(const std::string& name) {
		return false;
	}
#elif defined(__APPLE__)
	// macOS: 使用 managed_mapped_file（MMF 文件映射），而非系统共享内存
	// macOS 通过文件系统路径检查 .mmf 文件是否存在，而不是通过内存对象名字
	// 实际检查使用 std::filesystem::exists(path)，此函数不会被调用
	inline bool ShmExistsByName(const std::string& name) {
		return false;
	}
#endif
	inline std::wstring buildSHNameW(const std::string& version,
							 const std::string& filename) {
		auto shName = buildSHName(version, filename);
		return {shName.begin(), shName.end()};
	}

	inline void splitString(const string& str, const string& delim, vector<string>& result)
	{
		string::size_type lastPos = str.find_first_not_of(delim, 0);
		string::size_type pos = str.find_first_of(delim, lastPos);
		while (string::npos != pos || string::npos != lastPos) {
			result.push_back(str.substr(lastPos, pos - lastPos));
			lastPos = str.find_first_not_of(delim, pos);
			pos = str.find_first_of(delim, lastPos);
		}
	}

	// 计算文件的md5值
	inline std::string calculateFileMD5(const std::string& filePath) {
		std::ifstream file(filePath, std::ios::binary);
		if (!file.is_open()) {
			return "";
		}
		MD5 md5;
		constexpr size_t bufferSize = 4096;
		std::vector<char> buffer(bufferSize);
		while (file.good()) {
			// 从文件读取数据到缓冲区
			file.read(buffer.data(), bufferSize);
			// 获取实际读取的字节数
			const std::streamsize bytesRead = file.gcount();
			if (bytesRead > 0) {
				// 将读取到的数据块添加到 MD5 计算中
				md5.add(buffer.data(), static_cast<size_t>(bytesRead));
			}
		}
		file.close();
		return md5.getHash();
	}

	//计算共享内存的md5值
	inline std::string calculateMemoryMD5(const void* memPtr, const size_t size) {
		// 检查输入是否有效
		if (memPtr == nullptr || size == 0) {
			return "";
		}
		MD5 md5;
		return md5(memPtr, size);
	}

	inline std::vector<string_view> splitStringView(const std::string& str, char delimiter) {
		std::vector<string_view> result;
		size_t start = 0;
		size_t end = 0;
		while ((end = str.find(delimiter, start)) != std::string::npos) {
			result.emplace_back(str.data() + start, end - start);
			start = end + 1; 
		}
		if (start < str.size()) {
			result.emplace_back(str.data() + start, str.size() - start);
		}
		return result;
	}

	inline std::vector<string_view> splitStringView(const string_view& str_view, char delimiter) {
		std::vector<string_view> result;
		result.reserve(18);

		size_t start = 0;
		size_t strLen = str_view.size();
		for (size_t end = 0; end < strLen; ++end) 
		{
			if (str_view[end] == delimiter) 
			{
				if (end > start) 
				{
					result.emplace_back(str_view.data() + start, end - start);
				}
				start = end + 1;
			}
		}

		if (start < strLen)
			result.emplace_back(str_view.data() + start, strLen - start);

		return result;
	}

	inline void splitWstring(const wstring& str, const wstring& delim, vector<wstring>& result)
	{
		wstring::size_type lastPos = str.find_first_not_of(delim, 0);
		wstring::size_type pos = str.find_first_of(delim, lastPos);
		while (wstring::npos != pos || wstring::npos != lastPos) {
			result.push_back(str.substr(lastPos, pos - lastPos));
			lastPos = str.find_first_not_of(delim, pos);
			pos = str.find_first_of(delim, lastPos);
		}
	}

	inline bool icaseCompareString(const string& a, const string& b)
	{
		if (a.length() == b.length()) {
			return equal(a.begin(), a.end(), b.begin(),
				[](char a, char b) {
					return tolower(a) == tolower(b);
				});
		}
		else {
			return false;
		}
	}

	inline string& replaceString(string& str, const string_view oldSubstr, const string& newSubStr)
	{
		for (string::size_type pos(0); pos != string::npos; pos += newSubStr.length()) {
			pos = str.find(oldSubstr, pos);
			if (pos != string::npos) {
				str.replace(pos, oldSubstr.length(), newSubStr);
			}
			else {
				break;
			}
		}
		return str;
	}
	
	inline wstring& replaceWString(wstring& str, const wstring& oldSubstr, const wstring& newSubStr)
	{
		for (wstring::size_type pos(0); pos != wstring::npos; pos += newSubStr.length()) {
			pos = str.find(oldSubstr, pos);
			if (pos != wstring::npos) {
				str.replace(pos, oldSubstr.length(), newSubStr);
			}
			else {
				break;
			}
		}
		return str;
	}

	inline string& trimright(string& str, const string& trimedStr = "\r\n\t ")
	{
		auto idx = str.find_last_not_of(trimedStr);
		if (idx != string::npos)
			str.erase(idx + 1);
		return str;
	}

	inline string& trimleft(string& str, const string& trimedStr = "\r\n\t ")
	{
		auto idx = str.find_first_not_of(trimedStr);
		if (idx != string::npos)
			str.erase(0, idx);
		return str;
	}

	inline string& trim(string& str, const string& trimedStr = "\r\n\t ")
	{
		return trimleft(trimright(str, trimedStr), trimedStr);
	}

	inline string& trimFirstLeft(string& str, const string& trimedStr)
	{
		auto idx = str.find(trimedStr);
		if (idx != string::npos)
			str.erase(0, trimedStr.length());
		return str;
	}

	inline wstring& trimright(wstring& str, const wstring& trimedStr = L"\r\n\t ")
	{
		auto idx = str.find_last_not_of(trimedStr);
		if (idx != wstring::npos)
			str.erase(idx + 1);
		return str;
	}

	inline wstring& trimleft(wstring& str, const wstring& trimedStr = L"\r\n\t ")
	{
		auto idx = str.find_first_not_of(trimedStr);
		if (idx != wstring::npos)
			str.erase(0, idx);
		return str;
	}

	inline wstring& trim(wstring& str, const wstring& trimedStr = L"\r\n\t ")
	{
		return trimleft(trimright(str, trimedStr), trimedStr);
	}

	inline string wstr2str(const wstring& wstr)
	{
		if (wstr.empty())
			return "";
#if defined(_WIN32)
		// 使用 Windows API 转换，快速可靠
		int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.size(), nullptr, 0, nullptr, nullptr);
		if (len == 0)
			return "";
		std::string str(len, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), wstr.size(), str.data(), len, nullptr, nullptr);
		return str;
#else
		return wstrToUtf8(wstr);
#endif
	}

	// ANSI/GBK → 宽字符串
	inline wstring ansi2Wstr(const string& ansiStr)
	{
		if (ansiStr.empty())
			return L"";
#if defined(_WIN32)
		int wideLen = MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), 
										  static_cast<int>(ansiStr.size()), nullptr, 0);
		if (wideLen == 0)
			return L"";
		std::wstring wideStr(wideLen, L'\0');
		MultiByteToWideChar(CP_ACP, 0, ansiStr.c_str(), 
							static_cast<int>(ansiStr.size()), wideStr.data(), wideLen);
		return wideStr;
#else
		return utf8ToWstr(ansiStr);
#endif
	}

	inline wstring str2wstr(const string& str)
	{
		if (str.empty())
			return L"";
#if defined(_WIN32)
		// 使用 Windows API 转换，快速可靠
		int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.size(), nullptr, 0);
		if (len == 0)
			return L"";
		std::wstring wstr(len, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), str.size(), wstr.data(), len);
		return wstr;
#else
		return utf8ToWstr(str);
#endif
	}

	[[deprecated("Can't deal with extened utf-8 characters correctly")]]
	inline std::wstring_view str2wstrView(const string_view& str)
	{
		if (str.empty())
			return std::wstring_view();
		thread_local std::wstring buffer = str2wstr(std::string(str));
		return std::wstring_view(buffer);
	}

	inline string stringJoinVec(const string& delimiter, const vector<string>& input) {
		string res;
		for (auto it = input.cbegin(); it != input.cend(); ++it) {
			if (it != input.cbegin()) {
				res.append(delimiter);
			}
			res.append(*it);
		}
		return res;
	}

	inline string stringJoinVec(const char delimiter, const vector<string_view>& input) {
		string res;
		for (auto it = input.cbegin(); it != input.cend(); ++it) {
			if (it != input.cbegin()) {
				res.push_back(delimiter);
			}
			res.append(*it);
		}
		return res;
	}

	inline string stringJoinVec(const char delimiter, const vector<string>& input) {
		string res;
		for (auto it = input.cbegin(); it != input.cend(); ++it) {
			if (it != input.cbegin()) {
				res.push_back(delimiter);
			}
			res.append(*it);
		}
		return res;
	}

	inline string stringJoinVec(const string& delimiter, const vector<string_view>& input) {
		string res;
		for (auto it = input.cbegin(); it != input.cend(); ++it) {
			if (it != input.cbegin()) 
			{
				res.append(delimiter);
			}
			res.append(*it);
		}
		return res;
	}

	inline string stringJoinVec(const string& delimiter, const vector<string>& input, int pos) {
		if (pos >= static_cast<int>(input.size())) {
			return "";
		}
		string res;
		for (vector<string>::const_iterator it = input.begin() + pos; it != input.end(); ++it) {
			if (it != input.begin() + pos) {
				res += delimiter;
			}
			res += *it;
		}
		return res;
	}

	inline string stringJoinVec(const string& delimiter, const vector<string_view>& input, int pos) {
		if (pos >= static_cast<int>(input.size())) {
			return "";
		}
		string res;
		for (auto it = input.begin() + pos; it != input.end(); ++it) {
			if (it != input.begin() + pos) {
				res.append(delimiter);
			}
			res.append(*it);
		}
		return res;
	}

	inline wstring wstringJoinVec(const wstring& delimiter, const vector<wstring>& input) {
		wstring res;
		for (vector<wstring>::const_iterator it = input.begin(); it != input.end(); ++it) {
			if (it != input.begin()) {
				res += delimiter;
			}
			res += *it;
		}
		return res;
	}

	inline int getFileSize(const filesystem::path& fpath)
	{
		try {
			return filesystem::exists(fpath) ? (int)filesystem::file_size(fpath) : 0;
		}
		catch (const filesystem::filesystem_error& e) {
			cout << e.what() << endl;
			return 0;
		}

	}

	inline int getFileMTime(const filesystem::path& fpath)
	{
#ifdef OS_WINDOW
		HANDLE hFile = ::CreateFileW(reinterpret_cast<const WCHAR*>(fpath.u16string().c_str()), FILE_READ_EA, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if (hFile == INVALID_HANDLE_VALUE) {
			return 0;
		}
		FILETIME ftCreate, ftAccess, ftWrite;
		int stamp = 0;
		if (::GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite)) {
			ULARGE_INTEGER t = {};
			t.LowPart = ftWrite.dwLowDateTime;
			t.HighPart = ftWrite.dwHighDateTime;
			stamp = (int)((long long)(t.QuadPart - 116444736000000000) / 10000000);
		}
		::CloseHandle(hFile);
		return stamp;
#else
		struct stat statBuf;
		if (stat(fpath.u8string().c_str(), &statBuf) == 0) {
			return (int)statBuf.st_mtime;
		}
		return 0;
#endif
	}

	inline int getTimeStamp()
	{
		return (int)time(nullptr);
	}

	inline string stamp2string(int t, const string& format = "%Y-%m-%d %H:%M:%S")
	{
		time_t _t = t;
		struct tm timeinfo = {};
		LOCALTIME_R(&_t, &timeinfo);
		const int buf_size = 1024;
		char buffer[buf_size + 1] = { 0 };
		strftime(buffer, 1024, format.c_str(), &timeinfo);
		return buffer;
	}

	inline string stringviewTolower(const string_view& input) {
		std::string result(input); 
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return std::tolower(c); 
			});
		return result;
	}

	inline char charToLower(char c) {
		if (c >= 'A' && c <= 'Z')
			return c + ('a' - 'A');
		return c;
	}

	inline string& stringTolower(string& input) {
		std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
		return input;
	}

	inline string& stringToupper(string& input) {
		transform(input.begin(), input.end(), input.begin(),
			[](unsigned char c){ return toupper(c); });
		return input;
	}

	inline size_t countU8Char(const std::string& str)
	{
		size_t count = 0;
		for (size_t i = 0; i < str.size(); ++i)
		{
			if ((str[i] & 0x80) == 0)
			{
				count += 1;
			}
			else if ((str[i] & 0xE0) == 0xC0)
			{
				i += 1;
				count += 1;
			}
			else if ((str[i] & 0xF0) == 0xE0)
			{
				i += 2;
				count += 1;
			}
			else if ((str[i] & 0xF8) == 0xF0)
			{
				i += 3;
				count += 1;
			}
		}
		return count;
	}

	inline std::string removeBracketsManual(const string_view& word)
	{
		std::string result;
		bool insideBrackets = false;
		for (char c : word)
		{
			if (c == '(')
			{
				insideBrackets = true;
			}
			else if (c == ')')
			{
				insideBrackets = false;
			}
			else if (!insideBrackets)
			{
				result += c;
			}
		}
		return result;
	}

	inline std::wstring removeBracketsManual(const wstring_view& word)
	{
		std::wstring result;
		bool insideBrackets = false;
		for (wchar_t c : word)
		{
			if (c == '(')
			{
				insideBrackets = true;
			}
			else if (c == ')')
			{
				insideBrackets = false;
			}
			else if (!insideBrackets)
			{
				result += c;
			}
		}
		return result;
	}

	//计算字符串所代表wstring的长度，用来替代原先的先转化后计算size的场景，后者的效率太低了
	//目前该函数暂不支持utf8-bom 不支持错误检查 不进行越界检查，只用于用户存盘逻辑使用，这个场景下的字符串是确定有效的
	inline size_t utf8ToWstringSize(const std::string& str) {
		size_t wsize = 0;
		const unsigned char* bytes = reinterpret_cast<const unsigned char*>(str.data());

		for (size_t i = 0; i < str.size(); ++i) {
			if ((bytes[i] & 0x80) == 0x00) {
				++wsize;
			}
			else if ((bytes[i] & 0xE0) == 0xC0) {
				++wsize;
				++i;
			}
			else if ((bytes[i] & 0xF0) == 0xE0) {
				++wsize;
				i += 2;
			}
			else if ((bytes[i] & 0xF8) == 0xF0) {
				++wsize;
				i += 3;
			}
		}
		return wsize;
	}

	inline int getCurrentProcessId()
	{
#ifdef _WIN32
	return static_cast<int>(::GetCurrentProcessId());
#else
	return static_cast<int>(::getpid());
#endif
	}

	inline filesystem::path getCurrentProcessName()
	{
#ifdef _WIN32
	WCHAR processPath[MAX_PATH] = { 0 };
	DWORD size = ::GetModuleFileNameW(NULL, processPath, MAX_PATH);
	if (size > 0)
	{
		filesystem::path path(processPath);
		return path.filename();
	}
	return filesystem::path(L"unknown_process");
#else
	return "coralpinyin";
#endif
	}

	inline filesystem::path generateProcessLogPath(const filesystem::path& baseDir)
	{
		filesystem::path process_name = getCurrentProcessName();
		int processId = getCurrentProcessId();
		filesystem::path logFileName = process_name.stem().wstring() + L"_" + std::to_wstring(processId) + L".log";
		return baseDir / logFileName;
	}

#if defined(__ANDROID__)
	inline std::filesystem::path& androidLocalLowOverride()
	{
		static std::filesystem::path s;
		return s;
	}
	inline void setLocalLowCoralPinyinPathForAndroid(const std::filesystem::path& p)
	{
		androidLocalLowOverride() = p;
	}
#endif

	inline std::filesystem::path getLocalLowCoralPinyinPath()
	{
		static std::filesystem::path path = []() -> std::filesystem::path {
			if (const char* dir = std::getenv("CORAL_USER_DATA_DIR"); dir && *dir)
				return std::filesystem::u8path(dir);
#ifdef _WIN32
			wchar_t userProfile[MAX_PATH] = { 0 };
			if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0)
			{
				return std::filesystem::path(userProfile) / L"AppData" / L"LocalLow" / L"coralpinyin";
			}
			else
			{
				// win7 的 cmd 窗口无法直接获取 USERPROFILE 环境变量，以此替代
				wchar_t p[MAX_PATH] = {};
				if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, p)))
				{
					std::filesystem::path base(p);
					auto localLowPath = base.parent_path() / L"LocalLow" / L"coralpinyin";
					return localLowPath;
				}
			}
			return std::filesystem::path("");
#elif defined(__ANDROID__)
			auto& path = androidLocalLowOverride();
			if(!path.empty())
			{
				return path;
			}
			return std::filesystem::path("/tmp/coralpinyin");
#elif defined(__APPLE__)
			// macOS: 使用用户目录下的应用数据目录
			const char* home = getenv("HOME");
			if (home) {
				return std::filesystem::path(home) / "Library" / "Application Support" / "coralpinyin";
			}
			return std::filesystem::path("/tmp/coralpinyin");
#endif
		}();
	
		return path;
	}

	inline std::filesystem::path getInstallPath()
	{
		static std::filesystem::path path = []() -> std::filesystem::path {
#ifdef _WIN32
			// 从 HKLM app 注册表读取安装路径
			HKEY hKey = nullptr;
			WCHAR buf[MAX_PATH] = {0};
			DWORD cbData = sizeof(buf);
			LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
				coral::product::kAppRegistryWide, 
				0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey);
		
			if (result == ERROR_SUCCESS)
			{
				result = RegQueryValueExW(hKey, coral::product::kInstallPathValueWide, nullptr, nullptr,
					reinterpret_cast<LPBYTE>(buf), &cbData);
				if (result == ERROR_SUCCESS)
				{
					RegCloseKey(hKey);
					return std::filesystem::path(buf);
				}
				RegCloseKey(hKey);
			}
#endif
			return std::filesystem::path("");
		}();

		return path;
	}

	enum class LogPathType
	{
		AppType,
		TsfType
	};

	inline std::filesystem::path getCoralPinyinLogPath(LogPathType type = LogPathType::TsfType)
	{
		std::filesystem::path localLowPath = getLocalLowCoralPinyinPath();
		std::filesystem::path logDir = localLowPath / L"log";
		switch(type)
		{
			case LogPathType::TsfType:
				logDir = logDir / L"TSF";
				break;
			case LogPathType::AppType:
				logDir = logDir / L"APP";
				break;
			default:
				break;
		}
		return generateProcessLogPath(logDir);
	}

	inline std::filesystem::path getCloudLexiconPath()
	{
		std::filesystem::path localLowPath = hlp::getLocalLowCoralPinyinPath();
		auto cloudPath = localLowPath / (sConfig->getSettingStore()->getIniValueAs<std::string>("app", "CloudLexiconPath"));
		return std::filesystem::path(cloudPath);
	}

	inline void initCoralPinyinLog(LogPathType type = LogPathType::TsfType)
	{
		try {
			std::filesystem::path localLowPath = getLocalLowCoralPinyinPath();
			std::filesystem::path iniPath = localLowPath / "config" / "coralpinyinConfig.ini";
			std::string iniFilePath = iniPath.string();
			std::filesystem::path logPath = getCoralPinyinLogPath(type);
			
			IniFileManager ini(iniFilePath);
			logInit(logPath
				, ini.getIniValueAs<int>("log", "level", 2)
				, ini.getIniValueAs<int>("log", "maxLogFileSize", 10) * 1024 * 1024
				, ini.getIniValueAs<int>("log", "keepLogFileCount", 5));
		}
		catch (...) {
			//加一重保险，就算写不了日志也不能影响正常运行逻辑
		}
	}

	struct ImportResult {
		int totalLinesRead = 0;								// 一共读取了多少行
		int successfulImports = 0;							// 成功的行数
		std::vector<std::pair<int, std::string>> errors;	// 错误信息
	};

	inline void trimWhitespace(std::string& s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](const unsigned char ch) {
			return !std::isspace(ch);
		}));
		s.erase(std::find_if(s.rbegin(), s.rend(), [](const unsigned char ch) {
			return !std::isspace(ch);
		}).base(), s.end());
	}

	// 用于解析单行CSV
	inline std::vector<std::string> parse_csv_line(const std::string& line) {
		std::vector<std::string> fields;
		fields.reserve(4);
		std::stringstream ss(line);
		std::string field;

		while (std::getline(ss, field, ',')) {
			trimWhitespace(field);
			fields.push_back(field);
		}

		return fields;
	}

	// CSV格式检查函数
	// 仅允许 中英文字符、数字、逗号、空格、拼音分隔符
	inline bool isLexiconLineContentValid(const std::string& line)
	{
		size_t i = 0;
		while (i < line.length()) {
			uint32_t codePoint = 0;
			const unsigned char c = line[i];

			// 1. 解码UTF-8字符以获取Unicode码点
			try {
				if (c < 0x80) { // 1 字节序列（ASCII）
					codePoint = c;
					i += 1;
				} else if ((c & 0xE0) == 0xC0) { // 2 字节序列
					codePoint = ((line.at(i) & 0x1F) << 6) | (line.at(i + 1) & 0x3F);
					i += 2;
				} else if ((c & 0xF0) == 0xE0) { // 3 字节序列（大多数中文字符）
					codePoint = ((line.at(i) & 0x0F) << 12) | ((line.at(i + 1) & 0x3F) << 6) | (line.at(i + 2) & 0x3F);
					i += 3;
				} else if ((c & 0xF8) == 0xF0) { // 4 字节序列
					codePoint = ((line.at(i) & 0x07) << 18) | ((line.at(i + 1) & 0x3F) << 12) | ((line.at(i + 2) & 0x3F) << 6) | (line.at(i + 3) & 0x3F);
					i += 4;
				} else {
					// 无效的UTF-8起始字节
					return false;
				}
			} catch ([[maybe_unused]] const std::out_of_range& oor) {
				// 字符串末尾出现不完整的UTF-8序列
				return false;
			}

			// 2. 检查码点是否在白名单内
			// a. ASCII 英文大小写字母
			if ((codePoint >= 'a' && codePoint <= 'z') || (codePoint >= 'A' && codePoint <= 'Z')) {
				continue;
			}
			// b. ASCII 数字
			if (codePoint >= '0' && codePoint <= '9') {
				continue;
			}
			// c. 逗号, 空格, 拼音分隔符
			if (codePoint == ',' || codePoint == ' ' || codePoint == '\'') {
				continue;
			}
			// d. CJK统一汉字 (常用汉字范围)
			if (codePoint >= 0x4E00 && codePoint <= 0x9FFF) {
				continue;
			}

			return false;
		}

		return true;
	}

	//一个字符串转小写的函数，做了只针对99%场景都是小写，1%情况是大写的优化
	inline string toLower_Optimized(const std::string& in) {
		string s = in;
		auto first_upper = std::find_if(s.begin(), s.end(),
			[](char c) {
				return std::isupper(static_cast<unsigned char>(c));
			}
		);

		if (first_upper == s.end()) {
			return s;
		}

		std::transform(
			first_upper, 
			s.end(),
			first_upper, 
			[](char c) {
				return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
		);
		return s;
	}
	
}

#endif // COMM_HELP_HPP（通信辅助头文件）

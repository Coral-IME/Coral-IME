/*
* @author: huangzhishou
* @date 2021-9-2 16:49:23
*/
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_HELPER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_HELPER_H_INCLUDED

#include <TlHelp32.h>
#include <sddl.h>
#include "ipcclient.h"
#include "text_file_helpers.hpp"
#include <securitybaseapi.h>
#include <shlobj.h>
#include "ime_i.c"
#pragma comment(lib, "shell32.lib")

namespace hlp
{
	enum
	{
		LOGLEVEL_NONE = 0,			//	不记录日志
		LOGLEVEL_ERROR = 1,			// 错误，记录程序执行的错误信息
		LOGLEVEL_INFO = 2,			// 信息，记录程序执行的主干流程信息
		LOGLEVEL_TRACE = 3,			// 详细的调试信息，比较容易滥用
	};
	typedef int LogLevel;

	inline BOOL isStrEmpty(LPCWSTR szStr)
	{
		return szStr == 0 || *szStr == 0;
	}

	inline CString _tr(UINT id) {
		LPWSTR p = nullptr;
		int len = ::LoadStringW(g_hInst, id, (LPWSTR)&p, 0);
		if (len > 0) {
			return  CString(p, len);
		} 
		std::wostringstream oss;
		oss << L"[CoralPinyin::_tr] is error with language,id:" << id;
		std::wstring logMsg = oss.str();

		LOG_ERROR_W(logMsg.c_str());
		return CString();
	}

	inline void outputDebugView(LPCTSTR szFormat, ...)
	{
		va_list args;
		va_start(args, szFormat);
		TCHAR szMsg[1024] = { 0 };
		_vsntprintf_s(szMsg, 1023, _TRUNCATE, szFormat, args);
		CString msg = _T("[coral] ");
		msg += szMsg;
		::OutputDebugString(msg + _T("\n"));
		va_end(args);
	}

	inline CString getFileName(LPCWSTR szFullPath)
	{
		CPath path(szFullPath);
		path.RemoveBackslash();
		LPCWSTR szName = wcsrchr(path, TEXT('\\'));
		return szName ? CString(szName + 1) : path.m_strPath;
	}

	inline CString getModulePath(HMODULE hmod)
	{
		WCHAR buffer[MAX_PATH] = { 0 };
		DWORD dwRet = ::GetModuleFileNameW(hmod, buffer, MAX_PATH - 1);
		return (dwRet ? buffer : TEXT(""));
	}

	inline CPath getCurModuleDir(HMODULE hmod)
	{
		CPath pathCur = CPath(getModulePath(hmod));
		pathCur.RemoveFileSpec();
		return pathCur;
	}

	inline bool isWinX64()
	{
		TCHAR szSysDirPath[MAX_PATH] = { 0 };
		UINT uiLen = ::GetSystemWow64Directory(szSysDirPath, MAX_PATH);
		if (uiLen > 0)
		{
			return true;
		}
		DWORD dwErr = GetLastError();
		if (ERROR_CALL_NOT_IMPLEMENTED == dwErr)
		{
			return false;
		}
		return FALSE;
	}

	inline bool writeRegStrValue(HKEY hKey, const CString& szRoot, const CString& szKey, const CString& szValue)
	{
		if (szRoot.IsEmpty())
		{
			return false;
		}
		CRegKey key;
		LONG lResult = key.Create(hKey, szRoot);
		if (lResult != ERROR_SUCCESS)
		{
			return false;
		}
		lResult = key.SetStringValue(szKey, szValue);
		return lResult == ERROR_SUCCESS;
	}

	inline bool writeRegNumValue(HKEY hKey, const CString& szRoot, const CString& szKey, DWORD dwValue)
	{
		if (szRoot.IsEmpty())
		{
			return false;
		}
		CRegKey key;
		LONG lResult = key.Create(hKey, szRoot);
		if (lResult != ERROR_SUCCESS)
		{
			return false;
		}
		lResult = key.SetDWORDValue(szKey, dwValue);
		return lResult == ERROR_SUCCESS;
	}

	inline DWORD readRegNumValue(HKEY hKey, const CString& szRoot, const CString& szKey, DWORD dwDefault = 0)
	{
		if (szRoot.IsEmpty())
			return dwDefault;

		DWORD dwValue;
		CRegKey key;
		LONG lResult = key.Open(hKey, szRoot);
		if (lResult != ERROR_SUCCESS)
			return dwDefault;

		lResult = key.QueryDWORDValue(szKey, dwValue);
		return lResult == ERROR_SUCCESS ? dwValue : dwDefault;
	}

	inline CString readRegStrValue(HKEY hKey, const CString& szRoot, const CString& szKey, const CString& szDefault = TEXT(""))
	{
		if (szRoot.IsEmpty() || szKey.IsEmpty())
			return szDefault;

		CRegKey key;
		LONG lResult = key.Open(hKey, szRoot);
		if (lResult != ERROR_SUCCESS)
			return szDefault;

		TCHAR szValue[MAX_PATH + 1] = { 0 };
		ULONG ulLen = MAX_PATH;
		lResult = key.QueryStringValue(szKey, szValue, &ulLen);
		return lResult == ERROR_SUCCESS ? szValue : szDefault;
	}

	inline bool delRegKey(HKEY hkey, const CString& szRoot, const CString& szKey)
	{
		if (szRoot.IsEmpty() || szKey.IsEmpty())
			return false;

		CRegKey key;
		if (key.Open(hkey, szRoot) != ERROR_SUCCESS)
			return false;

		return key.RecurseDeleteKey(szKey) == ERROR_SUCCESS;
	}

	inline int getConfigInt(const CString& key, const CString& rootDir = L"", int nDef = 0, const CString& section = L"ime")
	{
		CPath pathIni = getCurModuleDir(g_hInst);
		pathIni.Append(rootDir + L"ime.ini");
		return (int)::GetPrivateProfileIntW(section, key, nDef, pathIni.m_strPath);
	}

	inline CString getConfigStr(const CString& key, const CString& rootDir = L"", const CString& strDef = L"", const CString& section = L"ime")
	{
		CPath pathIni = getCurModuleDir(g_hInst);
		pathIni.Append(rootDir + L"ime.ini");
		static const DWORD MAX_SIZE = 1024;
		WCHAR buffer[MAX_SIZE + 1] = { 0 };
		::GetPrivateProfileStringW(section, key, strDef, buffer, MAX_SIZE, pathIni.m_strPath);
		return buffer;
	}

	inline int getVersion()
	{
		DWORD dwVersion = readRegNumValue(HKEY_CURRENT_USER, _T("software\\coral"), _T("version"));
		return dwVersion;
	}

	inline bool isProcessExists(const CString& process_name)
	{
		PROCESSENTRY32W pc = { sizeof(pc) };
		HANDLE hSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (INVALID_HANDLE_VALUE == hSnapshot)
			return false;

		if (::Process32FirstW(hSnapshot, &pc))
		{
			do
			{
				if (_wcsicmp(process_name, pc.szExeFile) == 0)
				{
					::CloseHandle(hSnapshot);
					return true;
				}
			} while (::Process32NextW(hSnapshot, &pc));
		}
		::CloseHandle(hSnapshot);
		return false;
	}

	inline CString CLSID2String(REFGUID refGUID)
	{
		CString res;
		res.Format(
			TEXT("{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}"),
			refGUID.Data1,
			refGUID.Data2,
			refGUID.Data3,
			refGUID.Data4[0],
			refGUID.Data4[1],
			refGUID.Data4[2],
			refGUID.Data4[3],
			refGUID.Data4[4],
			refGUID.Data4[5],
			refGUID.Data4[6],
			refGUID.Data4[7]);
		return res;
	}

	inline int split(LPCWSTR szData, WCHAR sep, std::vector<CString>& result)
	{
		LPCWSTR pos1 = NULL, pos2 = NULL;
		pos1 = szData;
		for (;;)
		{
			pos2 = wcschr(pos1, sep);
			if (!pos2)
				break;
			CString strDecode = CString(pos1, (int)(pos2 - pos1));
			result.push_back(strDecode);
			pos1 = pos2 + 1;
		}
		if (pos1 != NULL && *pos1 != 0)
			result.emplace_back(pos1);
		return (int)result.size();
	}

	inline LPSECURITY_ATTRIBUTES initSecurityAttributes(SECURITY_ATTRIBUTES& sa)
	{
		sa.bInheritHandle = TRUE;
		sa.nLength = sizeof(sa);
		LPCWSTR szSD =
			L"D:"						// 自主访问控制（DACL）
			L"(A;OICI;GRGWGXSD;;;AU)"	// 允许 Authenticated Users 修改
			L"(A;OICI;GA;;;SY)"			// 允许 SYSTEM 完全控制
			L"(A;OICI;GA;;;BA)";		// 允许管理员完全控制

		BOOL bRet = ::ConvertStringSecurityDescriptorToSecurityDescriptor(szSD, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL);
		return bRet ? &sa : NULL;
	}

	// inline void uniformDlg(CWindow* window, bool bResizeWnd = true)
	//{
	// 	CUniformUI::Instance().InitUIParameters(window->m_hWnd, 96, bResizeWnd);
	//}

	inline CString HalfToFull(const CString& halfStr) 
	{
		CString fullStr;
		for (int i = 0; i < halfStr.GetLength(); ++i) 
		{
			TCHAR ch = halfStr[i];
			if (ch == 32) 
			{  // 半角空格单独处理  
				fullStr += static_cast<TCHAR>(12288);
			}
			else if (ch >= 33 && ch <= 126) 
			{  // 半角字符和符号  
				fullStr += static_cast<TCHAR>(ch + 65248);
			}
			else 
			{
				fullStr += ch;
			}
		}
		return fullStr;
	}

	// 获取指定矩形所在显示器的工作区
	inline RECT getWorkAreaForRect(const RECT& rect)
	{
		RECT workArea;
		HMONITOR hMonitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
		if (hMonitor) {
			MONITORINFOEX info;
			info.cbSize = sizeof(MONITORINFOEX);
			if (GetMonitorInfo(hMonitor, &info)) {
				return info.rcWork;
			}
		}
		// 如果获取失败，使用主显示器
		SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
		return workArea;
	}

	// 获取指定窗口所在显示器的工作区
	inline RECT getWorkAreaForWindow(HWND hwnd)
	{
		RECT workArea;
		if (hwnd && IsWindow(hwnd)) {
			HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
			if (hMonitor) {
				MONITORINFOEX info;
				info.cbSize = sizeof(MONITORINFOEX);
				if (GetMonitorInfo(hMonitor, &info)) {
					return info.rcWork;
				}
			}
		}
		// 如果获取失败，使用主显示器
		SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
		return workArea;
	}

	// 检查当前激活的输入法是否是 CORAL 拼音
	inline bool isCoralPinyinActive()
	{
		CComPtr<ITfInputProcessorProfileMgr> pProfileMgr;
		HRESULT hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, 
			CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfileMgr, reinterpret_cast<LPVOID*>(&pProfileMgr));
		
		if (FAILED(hr) || !pProfileMgr)
		{
			return false;
		}
		
		TF_INPUTPROCESSORPROFILE activeProfile = {0};
		hr = pProfileMgr->GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD, &activeProfile);
		
		if (FAILED(hr))
		{
			return false;
		}
		
		// 如果是键盘布局（如英文键盘），而不是输入法，返回 false
		if (activeProfile.dwProfileType != TF_PROFILETYPE_INPUTPROCESSOR)
		{
			return false;
		}
		
		// 检查是否是 CORAL 拼音
		return IsEqualGUID(activeProfile.clsid, CLSID_TextService) && 
			IsEqualGUID(activeProfile.guidProfile, c_guidProfile);
	}
}


#endif // HELPER_H

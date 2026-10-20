#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UTILS_HELPER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UTILS_HELPER_H_INCLUDED

#include "skingui/uniformui.h"
#include "pch.h"
#include <vector>
#include <securitybaseapi.h>
#include <windows.h>
#include <shlobj.h>
#pragma comment(lib, "shell32.lib")

namespace hlp
{
	const static CString SettingFileName = L"user_setting";
	inline void outputDebugView(LPCTSTR szFormat, ...)
	{
		va_list args;
		va_start(args, szFormat);
		TCHAR szMsg[1024] = { 0 };
		_vsntprintf_s(szMsg, 1023, _TRUNCATE, szFormat, args);
		CString msg = _T("[coralpinyin] ");
		msg += szMsg;
		::OutputDebugString(msg);
		va_end(args);
	}

	inline BOOL isStrEmpty(LPCWSTR szStr)
	{
		return szStr == 0 || *szStr == 0;
	}

	inline CPath getCurAppDir(HMODULE hMod = NULL)
	{
		WCHAR chPath[MAX_PATH + 1] = { 0 };
		DWORD dwRet = ::GetModuleFileNameW(hMod, chPath, MAX_PATH);
		CPath pathCur = (dwRet ? chPath : L"");
		pathCur.RemoveFileSpec();
		return pathCur;
	}

	inline CPath getAppdataPath()
	{
		WCHAR wszAppdataPath[MAX_PATH] = { 0 };
		BOOL bSuccess = ::SHGetSpecialFolderPathW(NULL, wszAppdataPath, CSIDL_LOCAL_APPDATA, FALSE);
		if (!bSuccess)
			return L"";
		CPath pathResult = wszAppdataPath;
		pathResult.Append(L"coralpinyin");
		return pathResult;
	}

	inline int getConfigInt(const CString& key, int nDef = 0, const CString& section = L"setting")
	{
		CPath pathIni = getAppdataPath();
		pathIni.Append(SettingFileName);
		return (int)::GetPrivateProfileIntW(section, key, nDef, pathIni.m_strPath);
	}

	inline CString getConfigStr(const CString& key, const CString& strDef = L"", const CString& section = L"setting")
	{
		CPath pathIni = getAppdataPath();
		pathIni.Append(SettingFileName);
		static const DWORD MAX_SIZE = 1024;
		WCHAR buffer[MAX_SIZE + 1] = { 0 };
		::GetPrivateProfileStringW(section, key, strDef, buffer, MAX_SIZE, pathIni.m_strPath);
		return buffer;
	}

	inline BOOL setConfigStr(const CString& key, const CString& val, const CString& section = L"setting")
	{
		CPath pathIni = getAppdataPath();
		pathIni.Append(SettingFileName);
		return ::WritePrivateProfileStringW(section, key, val, pathIni.m_strPath);
	}

	inline BOOL setConfigIni(const CString& key, int val, const CString& section = L"setting")
	{
		CString strVal;
		strVal.Format(L"%d", val);
		return setConfigStr(key, strVal, section);
	}

	inline CString mergeList(const std::vector<CString>& vecList, const CString& sp = L"|")
	{
		CString strMerge;
		std::vector<CString>::const_iterator it = vecList.begin();
		for (; it != vecList.end(); ++it)
		{
			if (it != vecList.begin())
				strMerge.Append(sp);
			strMerge.Append(*it);
		}
		return strMerge;
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

	inline CString getCurrentVersion()
	{
		return readRegStrValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\coralpinyin", L"DisplayVersion", L"0.0.0.0");
	}

	inline void setWindIcon(HWND hWnd, UINT nResId)
	{
		HICON hIcon = ::LoadIconW(ModuleHelper::GetResourceInstance(), MAKEINTRESOURCE(nResId));
		ATLASSERT(::IsWindow(hWnd));
		::SendMessage(hWnd, WM_SETICON, TRUE, (LPARAM)hIcon);
		::SendMessage(hWnd, WM_SETICON, FALSE, (LPARAM)hIcon);
	}

	inline void uniformDlg(CWindow* window, bool bResizeWnd = true)
	{
		CUniformUI::Instance().InitUIParameters(window->m_hWnd, 96, bResizeWnd);
	}

	// 只是简单的仿照tsf的split转换,gpt改的，正确性待验证
}

#endif // HELPER_H

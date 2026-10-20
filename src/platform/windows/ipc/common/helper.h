// -----------------------------------------------------------------------
// 创建人	:	黄智寿
// 创建时间:	2011:7:16 11:08:42
// 功能描述 :	
// -----------------------------------------------------------------------
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_HELPER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_IPC_COMMON_HELPER_H_INCLUDED

#include <time.h>
#include <tchar.h>
#include <windows.h>
#include <atlpath.h>
#include <wchar.h>
namespace ipc_hlp {
	inline void OutputForTest(LPCWSTR szFormat, ...)
	{
		va_list args;
		va_start(args, szFormat);
		WCHAR szMsg[1024] = { 0 };
		_vsntprintf_s(szMsg, 1023, _TRUNCATE, szFormat, args);
		::OutputDebugString(szMsg);
		va_end(args);
	}

	inline BOOL IsStrEmpty(LPCWSTR szStr)
	{
		return szStr == 0 || *szStr == 0;
	}

	inline BOOL IsStrEmpty(LPCSTR szStr)
	{
		return szStr == 0 || *szStr == 0;
	}

	inline CPath GetCurDirector(HMODULE hModule)
	{
		WCHAR szBuf[MAX_PATH] = { 0 };
		::GetModuleFileNameW(hModule, szBuf, MAX_PATH);
		CPath path(szBuf);
		path.RemoveFileSpec();
		return path.FileExists() ? path : CPath(L"");
	}

	inline long BuildRandNum(unsigned int n)
	{
		srand((unsigned)time(NULL) + n);
		return rand();
	}

	inline BOOL Is64BitWindows()
	{
		WCHAR wszSysDirPath[MAX_PATH] = {0};
		UINT uiLen = ::GetSystemWow64DirectoryW(wszSysDirPath, MAX_PATH);
		return uiLen != 0;
	}

	inline BOOL IsWow64Process()
	{
		if(!Is64BitWindows())
			return FALSE;

		/*利用IsWow64Process判断是否是x64进程*/  
		typedef BOOL(WINAPI *pfnIsWow64Process)(HANDLE,PBOOL);
		pfnIsWow64Process fnIsWow64Process=NULL;

		HMODULE hMod = ::GetModuleHandle(L"kernel32.dll");
		fnIsWow64Process = (pfnIsWow64Process)::GetProcAddress(hMod, "IsWow64Process");
		//如果没有导出则判定为32位
		if(!fnIsWow64Process) 
			return FALSE;

		BOOL bX64;
		if(!fnIsWow64Process(::GetCurrentProcess(), &bX64)) 
			return FALSE;

		return !bX64;
	}
}

#endif // HELPER_H

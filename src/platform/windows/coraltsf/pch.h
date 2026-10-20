// pch.h: 这是预编译标头文件。
// 下方列出的文件仅编译一次，提高了将来生成的生成性能。
// 这还将影响 IntelliSense 性能，包括代码完成和许多代码浏览功能。
// 但是，如果此处列出的文件中的任何一个在生成之间有更新，它们全部都将被重新编译。
// 请勿在此处添加要频繁更新的文件，这将使得性能优势无效。
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_PCH_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_PCH_H_INCLUDED
// 添加要在此处预编译的标头
#include "framework.h"

#include <string>
#include <vector>
#include <unordered_map>

#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT ATLASSERT

#define DELETE_P(p)\
do\
{\
	if(p)\
	{\
		delete p;\
		p = nullptr;\
	}\
}while(0)


#define CHECK_ERROR(exp) \
do { \
	if (!(exp)) \
	{ \
		goto Exit0; \
	} \
} while(0)

#define CHECK_ERROR_MSG(exp, process_name, msg) \
do { \
	if (!(exp)) \
	{ \
		std::stringstream ss_log_msg; \
		ss_log_msg << process_name << " " << msg; \
		LOG_ERROR(ss_log_msg.str()); \
		goto Exit0; \
	} \
} while(0)

#define LOG_ERROR_MSG_W(exp, functionName, msg) \
do { \
	if (!(exp)) \
	{ \
		std::wstringstream wss_log_msg;  \
		wss_log_msg << functionName << L" " << msg;  \
		LOG_ERROR_W(wss_log_msg.str());	\
		goto Exit0; \
	} \
} while(0)

extern HINSTANCE g_hInst;
extern DWORD g_dwVersion;

static const GUID c_guidProfile =
{ 0x3f535cf4, 0x1a54, 0x472f, { 0x87, 0xb5, 0x2, 0xb2, 0xe9, 0x21, 0xd, 0x5 } };

constexpr GUID GUID_CANDIDATELIST_UI =
{ 0x6ec68801, 0x45e4, 0x435b, { 0xaf, 0xcc, 0xe2, 0xd1, 0x7c, 0xbf, 0x4c, 0x62 } };


#include "ime.h"
#include "xdlldata.h"
#include "helper.h"
#include "preference_registry.h"
#include "tsfwindowsevent.h"
#include "call_timing.h"
#include "trace_log.h"

typedef UINT(WINAPI* GET_DPI_FOR_WINDOW)(HWND hwnd);
extern GET_DPI_FOR_WINDOW g_fGetDpiForWindow;
namespace coral::phonetics {
using TextSlice = std::string_view;
} // 命名空间 coral::phonetics 结束（批次 3）


#endif // PCH_H

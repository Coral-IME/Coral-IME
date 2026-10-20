/**
 * 提供的 ks_* 函数列表（均为宏映射到 CRT _s 系列）：
 *   char:    ks_strlen, ks_strcmp, ks_stricmp, ks_strnicmp, ks_gets_s,
 *            ks_memcpy_s, ks_strcat_s, ks_strncat_s, ks_strcpy_s, ks_strncpy_s,
 *            ks_strnlen_s, ks_sscanf_s, ks_sprintf_s, ks_snprintf_s,
 *            ks_vsprintf_s, ks_vsnprintf_s, ks_strtok_s, ks_itoa_s,
 *            ks_ultoa_s, ks_i64toa_s, ks_ui64toa_s, ks_makepath_s, ks_splitpath_s,
 *            ks_memset_s, ks_memmove_s, ks_vsscanf_s
 *   wchar_t: ks_wcslen, ks_wcscat_s, ks_wcsncat_s, ks_wcscpy_s, ks_wcsncpy_s,
 *            ks_wcsnlen_s, ks_wcstok_s, ks_swprintf_s, ks_snwprintf_s,
 *            ks_vswprintf_s, ks_vsnwprintf_s, ks_swscanf_s,
 *            ks_itow_s, ks_ultow_s, ks_i64tow_s, ks_wmakepath_s, ks_wsplitpath_s
 *   TCHAR:   ks_sntprintf_s, ks_vsntprintf_s 等（由 UNICODE 控制）
 */

 #pragma once

 #ifndef CORAL_MODULES_FOUNDATION_SAFEC_CSTRING_WIN_H_INCLUDED
 #define CORAL_MODULES_FOUNDATION_SAFEC_CSTRING_WIN_H_INCLUDED
 
 #include <cstddef>
 #include <cstring>
 #include <cwchar>
 #include <cstdio>
 #include <cerrno>
 
 #ifndef _ERRNO_T_DEFINED
 #define _ERRNO_T_DEFINED
 typedef int errno_t;
 #endif
 
 // -------------------------------------------------------------------------
 
 #define ks_strlen			strlen
 #define ks_strcmp			strcmp
 #define ks_gets_s			fgets
 #if defined(_MSC_VER)
 #	define ks_stricmp		_stricmp
 #	define ks_strnicmp		_strnicmp
 #else
 #	define ks_stricmp		stricmp
 #	define ks_strnicmp		strnicmp
 #endif
 #define ks_wcslen			wcslen
 
 // WO 定义的函数
 #define ks_vsscanf_s		vsscanf_s
 #define ks_memmove_s		memmove_s
 
 // WO 定义的函数
 inline errno_t ks_memset_s(void* dest, int val, size_t count)
 {
     memset(dest, val, count);
     return 0;
 }
 
 // 迁自 ksprepl.h
 #define ks_tccpy_s			_tccpy_s
 #define ks_tcsncat_s		_tcsncat_s
 #define ks_tcsncpy_s		_tcsncpy_s
 #define ks_tscanf_s			_tscanf_s
 
 // 窄字符函数
 #define ks_scanf_s			scanf_s
 #define ks_sscanf_s			sscanf_s
 #define ks_sprintf_s		sprintf_s
 #define ks_snprintf_s		_snprintf_s
 #define ks_vsprintf_s		vsprintf_s
 #define ks_vsnprintf_s		_vsnprintf_s
 #define ks_strcat_s			strcat_s
 #define ks_strncat_s		strncat_s
 #define ks_strcpy_s			strcpy_s
 #define ks_strncpy_s		strncpy_s
 #define ks_strnlen_s		strnlen_s
 #define ks_strtok_s			strtok_s
 #define ks_memcpy_s			memcpy_s
 #define ks_itoa_s			_itoa_s
 #define ks_ultoa_s			_ultoa_s
 #define ks_i64toa_s			_i64toa_s
 #define ks_ui64toa_s		_ui64toa_s
 #define ks_makepath_s		_makepath_s
 #define ks_splitpath_s		_splitpath_s
 
 // 宽字符函数
 #define ks_swscanf_s		swscanf_s
 #define ks_swprintf_s		swprintf_s
 #define ks_snwprintf_s		_snwprintf_s
 #define ks_vswprintf_s		vswprintf_s
 #define ks_vsnwprintf_s		_vsnwprintf_s
 #define ks_wcscat_s			wcscat_s
 #define ks_wcsncat_s		wcsncat_s
 #define ks_wcscpy_s			wcscpy_s
 #define ks_wcsncpy_s		wcsncpy_s
 #define ks_wcsnlen_s		wcsnlen_s
 #define ks_wcstok_s			wcstok_s
 #define ks_wmemcpy_s		wmemcpy_s
 #define ks_itow_s			_itow_s
 #define ks_ultow_s			_ultow_s
 #define ks_i64tow_s			_i64tow_s
 #define ks_wmakepath_s		_wmakepath_s
 #define ks_wsplitpath_s		_wsplitpath_s
 
 #ifdef UNICODE
 #	define ks_sntprintf_s		ks_snwprintf_s
 #	define ks_vsntprintf_s		ks_vsnwprintf_s
 #	define ks_lstrcpyn_s		ks_wcsncpy_s
 #	define ks_vstprintf_s		ks_vswprintf_s
 #	define ks_stprintf_s		ks_swprintf_s
 #	define ks_stscanf_s			ks_swscanf_s
 #	define ks_tcscat_s			ks_wcscat_s
 #	define ks_lstrcpy_s			ks_wcscpy_s
 #	define ks_tmakepath_s		ks_wmakepath_s
 #	define ks_tsplitpath_s		ks_wsplitpath_s
 #else
 #	define ks_sntprintf_s		ks_snprintf_s
 #	define ks_vsntprintf_s		ks_vsnprintf_s
 #	define ks_lstrcpyn_s		ks_strncpy_s
 #	define ks_vstprintf_s		ks_vsprintf_s
 #	define ks_stprintf_s		ks_sprintf_s
 #	define ks_stscanf_s			ks_sscanf_s
 #	define ks_tcscat_s			ks_strcat_s
 #	define ks_lstrcpy_s			ks_strcpy_s
 #	define ks_tmakepath_s		ks_makepath_s
 #	define ks_tsplitpath_s		ks_splitpath_s
 #endif	// UNICODE
 
 #endif	// KS_STRING_STANDALONE_WIN_H_（Windows 独立字符串头文件）
 
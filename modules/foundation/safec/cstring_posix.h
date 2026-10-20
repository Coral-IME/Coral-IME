/**
 * 提供的 ks_* 函数列表：
 *   char:    ks_strlen, ks_strcmp, ks_stricmp, ks_strnicmp, ks_gets_s,
 *            ks_memcpy_s, ks_strcat_s, ks_strncat_s, ks_strcpy_s, ks_strncpy_s,
 *            ks_strnlen_s, ks_sscanf_s, ks_vsscanf_s, ks_sprintf_s, ks_snprintf_s,
 *            ks_vsprintf_s, ks_vsnprintf_s, ks_strtok_s, ks_itoa_s,
 *            ks_memset_s, ks_memmove_s
 *   wchar_t: ks_wcscat_s, ks_wcsncat_s, ks_wcscpy_s, ks_wcsncpy_s, ks_wcslen,
 *            ks_wcsnlen_s, ks_wcstok_s, ks_swprintf_s, ks_snwprintf_s,
 *            ks_vswprintf_s, ks_swscanf_s
 */

 #pragma once

 #ifndef CORAL_MODULES_FOUNDATION_SAFEC_CSTRING_POSIX_H_INCLUDED
 #define CORAL_MODULES_FOUNDATION_SAFEC_CSTRING_POSIX_H_INCLUDED
 
 #include <cstddef>
 #include <cstring>
 #include <cwchar>
 #include <cstdio>
 #include <cerrno>
 #include <cstdarg>
 
#if defined(USE_SECURITY_FUNCTION)
#	include <libsafec/cstring.h>
#	include <libsafec/ustring.h>
#	include <libsafec/wstring.h>
#endif
// -------------------------------------------------------------------------

#define ks_strlen			strlen
 #define ks_strcmp			strcmp
 #define ks_gets_s			fgets
 
 #if defined(_WIN32) || defined(_MSC_VER)
 #	define ks_stricmp		_stricmp
 #	define ks_strnicmp		_strnicmp
 #else
 #	define ks_stricmp		strcasecmp
 #	define ks_strnicmp		strncasecmp
 #endif
 
typedef int errno_t;

// ks_* 实现刻意委托给 CRT；NOLINT 用于抑制 coral-safe-api 检查。
// NOLINTBEGIN(coral-safe-api)

inline errno_t ks_memcpy_s(void* dest, size_t destSize, const void* src, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 memcpy(dest, src, count);
	 return 0;
 #else
	 return vmemcpy_s(dest, destSize, src, count);
 #endif
 }
 
 inline errno_t ks_strcat_s(char* strDestination, size_t numberOfElements, const char* strSource)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strcat(strDestination, strSource);
	 return 0;
 #else
	 return c_strcat_s(strDestination, numberOfElements, strSource);
 #endif
 }
 
 inline errno_t ks_strncat_s(char* strDest, size_t numberOfElements, const char* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strncat(strDest, strSource, count);
	 if (strDest && count < numberOfElements)
		 strDest[count] = 0;
	 return 0;
 #else
	 return c_strncat_s(strDest, numberOfElements, strSource, count);
 #endif
 }
 
 inline errno_t ks_strcpy_s(char* dest, size_t destSize, const char* src)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strcpy(dest, src);
	 return 0;
 #else
	 return c_strcpy_s(dest, destSize, src);
 #endif
 }
 
 inline errno_t ks_strncpy_s(char* strDest, size_t numberOfElements, const char* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strncpy(strDest, strSource, count);
	 return 0;
 #else
	 return c_strncpy_s(strDest, numberOfElements, strSource, count);
 #endif
 }
 
 inline size_t ks_strnlen_s(const char* str, size_t numberOfElements)
 {
 #ifndef USE_SECURITY_FUNCTION
	 size_t len = strlen(str);
	 return len < numberOfElements ? len : numberOfElements;
 #else
	 return c_strnlen_s(str, numberOfElements);
 #endif
 }
 
 inline int ks_vsscanf_s(const char* buffer, const char* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 return vsscanf(buffer, format, args);
 #else
	 return c_vsscanf_s(buffer, format, args);
 #endif
 }
 
 inline int ks_vsprintf_s(char* buffer, size_t numberOfElements, const char* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 return vsprintf(buffer, format, args);
 #else
	 return c_vsnprintf(buffer, numberOfElements, format, args);
 #endif
 }
 
 inline int ks_vsnprintf_s(char* buffer, size_t sizeOfBuffer, size_t count, const char* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsnprintf(buffer, sizeOfBuffer, format, args);
	 if (buffer && count < sizeOfBuffer)
		 buffer[count] = 0;
	 return ret;
 #else
	 return c_vsnprintf_s(buffer, sizeOfBuffer, count, format, args);
 #endif
 }
 
 #ifdef __cplusplus
 
 template <size_t size>
 inline errno_t ks_strcat_s(char(&strDestination)[size], const char* strSource)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strcat(strDestination, strSource);
	 return 0;
 #else
	 return c_strcat_s(strDestination, size, strSource);
 #endif
 }
 
 template <size_t size>
 inline errno_t ks_strncat_s(char(&strDest)[size], const char* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strncat(strDest, strSource, count);
	 if (count < size)
		 strDest[count] = 0;
	 return 0;
 #else
	 return c_strncat_s(strDest, size, strSource, count);
 #endif
 }
 
 template <size_t size>
 inline errno_t ks_strcpy_s(char(&dest)[size], const char* src)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strcpy(dest, src);
	 return 0;
 #else
	 return c_strcpy_s(dest, size, src);
 #endif
 }
 
 template <size_t size>
 inline errno_t ks_strncpy_s(char(&strDest)[size], const char* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 strncpy(strDest, strSource, count);
	 if (count < size)
		 strDest[count] = 0;
	 return 0;
 #else
	 return c_strncpy_s(strDest, size, strSource, count);
 #endif
 }
 
 inline int ks_sscanf_s(const char* buffer, const char* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsscanf(buffer, format, args);
 #else
	 int ret = c_vsscanf_s(buffer, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 inline int ks_sprintf_s(char* buffer, size_t sizeOfBuffer, const char* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsprintf(buffer, format, args);
 #else
	 int ret = c_vsnprintf(buffer, sizeOfBuffer, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 template <size_t size>
 inline int ks_sprintf_s(char(&buffer)[size], const char* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsprintf(buffer, format, args);
 #else
	 int ret = c_vsnprintf(buffer, size, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 inline int ks_snprintf_s(char* buffer, size_t sizeOfBuffer, size_t count, const char* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsnprintf(buffer, sizeOfBuffer, format, args);
	 if (buffer && count < sizeOfBuffer)
		 buffer[count] = 0;
 #else
	 int ret = c_vsnprintf_s(buffer, sizeOfBuffer, count, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 template <size_t size>
 inline int ks_snprintf_s(char(&buffer)[size], size_t count, const char* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsnprintf(buffer, size, format, args);
	 if (count < size)
		 buffer[count] = 0;
 #else
	 int ret = c_vsnprintf_s(buffer, size, count, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 template <size_t size>
 inline int ks_vsprintf_s(char(&buffer)[size], const char* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 return vsprintf(buffer, format, args);
 #else
	 return c_vsnprintf(buffer, size, format, args);
 #endif
 }
 
 template <size_t size>
 inline int ks_vsnprintf_s(char(&buffer)[size], size_t count, const char* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vsnprintf(buffer, size, format, args);
	 if (count < size)
		 buffer[count] = 0;
	 return ret;
 #else
	 return c_vsnprintf_s(buffer, size, count, format, args);
 #endif
 }
 
inline char* ks_strtok_s(char* str, const char* delimiters, char** context)
{
#ifndef USE_SECURITY_FUNCTION
#if defined(_WIN32) || defined(_MSC_VER)
	return strtok_s(str, delimiters, context);
#else
	return strtok_r(str, delimiters, context);
#endif
#else
	return c_strtok_s(str, delimiters, context);
#endif
}
 
 #endif /* __cplusplus */
 
 // -------------------------------------------------------------------------
 
 inline errno_t ks_wcscat_s(wchar_t* strDestination, size_t numberOfElements, const wchar_t* strSource)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcscat(strDestination, strSource);
	 return 0;
 #else
	 return w_strcat_s(strDestination, numberOfElements, strSource);
 #endif
 }
 
 template <size_t size>
 inline errno_t  ks_wcscat_s(wchar_t(&strDestination)[size], const wchar_t* strSource)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcscat(strDestination, strSource);
	 return 0;
 #else
	 return w_strcat_s(strDestination, size, strSource);
 #endif
 }
 
 inline errno_t ks_wcsncat_s(wchar_t* strDest, size_t numberOfElements, const wchar_t* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcsncat(strDest, strSource, count);
	 if (strDest && count < numberOfElements)
		 strDest[count] = 0;
	 return 0;
 #else
	 return w_strncat_s(strDest, numberOfElements, strSource, count);
 #endif
 }
 
 template <size_t size>
 inline errno_t ks_wcsncat_s(wchar_t(&strDest)[size], const wchar_t* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcsncat(strDest, strSource, count);
	 if (count < size)
		 strDest[count] = 0;
	 return 0;
 #else
	 return w_strncat_s(strDest, size, strSource, count);
 #endif
 }
 
 inline errno_t ks_wcscpy_s(wchar_t* dest, size_t dest_size, const wchar_t* src)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcscpy(dest, src);
	 return 0;
 #else
	 return w_strcpy_s(dest, dest_size, src);
 #endif
 }
 
 template <size_t size>
 inline errno_t ks_wcscpy_s(wchar_t(&dest)[size], const wchar_t* src)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcscpy(dest, src);
	 return 0;
 #else
	 return w_strcpy_s(dest, size, src);
 #endif
 }
 
 inline errno_t ks_wcsncpy_s(wchar_t* strDest, size_t numberOfElements, const wchar_t* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcsncpy(strDest, strSource, count);
	 if (strDest && count < numberOfElements)
		 strDest[count] = 0;
	 return 0;
 #else
	 return w_strncpy_s(strDest, numberOfElements, strSource, count);
 #endif
 }
 
 template <size_t size>
 inline errno_t ks_wcsncpy_s(wchar_t(&strDest)[size], const wchar_t* strSource, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 wcsncpy(strDest, strSource, count);
	 if (count < size)
		 strDest[count] = 0;
	 return 0;
 #else
	 return w_strncpy_s(strDest, size, strSource, count);
 #endif
 }
 
 inline size_t ks_wcslen(const wchar_t* str)
 {
	 return wcslen(str);
 }
 
 inline size_t ks_wcsnlen_s(const wchar_t* str, size_t numberOfElements)
 {
 #ifndef USE_SECURITY_FUNCTION
	 size_t len = wcslen(str);
	 return len < numberOfElements ? len : numberOfElements;
 #else
	 return w_strnlen_s(str, numberOfElements);
 #endif
 }
 
 inline wchar_t* ks_wcstok_s(wchar_t* str, const wchar_t* delimiters, wchar_t** context)
 {
 #ifndef USE_SECURITY_FUNCTION
	 return wcstok(str, delimiters, context);
 #else
	 return w_strtok_s(str, delimiters, context);
 #endif
 }
 
 inline int ks_swprintf_s(wchar_t* buffer, size_t sizeOfBuffer, const wchar_t* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vswprintf(buffer, sizeOfBuffer, format, args);
 #else
	 int ret = w_vsnprintf(buffer, sizeOfBuffer, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 template <size_t size>
 inline int ks_swprintf_s(wchar_t(&buffer)[size], const wchar_t* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vswprintf(buffer, size, format, args);
 #else
	 int ret = w_vsnprintf(buffer, size, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 inline int ks_snwprintf_s(wchar_t* buffer, size_t sizeOfBuffer, size_t count, const wchar_t* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vswprintf(buffer, sizeOfBuffer, format, args);
 #else
	 int ret = w_vsnprintf_s(buffer, sizeOfBuffer, count, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 template <size_t size>
 inline int ks_snwprintf_s(wchar_t(&buffer)[size], size_t count, const wchar_t* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vswprintf(buffer, size, format, args);
 #else
	 int ret = w_vsnprintf_s(buffer, size, count, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 inline int ks_vswprintf_s(wchar_t* buffer, size_t numberOfElements, const wchar_t* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 return vswprintf(buffer, numberOfElements, format, args);
 #else
	 return w_vsnprintf(buffer, numberOfElements, format, args);
 #endif
 }
 
 template <size_t size>
 inline int ks_vswprintf_s(wchar_t(&buffer)[size], const wchar_t* format, va_list args)
 {
 #ifndef USE_SECURITY_FUNCTION
	 return vswprintf(buffer, size, format, args);
 #else
	 return w_vsnprintf(buffer, size, format, args);
 #endif
 }
 
 inline int ks_swscanf_s(const wchar_t* s, const wchar_t* format, ...)
 {
	 va_list args{};
	 va_start(args, format);
 #ifndef USE_SECURITY_FUNCTION
	 int ret = vswscanf(s, format, args);
 #else
	 int ret = w_vsscanf_s(s, format, args);
 #endif
	 va_end(args);
	 return ret;
 }
 
 static inline char* i2a(unsigned i, char* a, unsigned r)
 {
	 if (i / r > 0) a = i2a(i / r, a, r);
	 *a = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i % r];
	 return a + 1;
 }
 
 inline errno_t ks_itoa_s(int i, char* a, size_t s, int r)
 {
 #ifndef USE_SECURITY_FUNCTION
	 r = ((r < 2) || (r > 36)) ? 10 : r;
	 if (i < 0)
	 {
		 *a = '-';
		 *i2a(-i, a + 1, r) = 0;
	 }
	 else
	 {
		 *i2a(i, a, r) = 0;
	 }
	 return 0;
 #else
	 return c_itoa_s(i, a, s, r);
 #endif
 }
 
 // WO 定义的函数
 inline errno_t v_memset_s(void* dest, size_t destSize, int val, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 memset(dest, val, count);
	 return 0;
 #else
	 if (count == 0)
		 return 0;
	 if (!dest)
		 return EINVAL;
	 if (count > destSize)
		 return ERANGE;
	 memset(dest, val, count);
	 return 0;
 #endif
 }
 
 #define ks_memset_s(dest, val, count)	v_memset_s(dest, count, val, count)
 
 // WO 定义的函数
 inline errno_t ks_memmove_s(void* dest, size_t destSize, const void* src, size_t count)
 {
 #ifndef USE_SECURITY_FUNCTION
	 memmove(dest, src, count);
	 return 0;
 #else
	 if (count == 0)
		 return 0;
	 if (!dest || !src)
		 return EINVAL;
	 if (destSize < count)
		 return ERANGE;
	 memmove(dest, src, count);
	 return 0;
 #endif
 }
 
#define ks_vsnwprintf_s		ks_snwprintf_s

// NOLINTEND(coral-safe-api)

#endif /*  KS_STRING_STANDALONE_H_（独立字符串头文件） */
 
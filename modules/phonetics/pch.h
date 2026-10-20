/*
* @author: huangzhishou
* @date: 2021-8-19 10:27:54
*/
#pragma once
#ifndef CORAL_MODULES_PHONETICS_PCH_H_INCLUDED
#define CORAL_MODULES_PHONETICS_PCH_H_INCLUDED

#include <string>
#include <filesystem>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <iostream>
#include <regex>

#if defined(_WIN32) || defined(_MSC_VER)
#include <atlpath.h>
#include <fstream>
#elif defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <fstream>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <cassert>
#include <cstring>
#include <algorithm>
#endif
using namespace std;

#define DELETE_P(p)\
do\
{\
	if(p)\
	{\
		delete p;\
		p = nullptr;\
	}\
}while(0)

#ifdef _DEBUG
	#ifndef USE_DEBUG
		#define USE_DEBUG
	#endif
#endif

#define CHECK_INIT_ERROR(exp) \
do { \
	if (!(exp)) \
	{ \
		return false; \
	} \
} while(0)

#ifdef NDEBUG  // release下不崩溃
#define CHECK_ERROR(exp) \
do { \
    if (!(exp)) \
    { \
        goto Exit0; \
    } \
} while(0)
#else  // debug下崩溃
#define CHECK_ERROR(exp) \
do { \
    if (!(exp)) \
    { \
        goto Exit0; \
    } \
} while(0)
#endif

#define CHECK_ERROR_MSG(exp, msg) \
do { \
	if (!(exp)) \
	{ \
		LOG_ERROR(msg); \
		goto Exit0; \
	} \
} while(0)

#ifdef USE_DEBUG
    #define K_ASSERT(exp) assert((exp))
#else
    #define K_ASSERT(exp) ((void)0)
#endif

/*Author:qipan Log调试宏*/
#define DEBUG_LOGGER//启用调试



/*Author:qipan TimeTracker调试宏*/
#define DEBUG_TIME_TRACKER//启用调试

#ifdef DEBUG_TIME_TRACKER

#define TIME_TRACKER_INSTANCE() TimeTracker& tracker = TimeTracker::instance();
#define TIME_TRACKER_DEBUG(msg) tracker.mark(msg)
#define TIME_TRACKER_OUTPUT() tracker.outputResults();
#else
#define TIME_TRACKER_INSTANCE()
#define TIME_TRACKER_DEBUG(msg)
#define TIME_TRACKER_OUTPUT()
#endif

#include "phonetic_types.h"
#include "utf8conv.h"
#include "recursive_lock.h"
#include "syllable_helpers.h"
#include "trace_log.h"
#include "ini_store.h"
#include "call_timing.h"
namespace coral::phonetics {
using TextSlice = std::string_view;
} // 命名空间 coral::phonetics 结束（批次 3）

#if defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
typedef uint32_t DWORD;
typedef int errno_t;
int memcpy_s(void *dest, size_t dest_size, const void *src, size_t count);
// 跨平台解除内存映射
bool UnmapViewOfFile(const char* baseAddress, size_t size = 0);
std::string float2_string(float value);
#define HMODULE void*
#endif

bool map_file_to_memory(const std::filesystem::path& filePath, const char*& pFileData, DWORD& file_size);

#endif // PCH_H

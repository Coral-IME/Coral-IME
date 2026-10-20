/*
* spdlog 适配层：保留原有 ksolog 日志 API（logInit / logger / loggerW / LOG_* 宏 / CTraceStack）
* 实现已切换为 spdlog rotating_file_sink（异步线程池）。
* 上游源码：src/lib/spdlog（v1.17.0，MIT License）
*/
#pragma once
#ifndef CORAL_MODULES_FOUNDATION_TRACE_LOG_H_INCLUDED
#define CORAL_MODULES_FOUNDATION_TRACE_LOG_H_INCLUDED

#include <filesystem>
#include <time.h>
#include <string>
enum {
	LOGLEVEL_NONE = 0,
	LOGLEVEL_ERROR = 1,
	LOGLEVEL_INFO = 2,
	LOGLEVEL_TRACE = 3
};
typedef int LogLevel;

bool logInit(const std::filesystem::path& logFile, LogLevel level, int logFileMaxSize = 0, int keepLogCount = 0);
// 显式关闭：短生命周期进程在 main 末尾调用，避免静态析构顺序问题
// （spdlog registry 与线程池先于全局 Log 析构时，flush 会段错误）
void logShutdown();
void logger(LogLevel level, const char* fname, int line, const char* func, const std::string& msg);
void loggerW(LogLevel level, const char* fname, int line, const char* func, const std::wstring& msg);

#define LOG_ERROR(x1)  logger(LOGLEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, x1)
#define LOG_ERROR_W(x1) loggerW(LOGLEVEL_ERROR, __FILE__, __LINE__, __FUNCTION__, x1)
#define LOG_INFO(x1)  logger(LOGLEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, x1)
#define LOG_INFO_W(x1) loggerW(LOGLEVEL_INFO, __FILE__, __LINE__, __FUNCTION__, x1)
#define LOG_TRACE(x1)  logger(LOGLEVEL_TRACE, __FILE__, __LINE__, __FUNCTION__, x1)
#define LOG_TRACE_W(x1) loggerW(LOGLEVEL_TRACE, __FILE__, __LINE__, __FUNCTION__, x1)

class CTraceStack
{
public:
	CTraceStack(const std::string& fname, const std::string& func, int line, bool* isok = nullptr, const std::string& args = "");
	~CTraceStack();
private:
	clock_t m_start;
	std::string m_fname;
	std::string m_func;
	int m_line;
	bool* m_isok;
};

#define RECORD_TRACE(x1, x2) CTraceStack __z_record_trace__(__FILE__, __FUNCTION__, __LINE__, x1, x2)

#endif // LOG_H

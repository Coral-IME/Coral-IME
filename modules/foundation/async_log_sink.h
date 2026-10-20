/*
* AsyncLogger 兼容层：原自研异步日志（队列+后台线程写临时目录文件）。
* 现内部转发到 spdlog 适配层（common/log.cpp），写到临时目录
* "CORAL Input Method/log" 下的性能追踪日志。
* 保留此头以兼容 functimer.cpp 等调用方，后续可整体移除。
*/
#pragma once
#ifndef CORAL_MODULES_FOUNDATION_ASYNC_LOG_SINK_H_INCLUDED
#define CORAL_MODULES_FOUNDATION_ASYNC_LOG_SINK_H_INCLUDED

#include <string>

#ifdef _UNICODE
#include <string>
class AsyncLogger
{
public:
	static AsyncLogger& instance();
	void logMessage(const std::wstring& message);
	~AsyncLogger() = default;

private:
	AsyncLogger() = default;
	AsyncLogger(const AsyncLogger&) = delete;
	AsyncLogger& operator=(const AsyncLogger&) = delete;
};
#else
class AsyncLogger
{
public:
	static AsyncLogger& instance();
	void setLogFilePath(const std::string& path);
	void logMessage(const std::string& message);
	~AsyncLogger() = default;

private:
	AsyncLogger() = default;
	AsyncLogger(const AsyncLogger&) = delete;
	AsyncLogger& operator=(const AsyncLogger&) = delete;
};
#endif // _UNICODE

#endif // ASYNCLOGGER_H

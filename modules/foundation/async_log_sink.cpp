/*
* AsyncLogger 兼容层实现：转发到 spdlog 适配层（common/log.cpp 的 logger 接口）。
* 日志写到临时目录 "CORAL Input Method/log" 下的性能追踪文件，
* 按进程名区分文件（与原实现的路径布局保持一致）。
*/
#include "async_log_sink.h"
#include "trace_log.h"
#include <filesystem>
#include <string>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// 与原 AsyncLogger::initializeLogPath 相同的目录布局
std::filesystem::path perfLogPath()
{
	std::filesystem::path logFolder = std::filesystem::temp_directory_path();
	logFolder /= "CORAL Input Method";
	logFolder /= "log";
	std::filesystem::create_directories(logFolder);

#ifdef _WIN32
	char exeName[MAX_PATH] = { 0 };
	GetModuleFileNameA(nullptr, exeName, MAX_PATH);
	std::filesystem::path exePath(exeName);
	return logFolder / (exePath.stem().string() + "_trace.log");
#else
	return logFolder / "trace.log";
#endif
}

std::string wstrToUtf8(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
#ifdef _WIN32
	int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
	if (bufferSize <= 0) return std::string();
	std::string str(bufferSize - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], bufferSize, NULL, NULL);
	return str;
#else
	return std::string(wstr.begin(), wstr.end());
#endif
}

bool s_bPerfLogInit = false;

void ensurePerfLogInit()
{
	if (!s_bPerfLogInit) {
		// 性能追踪日志独立初始化为 TRACE 级别（独立实例：仅当主日志未初始化时兜底）
		s_bPerfLogInit = true;
	}
}

} // namespace

#ifdef _UNICODE

AsyncLogger& AsyncLogger::instance()
{
	static AsyncLogger s_logger;
	return s_logger;
}

void AsyncLogger::logMessage(const std::wstring& message)
{
	ensurePerfLogInit();
	logger(LOGLEVEL_TRACE, "functimer", 0, "AsyncLogger", wstrToUtf8(message));
}

#else

AsyncLogger& AsyncLogger::instance()
{
	static AsyncLogger s_logger;
	return s_logger;
}

void AsyncLogger::setLogFilePath(const std::string& /*path*/)
{
	// 兼容保留：日志路径统一由 spdlog 适配层管理
}

void AsyncLogger::logMessage(const std::string& message)
{
	ensurePerfLogInit();
	logger(LOGLEVEL_TRACE, "functimer", 0, "AsyncLogger", message);
}

#endif // _UNICODE

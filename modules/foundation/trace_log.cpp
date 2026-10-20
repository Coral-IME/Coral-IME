/*
* spdlog 适配层实现：替代原 ksolog（自研 ofstream 同步写 + 线程清理）。
* - rotating_file_sink 对应原 logFileMaxSize / keepLogCount 策略
* - 异步线程池对应原 AsyncLogger 的后台写线程
* - 级别映射：LOGLEVEL_ERROR->err, INFO->info, TRACE->debug(trace)
* 注意：日志目录的旧文件清理策略（TSF 200 / APP 50 个）保留原 cleanupLogFolder 逻辑。
*/
#include "trace_log.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include <mutex>
#include <algorithm>
#include <vector>
#include <fstream>

using namespace std;

namespace {

constexpr int MAX_LOG_FILESIZE = 1024 * 1024 * 10;
constexpr int TSF_LOG_FILE_THRESHOLD = 200;
constexpr int APP_LOG_FILE_THRESHOLD = 50;

// 原 log.cpp 中的目录清理逻辑保留：控制日志目录内总文件数
void cleanupLogDir(const filesystem::path& logFile, int maxFileCount)
{
	try {
		filesystem::path logDir = logFile.parent_path();
		if (!filesystem::exists(logDir)) {
			return;
		}
		vector<pair<int, filesystem::path>> paths;
		for (auto& entry : filesystem::directory_iterator(logDir)) {
			if (filesystem::is_directory(entry.status()))
				continue;
			auto mtime = filesystem::last_write_time(entry);
			int mtimeSec = static_cast<int>(mtime.time_since_epoch().count()
				* filesystem::file_time_type::period::num / filesystem::file_time_type::period::den);
			paths.push_back(make_pair(mtimeSec, entry.path()));
		}
		if (static_cast<int>(paths.size()) <= maxFileCount) {
			return;
		}
		sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		int deleteCount = static_cast<int>(paths.size()) - maxFileCount;
		error_code ecode{};
		for (int i = 0; i < deleteCount && !paths.empty(); ++i) {
			filesystem::remove(paths.back().second, ecode);
			paths.pop_back();
		}
	}
	catch (...) {
	}
}

spdlog::level::level_enum toSpdlogLevel(LogLevel level)
{
	switch (level) {
	case LOGLEVEL_ERROR: return spdlog::level::err;
	case LOGLEVEL_INFO:  return spdlog::level::info;
	case LOGLEVEL_TRACE: return spdlog::level::trace;
	default:             return spdlog::level::off;
	}
}

class Log
{
public:
	Log() : m_level(LOGLEVEL_NONE) {}
	~Log()
	{
		//这里不能shutdown，会造成退出时崩溃，原因待分析
		//shutdown();
	}

	// 幂等：显式 logShutdown() 调用后，静态析构时再次进入为空操作
	void shutdown()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_logger) {
			try {
				m_logger->flush();
			} catch (...) {
			}
			m_logger.reset();
		}
		spdlog::shutdown();
	}

	bool init(const filesystem::path& logFile, LogLevel level, int logFileMaxSize, int keepLogCount)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_logger) {
			return true;
		}
		try {
			filesystem::path logDir = logFile.parent_path();
			error_code ecode;
			filesystem::create_directories(logDir, ecode);

			// rotating 文件名策略：与原实现一致，滚动文件带序号后缀
			spdlog::init_thread_pool(8192, 1);
			auto rotatingSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
				logFile.string(),
				logFileMaxSize > 0 ? static_cast<size_t>(logFileMaxSize) : static_cast<size_t>(MAX_LOG_FILESIZE),
				keepLogCount > 0 ? static_cast<size_t>(keepLogCount) : 5);
			// 输出格式尽量贴近旧格式：时间 [级别] [ 文件:(行) ] 消息
			rotatingSink->set_pattern("%Y-%m-%d %H:%M:%S\t%l\t[%s:(%#)]\t%v");
			m_logger = std::make_shared<spdlog::async_logger>(
				"coral", rotatingSink, spdlog::thread_pool(),
				spdlog::async_overflow_policy::block);
			m_logger->set_level(toSpdlogLevel(level));
			m_logger->flush_on(spdlog::level::err);
			spdlog::register_logger(m_logger);

			m_logFile = logFile;
			m_level = level;
			// 目录级清理（沿用旧阈值）
			string dirStr = logDir.string();
			bool isTsf = dirStr.find("TSF") != string::npos;
			cleanupLogDir(logFile, isTsf ? TSF_LOG_FILE_THRESHOLD : APP_LOG_FILE_THRESHOLD);
			return true;
		}
		catch (...) {
			// 与旧实现一致：日志初始化失败不影响主流程
			return false;
		}
	}

	LogLevel getLevel() const { return m_level; }

	void writeLog(LogLevel level, const string& fname, int line, const string& funcName, const string& logMsg)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_logger || level > m_level) {
			return;
		}
		m_logger->log(toSpdlogLevel(level), logMsg);
	}

private:
	filesystem::path m_logFile;
	LogLevel m_level = LOGLEVEL_NONE;
	std::shared_ptr<spdlog::async_logger> m_logger;
	std::mutex m_mutex;
} __g__log__;

} // namespace

void logShutdown()
{
	__g__log__.shutdown();
}

bool logInit(const filesystem::path& logFile, LogLevel level, int logFileMaxSize, int keepLogCount)
{
	if (logFileMaxSize <= 0) {
		logFileMaxSize = MAX_LOG_FILESIZE;
	}
	if (keepLogCount <= 0) {
		keepLogCount = 5;
	}
	return __g__log__.init(logFile, level, logFileMaxSize, keepLogCount);
}

void logger(LogLevel level, const char* fname, int line, const char* func, const string& msg)
{
	if (level <= __g__log__.getLevel())
	{
		__g__log__.writeLog(level, fname, line, func, msg);
	}
}

void loggerW(LogLevel level, const char* fname, int line, const char* func, const std::wstring& msg)
{
	std::string converted;
	if (!msg.empty()) {
#ifdef _WIN32
		int bufferSize = WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, NULL, 0, NULL, NULL);
		if (bufferSize > 0) {
			converted.resize(bufferSize - 1);
			WideCharToMultiByte(CP_UTF8, 0, msg.c_str(), -1, &converted[0], bufferSize, NULL, NULL);
		}
#endif
	}
	logger(level, fname, line, func, converted);
}

CTraceStack::CTraceStack(const std::string& fname, const std::string& func, int line, bool* isok, const std::string& args)
{
	filesystem::path f(fname);
	m_fname = f.filename().string();
	m_func = func;
	m_line = line;
	m_isok = isok;
	logger(LOGLEVEL_TRACE, m_fname.c_str(), line, func.c_str(), string("[CTraceStack In] ") + args);
	m_start = clock();
}

CTraceStack::~CTraceStack()
{
	clock_t finish = clock();
	double cost = ((double)finish - m_start) / (double)CLOCKS_PER_SEC;
	string hr = "success";
	if (m_isok) {
		hr = *m_isok ? "success" : "***failed***";
	}
	logger(LOGLEVEL_TRACE, m_fname.c_str(), m_line, m_func.c_str(),
		"[CTraceStack Out] " + hr + ", CostTime: " + to_string(cost) + "s");
}

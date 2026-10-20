#include "call_timing.h"
#include <sys/types.h>

#ifdef FUNCTIMER_OPEN
#ifdef _UNICODE

std::wstring generateUUID()
{
	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_int_distribution<> dis(0, 15);
	static std::uniform_int_distribution<> dis2(8, 11);

	std::wstringstream uuidStream;

	for (int i = 0; i < 8; i++) 
		uuidStream << std::hex << dis(gen);
	uuidStream << L"-";

	for (int i = 0; i < 4; i++) 
		uuidStream << std::hex << dis(gen);
	uuidStream << L"-4";

	for (int i = 0; i < 3; i++) 
		uuidStream << std::hex << dis(gen);
	uuidStream << L"-";

	uuidStream << std::hex << dis2(gen);

	for (int i = 0; i < 3; i++) 
		uuidStream << std::hex << dis(gen);
	uuidStream << L"-";

	for (int i = 0; i < 12; i++) 
		uuidStream << std::hex << dis(gen);

	return uuidStream.str();
}

// 获取当前时间字符串
std::wstring current_date_time()
{
	auto now = std::chrono::system_clock::now();
	auto now_c = std::chrono::system_clock::to_time_t(now);

	tm localTime = {};
	localtime_s(&localTime, &now_c);

	std::wstringstream wss;
	wss << std::put_time(&localTime, L"%Y-%m-%d %H:%M:%S");

	auto now_micros = std::chrono::time_point_cast<std::chrono::microseconds>(now);
	auto micros_since_epoch = now_micros.time_since_epoch();
	auto micros = micros_since_epoch.count() % 1000000;

	wss << L"." << std::setfill(L'0') << std::setw(6) << micros;
	return wss.str();
}

// 获取当前进程ID
std::wstring getProcessId()
{
	static std::wstring pid;
	if (pid == L"")
		pid = std::to_wstring(static_cast<__int64>(getpid()));
	return pid;
}

// 静态成员初始化
std::vector<FuncTimer::CallInfo> FuncTimer::s_callStack;

// 构造函数
FuncTimer::FuncTimer(const std::wstring& funcName,const std::wstring& content)
	: m_funcName(funcName), m_context(generateNewTraceContext()), m_startTime(std::chrono::high_resolution_clock::now())
{	
	if (!content.empty())
		setContent(content);
	s_callStack.push_back({ m_funcName, m_context.spanId });
}

// 构造函数EX
FuncTimer::FuncTimer(const std::wstring& funcName,const std::wstring& content, const TraceContext& ctx)
	: m_funcName(funcName), m_context(ctx), m_startTime(std::chrono::high_resolution_clock::now())
{
	// 跨进程逻辑
	if (m_context.parentName!=L"" && m_context.startTimeNs != -1)
	{
		// 在接收端重新获取当前时间
		auto currentTime = std::chrono::steady_clock::now();
		auto currentTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();

		// 使用接收端时间计算差值
		const auto timeDiffUs = (currentTimeNs - m_context.startTimeNs) / 1000.0;
		std::wstring logMsg = formatLogMsg(timeDiffUs, m_context.parentName, m_funcName, L"CrossProcessIPC");
		AsyncLogger::instance().logMessage(logMsg);
	}
	if (!content.empty())
		setContent(content);
	s_callStack.push_back({ m_funcName, m_context.spanId });
}

FuncTimer::~FuncTimer()
{
	if (s_callStack.empty()) 
		return;

	const auto endTime = std::chrono::high_resolution_clock::now();
	const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - m_startTime).count();
	const auto us = ns / 1000.0; // 微秒

	const std::wstring currentFunc = s_callStack.back().funcName;
	//如果一个函数即将出栈，那么他的父函数一定在size-2的位置上
	const std::wstring parentFunc = s_callStack.size() >= 2 ? s_callStack[s_callStack.size() - 2].funcName : L"";

	// 出栈
	s_callStack.pop_back();

	// 记录日志
	//别的进程传来的
	std::wstring logMsg= formatLogMsg(us, parentFunc, currentFunc, L"No");
	if (m_context.startTimeNs != -1 && m_context.parentName != L"")
		logMsg = formatLogMsg(us, m_context.parentName, currentFunc, L"CrossProcess");
	AsyncLogger::instance().logMessage(logMsg);
}

// 生成新上下文
TraceContext FuncTimer::generateNewTraceContext(long long startTimeNs, const std::wstring& parentName)
{
	TraceContext ctx;
	ctx.traceId = generateUUID();
	ctx.spanId = generateUUID();
	ctx.parentId = L"ROOT";
	ctx.processId = getProcessId();

	if (startTimeNs != -1 && !parentName.empty())
	{
		ctx.startTimeNs = startTimeNs;
		ctx.parentName = parentName;
	}
	return ctx;
}

// 获取上下文
const TraceContext& FuncTimer::getContext() const 
{ 
	return m_context;
}

// 设置内容
void FuncTimer::setContent(const std::wstring& content)
{
	m_context.content = content;
}

// 格式化日志消息
std::wstring FuncTimer::formatLogMsg(double us, const std::wstring& parentFunc, const std::wstring& currentFunc, const std::wstring& isCrossProcess)
{
	std::wstring logMsg;
	logMsg = current_date_time();
	logMsg += L"|";
	logMsg += m_context.processId;
	logMsg += L"|";
	logMsg += isCrossProcess;
	logMsg += L"|";
	logMsg += m_context.content;
	logMsg += L"|";
	logMsg += parentFunc!=L"" ? parentFunc : L"ROOTFUNC";
	logMsg += L"|";
	logMsg += currentFunc;
	logMsg += L"|";

	std::wstringstream usStream;
	usStream << std::fixed << std::setprecision(2) << us;  
	logMsg += usStream.str();

	if (parentFunc == L"" )
		logMsg += L"\n";
	return logMsg;
}
#else
#include <random>
#include <sstream>
std::string generateUUID() 
{
	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_int_distribution<> dis(0, 15);
	static std::uniform_int_distribution<> dis2(8, 11);

	std::ostringstream oss;
	for (int i = 0; i < 8; i++) 
	{
		oss << std::hex << dis(gen);
	}
	oss << "-";
	for (int i = 0; i < 4; i++) 
	{
		oss << std::hex << dis(gen);
	}
	oss << "-4";
	for (int i = 0; i < 3; i++) 
	{
		oss << std::hex << dis(gen);
	}
	oss << "-";
	oss << std::hex << dis2(gen);
	for (int i = 0; i < 3; i++) 
	{
		oss << std::hex << dis(gen);
	}
	oss << "-";
	for (int i = 0; i < 12; i++) 
	{
		oss << std::hex << dis(gen);
	}
	std::string uuid = oss.str();
	return uuid.substr(0, 16);
}

std::string current_date_time() 
{
	auto now = std::chrono::system_clock::now();
	auto now_c = std::chrono::system_clock::to_time_t(now);
	std::ostringstream oss;

	std::tm localTime;
	localtime_s(&localTime, &now_c);
	oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");

	auto now_micros = std::chrono::time_point_cast<std::chrono::microseconds>(now);
	auto micros_since_epoch = now_micros.time_since_epoch();
	auto micros = micros_since_epoch.count() % 1000000;

	oss << "." << std::setfill('0') << std::setw(6) << micros;

	return oss.str();
}

std::string getProcessId() 
{
	static std::string pid;
	if (pid.empty()) 
		pid = std::to_string(static_cast<__int64>(getpid()));
	return pid;
}

std::vector<FuncTimer::CallInfo> FuncTimer::s_callStack;

FuncTimer::FuncTimer(const std::string& funcName, const std::string& content)
	: m_funcName(funcName), m_context(generateNewTraceContext()), m_startTime(std::chrono::high_resolution_clock::now())
{
	if (!content.empty()) 
		setContent(content);
	s_callStack.push_back({ m_funcName, m_context.spanId });
}

FuncTimer::FuncTimer(const std::string& funcName, const std::string& content, const TraceContext& ctx)
	: m_funcName(funcName), m_context(ctx), m_startTime(std::chrono::high_resolution_clock::now())
{
	// 跨进程逻辑
	if (!m_context.parentName.empty() && m_context.startTimeNs != -1)
	{
		auto currentTime = std::chrono::steady_clock::now();
		auto currentTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(currentTime.time_since_epoch()).count();

		const auto timeDiffUs = (currentTimeNs - m_context.startTimeNs) / 1000;
		std::string logMsg = formatLogMsg(timeDiffUs, m_context.parentName, m_funcName, "CrossProcessIPC");
		AsyncLogger::instance().logMessage(logMsg);
	}
	if (!content.empty()) 
		setContent(content);
	s_callStack.push_back({ m_funcName, m_context.spanId });
}

FuncTimer::~FuncTimer()
{
	if (s_callStack.empty()) 
		return;

	const auto endTime = std::chrono::high_resolution_clock::now();
	const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - m_startTime).count();
	const auto us = ns / 1000.0; // 微秒

	const std::string currentFunc = s_callStack.back().funcName;
	const std::string parentFunc = s_callStack.size() >= 2 ? s_callStack[s_callStack.size() - 2].funcName : "";

	s_callStack.pop_back();

	//别的进程传来的
	std::string logMsg = formatLogMsg(us, parentFunc, currentFunc, "No");
	if (m_context.startTimeNs != -1 && m_context.parentName != "")
		logMsg = formatLogMsg(us, m_context.parentName, currentFunc, "CrossProcess");

	AsyncLogger::instance().logMessage(logMsg);
}

TraceContext FuncTimer::generateNewTraceContext(long long startTimeNs, const std::string& parentName)
{
	TraceContext ctx;
	ctx.parentId = "ROOT";
	ctx.processId = getProcessId();

	if (startTimeNs != -1 && !parentName.empty())
	{
		ctx.startTimeNs = startTimeNs;
		ctx.parentName = parentName;
	}
	return ctx;
}

const TraceContext& FuncTimer::getContext() const 
{ 
	return m_context; 
}

void FuncTimer::setContent(const std::string& content)
{
	m_context.content = content;
}

std::string FuncTimer::formatLogMsg(double us, const std::string& parentFunc, const std::string& currentFunc, const std::string& isCrossProcess)
{
	std::ostringstream oss;
	oss << current_date_time();
	oss << "|";
	oss << m_context.processId;
	oss << "|";
	oss << isCrossProcess;
	oss << "|";
	oss << m_context.content;
	oss << "|";
	oss << (parentFunc != "" ? parentFunc : "ROOTFUNC");
	oss << "|";
	oss << currentFunc;
	oss << "|";
	oss << us;

	if (parentFunc == "")
		oss << "\n";
	return oss.str();
}
#endif
#endif
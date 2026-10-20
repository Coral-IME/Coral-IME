#pragma once
#ifndef CORAL_MODULES_PREFERENCES_CALL_TIMING_H_INCLUDED
#define CORAL_MODULES_PREFERENCES_CALL_TIMING_H_INCLUDED

//控制使用这个宏与否的重要宏 默认注释掉，不使用
// #define FUNCTIMER_OPEN

#include "async_log_sink.h"
#include <functional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>

#ifdef FUNCTIMER_OPEN
#ifdef _UNICODE 

// 调用链上下文结构
struct TraceContext 
{
	//原本的打算是函数链有一个自己的id，但后来这个想法暂时放弃了
	//但是逻辑暂时保留，后续有可能在异步这种情况的时候复用
	std::wstring traceId;   // 函数链的SpanID（现在没啥用）
	std::wstring spanId;   // 当前函数的SpanID（现在没啥用）
	std::wstring parentId;   // 父函数的SpanID（现在没啥用）
	std::wstring processId;  // 进程ID或服务名
	std::wstring content = L"noContent"; // 内容
	long long startTimeNs = -1;      // 开始时间戳（多进程间使用） 存储ns，输出的时候看情况转us或ms
	std::wstring parentName = L""; // 父函数Name（多进程间使用）
};

class FuncTimer 
{
public:
	explicit FuncTimer(const std::wstring& funcName, const std::wstring& content = L"");

	FuncTimer(const std::wstring& funcName, const std::wstring& content, const TraceContext& ctx);

	~FuncTimer();

	// 生成新的TraceContext
	static TraceContext generateNewTraceContext(long long startTimeNs = -1, const std::wstring& parentName = L"");

	const TraceContext& getContext() const;

	void setContent(const std::wstring& content);

private:
	std::wstring m_funcName;
	TraceContext m_context;
	std::chrono::high_resolution_clock::time_point m_startTime;

	//维护全局堆栈结构
	struct CallInfo 
	{
		std::wstring funcName;
		std::wstring spanId;
	};

	// 静态成员
	static std::vector<CallInfo> s_callStack;

	// 格式化日志消息
	std::wstring formatLogMsg(double us, const std::wstring& parentFunc, const std::wstring& currentFunc, const std::wstring& isCrossProcess);
};
#else 
// 调用链上下文结构
struct TraceContext 
{
	//原本的打算是函数链有一个自己的id，但后来这个想法暂时放弃了
	//但是逻辑暂时保留，后续有可能在异步这种情况的时候复用
	std::string traceId;   // 函数链的SpanID（现在没啥用） 
	std::string spanId;   // 当前函数的SpanID（现在没啥用）
	std::string parentId;   // 父函数的SpanID（现在没啥用） 
	std::string processId;  // 进程ID或服务名
	std::string content = "noContent"; // 内容
	long long startTimeNs = -1;      // 开始时间戳（多进程间使用）
	std::string parentName = ""; // 父函数Name（多进程间使用）
};

class FuncTimer 
{
public:
	explicit FuncTimer(const std::string& funcName, const std::string& content = "");

	FuncTimer(const std::string& funcName, const std::string& content, const TraceContext& ctx);

	// 析构函数
	~FuncTimer();

	static TraceContext generateNewTraceContext(long long startTimeNs = -1, const std::string& parentName = "");

	const TraceContext& getContext() const;

	// 设置内容
	void setContent(const std::string& content);

private:
	std::string m_funcName;
	TraceContext m_context;
	std::chrono::high_resolution_clock::time_point m_startTime;

	// 嵌套结构保存函数名
	struct CallInfo 
	{
		std::string funcName;
		std::string spanId;
	};

	// 静态成员
	static std::vector<CallInfo> s_callStack;

	// 格式化日志消息
	std::string formatLogMsg(double us, std::string parentFunc, std::string currentFunc, std::string isCrossProcess);
};
#endif

#ifdef _UNICODE
// 宏定义正常使用TIMER_FUNC和TIMER_FUNC_DIY即可，多进程通信使用EX
#define TIMER_FUNC(content) FuncTimer __timer__(__FUNCTIONW__, content)
#define TIMER_FUNC_DIY(content,funcName) FuncTimer __timer__(funcName, content)
#define TIMER_FUNC_EX(content, startTimeNs, parentName) FuncTimer __timer__(__FUNCTIONW__, content, FuncTimer::generateNewTraceContext(startTimeNs, parentName))
#else
#define TIMER_FUNC(content) FuncTimer __timer__(__FUNCTION__, content)
#define TIMER_FUNC_DIY(content,funcName) FuncTimer __timer__(funcName, content)
#define TIMER_FUNC_EX(content, startTimeNs, parentName) FuncTimer __timer__(__FUNCTION__, content, FuncTimer::generateNewTraceContext(startTimeNs, parentName))
#endif
#else 
#define TIMER_FUNC(content) 
#define TIMER_FUNC_DIY(content,funcName) 
#define TIMER_FUNC_EX(content, startTimeNs, parentName) 
#endif

#endif // FUNCTIMER_H

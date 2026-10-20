#pragma once

#include <windows.h>
#include <cwchar>
#include <cstdint>

// 首键路径的诊断跟踪。在启动承载 TSF 与辅助进程的应用之前，
// 设置 CORAL_INPUT_TIMING=1 启用。
// OutputDebugString 使 IPC 库独立于文件日志器。
namespace coral::input_timing
{
inline bool enabled()
{
    static const bool value = [] {
        wchar_t flag[2] = {};
        return ::GetEnvironmentVariableW(L"CORAL_INPUT_TIMING", flag, 2) == 1 && flag[0] == L'1';
    }();
    return value;
}

inline std::uint64_t nowUs()
{
    if (!enabled()) return 0;
    LARGE_INTEGER counter{}, frequency{};
    ::QueryPerformanceCounter(&counter);
    ::QueryPerformanceFrequency(&frequency);
    return static_cast<std::uint64_t>(counter.QuadPart / frequency.QuadPart) * 1000000ULL +
        static_cast<std::uint64_t>(counter.QuadPart % frequency.QuadPart) * 1000000ULL /
        static_cast<std::uint64_t>(frequency.QuadPart);
}

inline void mark(const wchar_t* stage, std::uint64_t startUs,
    std::uint64_t request_id = 0, std::uint64_t result = 0)
{
    if (!enabled()) return;
    const std::uint64_t endUs = nowUs();
    wchar_t line[320] = {};
    swprintf_s(line, L"[coral_input_timing] qpc_us=%llu pid=%lu tid=%lu stage=%s elapsed_us=%llu id=%llu result=%llu\n",
        static_cast<unsigned long long>(endUs), ::GetCurrentProcessId(), ::GetCurrentThreadId(),
        stage, static_cast<unsigned long long>(startUs ? endUs - startUs : 0),
        static_cast<unsigned long long>(request_id), static_cast<unsigned long long>(result));
    ::OutputDebugStringW(line);
}

class Scope
{
public:
    Scope(const wchar_t* stage, std::uint64_t request_id = 0)
        : stage_(stage), requestId_(request_id), startUs_(nowUs()) {}
    ~Scope() { mark(stage_, startUs_, requestId_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    const wchar_t* stage_;
    std::uint64_t requestId_;
    std::uint64_t startUs_;
};
}

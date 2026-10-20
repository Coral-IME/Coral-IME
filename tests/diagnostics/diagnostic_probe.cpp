#include "crash_reporter.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {
LONG WINAPI sentinel_filter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
}
int wmain(int argc, wchar_t** argv) {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc > 1 && std::wstring_view(argv[1]) == L"--coral-crash=1") {
        wchar_t marker[32768]{};
        const auto length = ::GetEnvironmentVariableW(L"CORAL_DIAGNOSTIC_PROBE_MARKER", marker, 32768);
        if (!length || length >= 32768) return 2;
        { std::ofstream output{std::filesystem::path(marker)}; output << ::GetCurrentProcessId(); }
        ::Sleep(2500);
        return 0;
    }
    if (argc < 2) return 2;
    const std::wstring_view mode(argv[1]);
    if (mode == L"snapshot") {
        std::cout << ::GetCurrentProcessId() << std::endl;
        std::string line; std::getline(std::cin, line);
        return 0;
    }
    if (argc != 3) return 2;
    if (mode == L"tsf_filter") {
        const auto previous = ::SetUnhandledExceptionFilter(sentinel_filter);
        const auto module = ::LoadLibraryExW(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) { ::SetUnhandledExceptionFilter(previous); return 3; }
        const bool loaded = ::SetUnhandledExceptionFilter(sentinel_filter) == sentinel_filter;
        const bool unloaded = ::FreeLibrary(module) != FALSE;
        const bool preserved = ::SetUnhandledExceptionFilter(previous) == sentinel_filter;
        return loaded && unloaded && preserved ? 0 : 1;
    }
    using namespace coral::diagnostics;
    if (mode == L"missing") {
        const auto previous = ::SetUnhandledExceptionFilter(sentinel_filter);
        const bool rejected = !install_crash_reporter(CrashModule::kSettings, argv[2]);
        const bool preserved = ::SetUnhandledExceptionFilter(previous) == sentinel_filter;
        return rejected && preserved ? 0 : 1;
    }
    const bool installed = mode == L"adjacent" ? install_crash_reporter(CrashModule::kSettings)
        : install_crash_reporter(CrashModule::kSettings, argv[2], mode == L"slow" ? 100 : 5000);
    if (!installed) return 4;
    if (mode == L"twice") return !install_crash_reporter(CrashModule::kUpdater, argv[2]) ? 0 : 1;
    const auto fault = [] { ::RaiseException(0xE04C0101, EXCEPTION_NONCONTINUABLE, 0, nullptr); };
    if (mode == L"worker") { std::thread thread(fault); thread.join(); }
    else fault();
    return 5;
}

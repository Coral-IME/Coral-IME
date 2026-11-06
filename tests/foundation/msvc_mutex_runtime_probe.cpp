#include <windows.h>

#include <cstdio>
#include <mutex>
#include <thread>

namespace {
// 覆盖静态存储与对象成员两种初始化路径。
std::mutex global_mutex;

struct Counter {
    std::mutex mutex;
    int value = 0;

    void increment()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++value;
    }
};
}

int wmain(int argc, wchar_t** argv)
{
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#ifdef _DEBUG
    const HMODULE runtime = ::GetModuleHandleW(L"MSVCP140D.dll");
#else
    const HMODULE runtime = ::GetModuleHandleW(L"MSVCP140.dll");
#endif
    wchar_t runtime_path[32768]{};
    const DWORD length = ::GetModuleFileNameW(runtime, runtime_path, 32768);
    if (!runtime || !length || length == 32768)
        return 1;
    std::printf("compiler=%d runtime=%ls\n", _MSC_VER, runtime_path);
    std::fflush(stdout);
    if (argc > 1 && ::_wcsicmp(runtime_path, argv[1]) != 0)
        return 2;

    {
        std::lock_guard<std::mutex> lock(global_mutex);
    }
    Counter counter;
    std::thread worker([&counter] {
        for (int i = 0; i < 100; ++i)
            counter.increment();
    });
    for (int i = 0; i < 100; ++i)
        counter.increment();
    worker.join();
    if (counter.value != 200)
        return 3;

    std::recursive_mutex recursive;
    std::lock_guard<std::recursive_mutex> first(recursive);
    std::lock_guard<std::recursive_mutex> second(recursive);
    std::puts("mutex runtime checks passed");
    return 0;
}

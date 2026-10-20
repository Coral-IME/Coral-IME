#include "shared_preferences.h"
#include "test_environment.h"
#include "trace_log.h"
#include "system_mutex_lease.hpp"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

namespace boost::interprocess::ipcdetail {
    template<class A, class B>
    inline A get_rounded_size(A size, B alignment) {
        return ((size - 1) / static_cast<A>(alignment) + 1) * static_cast<A>(alignment);
    }
}
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/indexes/iset_index.hpp>
#include <boost/interprocess/offset_ptr.hpp>

namespace {
namespace bip = boost::interprocess;
using Memory = bip::basic_managed_windows_shared_memory<char,
    bip::rbtree_best_fit<bip::mutex_family, bip::offset_ptr<void, std::int64_t, std::uint64_t>>,
    bip::iset_index>;
// 独立的 V4 字节夹具，刻意不引入实现结构体；同时约束既有跨架构布局。
constexpr std::size_t kRootSize = 335936;
constexpr std::size_t kBufferOffset = 64;
constexpr std::size_t kBufferSize = 167936;
constexpr std::size_t kSlotSize = 328;
constexpr std::size_t kSlotCount = 512;
int failures = 0;

void check(bool condition, const char* name) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    if (!condition) ++failures;
}

void set_flag(char* root, std::size_t offset, std::uint32_t value) {
    reinterpret_cast<std::atomic<std::uint32_t>*>(root + offset)->store(value);
}

void rejected(SharedMemoryConfig& config) {
    check(config.getConfig("probe").empty(), "invalid data rejects single read");
    check(config.getAllConfigMap().empty(), "invalid data rejects snapshot read");
    check(!config.setConfig("probe", "new"), "invalid data rejects single write");
    check(!config.setConfigs({{"probe", "new"}}), "invalid data rejects batch write");
    check(!config.initializeAndSetConfigs({{"probe", "new"}}), "invalid data rejects initialized batch write");
    SharedMemoryConfig newcomer;
    check(!newcomer.initialize(), "new connection rejects invalid existing data");
    SharedMemoryConfig batch_newcomer;
    check(!batch_newcomer.initializeAndSetConfigs({{"probe", "new"}}), "new batch connection rejects invalid data");
}

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE handle = nullptr) : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

std::wstring object_name(const char* base) {
    const auto name = coral::testing::objectName(base);
    return std::wstring(name.begin(), name.end());
}

int hold_config_mutex() {
    Handle mutex(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
        object_name("CORALPinyinConfigSharedMemoryMutexV4").c_str()));
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, object_name("CoralConfigLockReady").c_str()));
    Handle release(OpenEventW(SYNCHRONIZE, FALSE, object_name("CoralConfigLockRelease").c_str()));
    if (!mutex.value || !ready.value || !release.value || WaitForSingleObject(mutex.value, 1000) != WAIT_OBJECT_0) return 2;
    SetEvent(ready.value);
    // 旧版本无限等待时也能自行退出，避免失败夹具遗留持锁进程。
    const auto result = WaitForSingleObject(release.value, 5000);
    ReleaseMutex(mutex.value);
    return result == WAIT_OBJECT_0 ? 0 : 3;
}

void check_lock_contention(SharedMemoryConfig& writer, SharedMemoryConfig& reader, char* root) {
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, object_name("CoralConfigLockReady").c_str()));
    Handle release(CreateEventW(nullptr, TRUE, FALSE, object_name("CoralConfigLockRelease").c_str()));
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!ready.value || !release.value || length == 0 || length >= 32768) {
        check(false, "contention fixture initialized");
        return;
    }
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --hold-config-mutex";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        check(false, "mutex holder started");
        return;
    }
    Handle child(process.hProcess), thread(process.hThread);
    if (WaitForSingleObject(ready.value, 2000) != WAIT_OBJECT_0) {
        check(false, "child acquired configuration mutex");
        SetEvent(release.value);
        WaitForSingleObject(child.value, 6000);
        return;
    }
    const std::string before(root, kRootSize);
    auto bounded_failure = [&](auto&& operation, const char* message) {
        const auto start = std::chrono::steady_clock::now();
        const bool result = operation();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check(!result && elapsed < std::chrono::seconds(1), message);
    };
    SharedMemoryConfig newcomer, batch_newcomer;
    bounded_failure([&] { return newcomer.initialize(); }, "initialization fails promptly under cross-process contention");
    bounded_failure([&] { return batch_newcomer.initializeAndSetConfigs({{"probe", "blocked"}}); }, "initial batch fails promptly under contention");
    bounded_failure([&] { return writer.setConfig("probe", "blocked"); }, "single write fails promptly under contention");
    bounded_failure([&] { return writer.setConfigs({{"probe", "blocked"}}); }, "batch write fails promptly under contention");
    bounded_failure([&] { return writer.initializeAndSetConfigs({{"probe", "blocked"}}); }, "initialized batch fails promptly under contention");
    DWORD handles_before = 0, handles_after = 0;
    check(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != FALSE, "handle baseline available");
    for (int i = 0; i < 4; ++i) bounded_failure([&] { return newcomer.initialize(); }, "repeated initialization remains bounded");
    check(GetProcessHandleCount(GetCurrentProcess(), &handles_after) && handles_after == handles_before, "failed initialization retries do not leak mutex handles");
    const auto start = std::chrono::steady_clock::now();
    check(reader.getConfig("probe") == "batch" && !reader.getAllConfigMap().empty() &&
        std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "readers remain usable while configuration mutex is held");
    check(std::memcmp(root, before.data(), kRootSize) == 0, "timeouts preserve sequence, index and both buffers");
    check(WaitForSingleObject(child.value, 0) == WAIT_TIMEOUT, "holder still owns mutex until explicitly released");
    SetEvent(release.value);
    check(WaitForSingleObject(child.value, 6000) == WAIT_OBJECT_0, "holder exits after release");
    DWORD exit_code = 1;
    check(GetExitCodeProcess(child.value, &exit_code) && exit_code == 0, "mutex holder released normally");
    check(newcomer.initialize() && batch_newcomer.initializeAndSetConfigs({{"probe", "retry"}}), "timed-out instances can initialize after release");
    check(writer.setConfig("probe", "retry") && writer.setConfigs({{"probe", "batch"}}) &&
        reader.getConfig("probe") == "batch", "writes recover after mutex release");
    {
        ScopedLock invalid(nullptr, 0);
        check(!invalid.isAcquired(), "null mutex handle is rejected");
        ScopedLock invalid_value(INVALID_HANDLE_VALUE, 0);
        check(!invalid_value.isAcquired(), "invalid mutex handle is rejected");
    }
    Handle mutex(OpenMutexW(MUTEX_MODIFY_STATE, FALSE, object_name("CORALPinyinConfigSharedMemoryMutexV4").c_str()));
    check(mutex.value != nullptr, "modify-only mutex handle opened");
    ScopedLock failed_wait(mutex.value, 0);
    check(!failed_wait.isAcquired(), "wait failure without synchronize access is rejected");
}
}

int main(int argc, char** argv) {
    const char* scope = std::getenv("CORAL_TEST_NAMESPACE");
    if (!scope || !*scope) return 2;
    if (argc == 2 && std::string(argv[1]) == "--hold-config-mutex") return hold_config_mutex();
    const auto name = coral::testing::objectName("CORALPinyinConfigSharedMemoryV4");
    {
        SharedMemoryConfig writer;
        if (!writer.initializeAndSetConfigs({{"probe", "old"}, {"other", "two"}})) {
            std::cerr << "Cannot create isolated shared preferences\n";
            return 2;
        }
        SharedMemoryConfig reader;
        check(reader.initialize() && reader.getConfig("probe") == "old", "second mapping reads configuration");
        check(writer.setConfig("probe", "new") && reader.getConfig("probe") == "new", "single write publishes value");
        check(writer.setConfigs({{"probe", "batch"}}) && reader.getAllConfigMap().at("probe") == "batch", "batch publishes snapshot");
        check(writer.setConfig(std::string(63, 'k'), std::string(255, 'v')) &&
              reader.getConfig(std::string(63, 'k')) == std::string(255, 'v'), "maximum terminated lengths work");
        check(!writer.setConfig(std::string(64, 'k'), "x") && !writer.setConfig("x", std::string(256, 'v')), "oversized input rejected");
        Memory mapping(bip::open_only, name.c_str());
        auto allocation = mapping.find<char>("ConfigDataRootCrossArchV4");
        if (!allocation.first || allocation.second != kRootSize) return 3;
        char* root = allocation.first;
        check_lock_contention(writer, reader, root);
        const auto active = reinterpret_cast<std::atomic<std::uint32_t>*>(root + 4)->load();
        const auto sequence = reinterpret_cast<std::atomic<std::uint32_t>*>(root)->load();
        for (auto index : {2u, 5u, 6u, std::numeric_limits<std::uint32_t>::max()}) {
            set_flag(root, 4, index);
            rejected(reader);
            check(reinterpret_cast<std::atomic<std::uint32_t>*>(root)->load() == sequence, "failed write preserves sequence");
        }
        set_flag(root, 4, active);
        set_flag(root, 0, sequence | 1u);
        rejected(reader);
        set_flag(root, 0, sequence);
        set_flag(root, 8, 2);
        rejected(reader);
        set_flag(root, 8, 1);
        char* buffer = root + kBufferOffset + active * kBufferSize;
        std::string saved(buffer, kBufferSize);
        for (int kind = 0; kind < 3; ++kind) {
            // 全部槽位放入恶意字节，保证单键探测也必然命中损坏记录。
            for (std::size_t i = 0; i < kSlotCount; ++i) {
                char* slot = buffer + i * kSlotSize;
                std::memset(slot, 0, kSlotSize);
                slot[0] = kind == 2 ? 2 : 1;
                if (kind == 0) std::memset(slot + 1, 'k', 64);
                if (kind == 1) std::memset(slot + 65, 'v', 256);
            }
            rejected(reader);
        }
        std::memcpy(buffer, saved.data(), saved.size());
        check(reader.getConfig("probe") == "batch", "restored data is readable");
        check(writer.setConfig("probe", "recovered") && reader.getConfig("probe") == "recovered", "valid writes still work after rejection");
        std::atomic<bool> stop{false};
        std::thread mutator([&] {
            while (!stop.load()) {
                set_flag(root, 4, 6);
                set_flag(root, 4, std::numeric_limits<std::uint32_t>::max());
                set_flag(root, 4, active);
            }
        });
        for (int i = 0; i < 64; ++i) {
            reader.getConfig("probe");
            reader.getAllConfigMap();
            writer.setConfig("probe", "raced");
            writer.setConfigs({{"probe", "raced"}});
        }
        stop.store(true);
        mutator.join();
        set_flag(root, 4, active);
        check(writer.setConfig("probe", "stable") && reader.getConfig("probe") == "stable", "concurrent malicious index changes stay bounded");
    }
    {
        // 管理段合法但同名根对象的尺寸错误，不能按结构体直接解引用。
        Memory malformed(bip::create_only, name.c_str(), 1024 * 1024);
        malformed.construct<char>("ConfigDataRootCrossArchV4")[16](0);
        SharedMemoryConfig config;
        check(!config.initialize(), "wrong allocation size rejected");
    }
    return failures ? 1 : 0;
}

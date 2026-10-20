#include "shared_preferences.h"
#include "test_environment.h"
#include <Windows.h>
#include <atomic>
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
}

int main() {
    const char* scope = std::getenv("CORAL_TEST_NAMESPACE");
    if (!scope || !*scope) return 2;
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

#include "verified_resource.h"
#include <filesystem>
#include <fstream>
#include <future>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using coral::storage::VerifiedResource;
using coral::storage::ResourceAccess;
constexpr auto kCreate = ResourceAccess::kCreateOrOpen;
constexpr auto kOpen = ResourceAccess::kOpenExisting;
constexpr const char* kDigest = "900150983cd24fb0d6963f7d28e17f72";
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
    require(static_cast<bool>(file), "fixture write");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        write(root / "data", "abc"); write(root / "empty", ""); write(root / "other", "xyz");
#ifdef _WIN32
        const std::wstring prefix = L"Local\\coral-resource-test-" + std::to_wstring(GetCurrentProcessId()) + L"-";
#else
        const std::wstring prefix = root.wstring();
#endif
        VerifiedResource resource;
        require(!resource.load(prefix + L"missing", root / "missing", "", kCreate), "missing source");
        require(!resource.load(prefix + L"empty", root / "empty", "", kCreate), "empty source");
        require(!resource.data() && resource.size() == 0, "failed load exposes no data");
        require(!resource.load(prefix + L"checksum", root / "data", "bad", kCreate), "checksum rejection");
        require(resource.load(prefix + L"checksum", root / "data", kDigest, kCreate), "retry after checksum failure");
        require(resource.size() == 3 && std::string(static_cast<const char*>(resource.data()), 3) == "abc", "payload");
        const void* original = resource.data();
        require(resource.load(prefix + L"checksum", root / "data", kDigest, kOpen) && original == resource.data(), "same request is idempotent");
        require(!resource.load(prefix + L"changed", root / "other", "", kCreate), "reject different identity");
        require(!resource.load(prefix + L"checksum", root / "data", "different", kCreate), "reject different integrity request");
        require(original == resource.data() && resource.size() == 3, "failed replacement retains existing view");
#ifdef _WIN32
        VerifiedResource peer;
        require(peer.load(prefix + L"checksum", root / "absent", kDigest, kOpen), "open shared payload without source");
        resource.reset(); resource.reset();
        require(!resource.data() && resource.size() == 0 && peer.size() == 3, "peer owns independent lease");
        peer.reset();
        require(!resource.load(prefix + L"checksum", root / "data", kDigest, kOpen), "released shared object no longer exists");
        HANDLE mutex = CreateMutexW(nullptr, FALSE, (prefix + L"corrupt_Mutex").c_str());
        HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 8,
            (prefix + L"corruptSharedMemory").c_str());
        require(mutex && section, "malformed shared fixture");
        void* view = MapViewOfFile(section, FILE_MAP_WRITE, 0, 0, 8);
        require(view != nullptr, "malformed header view");
        *static_cast<std::uint64_t*>(view) = ~std::uint64_t{0};
        UnmapViewOfFile(view);
        require(!resource.load(prefix + L"corrupt", root / "data", "", kOpen), "reject overflowing shared header");
        CloseHandle(section); CloseHandle(mutex);

        HANDLE abandoned = CreateMutexW(nullptr, FALSE, (prefix + L"abandoned_Mutex").c_str());
        require(abandoned != nullptr, "abandoned mutex fixture");
        std::thread crashed([&] { require(WaitForSingleObject(abandoned, 1000) == WAIT_OBJECT_0, "fixture mutex"); });
        crashed.join();
        require(!resource.load(prefix + L"abandoned", root / "data", "", kCreate), "reject abandoned publisher");
        CloseHandle(abandoned);
        HANDLE busy = CreateMutexW(nullptr, FALSE, (prefix + L"busy_Mutex").c_str());
        require(busy != nullptr, "busy mutex fixture");
        std::promise<void> locked, unlock;
        auto unblock = unlock.get_future().share();
        std::thread owner([&] {
            WaitForSingleObject(busy, INFINITE);
            locked.set_value(); unblock.wait(); ReleaseMutex(busy);
        });
        locked.get_future().wait();
        const auto started = std::chrono::steady_clock::now();
        const bool busy_loaded = resource.load(prefix + L"busy", root / "data", "", kOpen);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        unlock.set_value(); owner.join(); CloseHandle(busy);
        require(!busy_loaded && elapsed < std::chrono::seconds(8), "bounded wait for publisher lock");
        DWORD handles_before = 0, handles_after = 0;
        require(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != 0, "handle baseline");
        for (int i = 0; i < 80; ++i) {
            require(!resource.load(prefix + L"leak", root / "data", "bad", kCreate), "repeat failure");
            require(resource.load(prefix + L"leak", root / "data", kDigest, kCreate), "repeat recovery");
            resource.reset();
        }
        require(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != 0 && handles_after <= handles_before + 1,
            "load/failure/reset do not accumulate handles");
#else
        resource.reset(); resource.reset();
#endif
        std::vector<std::future<bool>> loads;
        for (int i = 0; i < 8; ++i)
            loads.push_back(std::async(std::launch::async, [&] { return resource.load(prefix + L"concurrent", root / "data", kDigest, kCreate); }));
        for (auto& load : loads) require(load.get(), "concurrent equivalent loads");
        resource.reset();
        const std::string large(2 * 1024 * 1024 + 3, 'q');
        write(root / "large", large);
        require(resource.load(prefix + L"large", root / "large", "", kCreate), "multi-chunk source");
        require(resource.size() == large.size() && std::string(static_cast<const char*>(resource.data()), resource.size()) == large,
            "multi-chunk bytes intact");
        std::cout << "Resource ownership, integrity, sharing and failure contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

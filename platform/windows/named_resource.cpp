#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include "resource_mapping_backend.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

namespace coral::storage::detail {
namespace {

class Handle final {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

class View final {
public:
    explicit View(void* value = nullptr) : value_(value) {}
    ~View() { if (value_) UnmapViewOfFile(value_); }
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    void* get() const { return value_; }
    void reset(void* value) { if (value_) UnmapViewOfFile(value_); value_ = value; }
private:
    void* value_;
};

struct Lease {
    Lease(Handle&& section, Handle&& mutex) : section(std::move(section)), mutex(std::move(mutex)) {}
    Handle section;
    Handle mutex;
    std::unique_ptr<View> view;
};

struct Security {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    Security() {
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"S:(ML;;;;;LW)D:(A;;GA;;;WD)(A;;GA;;;AC)", SDDL_REVISION_1, &descriptor, nullptr))
            attributes.lpSecurityDescriptor = descriptor;
    }
    ~Security() { if (descriptor) LocalFree(descriptor); }
};

struct Unlock {
    HANDLE mutex;
    ~Unlock() { ReleaseMutex(mutex); }
};

constexpr std::uint64_t kHeaderBytes = sizeof(std::uint64_t);
constexpr DWORD kLockTimeoutMs = 5000;

std::shared_ptr<Lease> make_lease(Handle&& section, HANDLE mutex) {
    HANDLE retained = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), mutex, GetCurrentProcess(), &retained,
        0, FALSE, DUPLICATE_SAME_ACCESS)) return {};
    Handle retained_mutex(retained);
    return std::make_shared<Lease>(std::move(section), std::move(retained_mutex));
}

std::optional<Mapping> attach(const std::shared_ptr<Lease>& lease, const std::string& checksum) {
    View header(MapViewOfFile(lease->section.get(), FILE_MAP_READ, 0, 0, kHeaderBytes));
    if (!header.get()) return std::nullopt;
    std::uint64_t size = 0;
    std::memcpy(&size, header.get(), kHeaderBytes);
    if (size == 0 || size > (std::numeric_limits<std::size_t>::max)() - kHeaderBytes)
        return std::nullopt;
    lease->view = std::make_unique<View>();
    lease->view->reset(MapViewOfFile(lease->section.get(), FILE_MAP_READ,
        0, 0, static_cast<std::size_t>(size + kHeaderBytes)));
    if (!lease->view->get()) return std::nullopt;
    const auto* data = static_cast<const char*>(lease->view->get()) + kHeaderBytes;
    if (!matches_checksum(data, static_cast<std::size_t>(size), checksum)) return std::nullopt;
    return Mapping{lease, data, static_cast<std::size_t>(size)};
}

} // namespace

std::optional<Mapping> open_mapping(const std::wstring& name, const std::filesystem::path& path,
    const std::string& checksum, ResourceAccess access) {
    const auto mutex_name = name + L"_Mutex";
    const auto mapping_name = name + L"SharedMemory";
    Security security;
    const bool create = access == ResourceAccess::kCreateOrOpen;
    if (create && !security.descriptor) return std::nullopt;
    Handle mutex(create ? CreateMutexW(&security.attributes, FALSE, mutex_name.c_str())
                        : OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex_name.c_str()));
    if (!mutex) return std::nullopt;
    const DWORD wait = WaitForSingleObject(mutex.get(), kLockTimeoutMs);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return std::nullopt;
    Unlock unlock{mutex.get()};
    // 已崩溃的发布者可能留下不完整的载荷。绝不在原地修复它。
    if (wait == WAIT_ABANDONED) return std::nullopt;

    Handle opened(OpenFileMappingW(FILE_MAP_READ, FALSE, mapping_name.c_str()));
    if (opened) {
        auto lease = make_lease(std::move(opened), mutex.get());
        return lease ? attach(lease, checksum) : std::nullopt;
    }
    if (!create || GetLastError() != ERROR_FILE_NOT_FOUND) return std::nullopt;

    Handle source(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    LARGE_INTEGER length{};
    if (!source || !GetFileSizeEx(source.get(), &length) || length.QuadPart <= 0)
        return std::nullopt;
    const auto size = static_cast<std::uint64_t>(length.QuadPart);
    if (size > (std::numeric_limits<std::size_t>::max)() - kHeaderBytes) return std::nullopt;
    const std::uint64_t total = size + kHeaderBytes;
    Handle section(CreateFileMappingW(INVALID_HANDLE_VALUE, &security.attributes, PAGE_READWRITE,
        static_cast<DWORD>(total >> 32), static_cast<DWORD>(total), mapping_name.c_str()));
    const DWORD created = GetLastError();
    if (!section) return std::nullopt;
    auto lease = make_lease(std::move(section), mutex.get());
    if (!lease) return std::nullopt;
    if (created == ERROR_ALREADY_EXISTS) return attach(lease, checksum);
    {
        View writable(MapViewOfFile(lease->section.get(), FILE_MAP_WRITE, 0, 0, static_cast<std::size_t>(total)));
        if (!writable.get()) return std::nullopt;
        auto* bytes = static_cast<char*>(writable.get()) + kHeaderBytes;
        std::uint64_t copied = 0;
        while (copied < size) {
            const DWORD requested = static_cast<DWORD>((std::min)(size - copied, std::uint64_t{1 << 20}));
            DWORD received = 0;
            if (!ReadFile(source.get(), bytes + static_cast<std::size_t>(copied), requested, &received, nullptr)
                || received == 0) return std::nullopt;
            copied += received;
        }
        if (!matches_checksum(bytes, static_cast<std::size_t>(size), checksum)) return std::nullopt;
        // 只有在读取并检查完每个载荷字节之后才发布长度。
        std::memcpy(writable.get(), &size, kHeaderBytes);
    }
    // 这个新建载荷已在发布者锁下检查过。
    return attach(lease, "");
}

} // 命名空间 coral::storage::detail 结束

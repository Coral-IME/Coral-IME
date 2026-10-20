#include "package_acceptance.h"
#include "package_version.h"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace coral::updates {
namespace {
class OwnedFile {
public:
    explicit OwnedFile(HANDLE value) : value_(value) {}
    ~OwnedFile() { if (value_ != INVALID_HANDLE_VALUE) ::CloseHandle(value_); }
    OwnedFile(const OwnedFile&) = delete;
    OwnedFile& operator=(const OwnedFile&) = delete;
    HANDLE get() const { return value_; }
private:
    HANDLE value_;
};
class Sha256Context {
public:
    ~Sha256Context() {
        if (hash_) ::BCryptDestroyHash(hash_);
        if (algorithm_) ::BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    Sha256Context() = default;
    Sha256Context(const Sha256Context&) = delete;
    Sha256Context& operator=(const Sha256Context&) = delete;
    bool start() {
        return ::BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
            && ::BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    bool add(UCHAR* bytes, ULONG count) { return ::BCryptHashData(hash_, bytes, count, 0) >= 0; }
    bool finish(std::array<std::uint8_t, 32>& digest) {
        return ::BCryptFinishHash(hash_, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    }
private:
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
};
bool valid_file_path(const std::filesystem::path& path) {
    const auto text = path.wstring();
    return path.is_absolute() && !text.empty() && text.size() < 32700 && text.find(L'\0') == std::wstring::npos;
}
PackageStatus verify_handle(HANDLE file, const PackageFingerprint& expected, const std::function<bool()>& cancelled) {
    if (cancelled && cancelled()) return PackageStatus::kCancelled;
    if (file == INVALID_HANDLE_VALUE) return PackageStatus::kOpenFailed;
    if (::GetFileType(file) != FILE_TYPE_DISK) return PackageStatus::kInvalidRequest;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(file, &size)) return PackageStatus::kReadFailed;
    if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) != expected.byte_count) return PackageStatus::kSizeMismatch;
    Sha256Context context;
    if (!context.start()) return PackageStatus::kHashFailed;
    UCHAR buffer[65536];
    std::uint64_t total = 0;
    for (;;) {
        if (cancelled && cancelled()) return PackageStatus::kCancelled;
        DWORD count = 0;
        if (!::ReadFile(file, buffer, sizeof(buffer), &count, nullptr)) return PackageStatus::kReadFailed;
        if (!count) break;
        total += count;
        if (total > expected.byte_count) return PackageStatus::kSizeMismatch;
        if (!context.add(buffer, count)) return PackageStatus::kHashFailed;
    }
    if (total != expected.byte_count) return PackageStatus::kSizeMismatch;
    std::array<std::uint8_t, 32> digest{};
    if (!context.finish(digest)) return PackageStatus::kHashFailed;
    return digest == expected.sha256 ? PackageStatus::kReady : PackageStatus::kHashMismatch;
}
bool valid_fingerprint(const PackageFingerprint& expected) {
    return expected.byte_count && expected.byte_count <= kMaximumPackageBytes;
}
}

PackageStatus verify_package(const std::filesystem::path& file, const PackageFingerprint& expected, const std::function<bool()>& cancelled) {
    if (!valid_file_path(file) || !valid_fingerprint(expected)) return PackageStatus::kInvalidRequest;
    OwnedFile input(::CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    return verify_handle(input.get(), expected, cancelled);
}
PackageStatus publish_package(const std::filesystem::path& pending, const std::filesystem::path& ready,
    const PackageFingerprint& expected, const std::function<bool()>& cancelled) {
    if (!valid_file_path(pending) || !valid_file_path(ready) || !valid_fingerprint(expected)
        || pending.lexically_normal().parent_path() != ready.lexically_normal().parent_path()
        || pending.lexically_normal() == ready.lexically_normal()) return PackageStatus::kInvalidRequest;
    OwnedFile input(::CreateFileW(pending.c_str(), GENERIC_READ | DELETE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    const auto status = verify_handle(input.get(), expected, cancelled);
    if (status != PackageStatus::kReady) return status;
    const auto name = ready.wstring();
    const auto bytes = name.size() * sizeof(wchar_t);
    std::vector<BYTE> storage(sizeof(FILE_RENAME_INFO) + bytes, 0);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(bytes);
    std::memcpy(rename->FileName, name.data(), bytes);
    if (cancelled && cancelled()) return PackageStatus::kCancelled;
    return ::SetFileInformationByHandle(input.get(), FileRenameInfo, rename, static_cast<DWORD>(storage.size()))
        ? PackageStatus::kReady : PackageStatus::kPublishFailed;
}
std::filesystem::path package_cache_directory() {
    std::vector<wchar_t> buffer(32768);
    const auto length = ::GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (!length || length >= buffer.size()) return {};
    const auto path = std::filesystem::path(std::wstring(buffer.data(), length)) / L"CoralPinyin" / L"updates";
    return path.is_absolute() ? path : std::filesystem::path{};
}
std::filesystem::path ready_package_path(const ReleaseVersion& version) {
    const auto directory = package_cache_directory();
    return directory.empty() ? std::filesystem::path{} : directory / (L"package_" +
        std::filesystem::u8path(format_release_version(version)).wstring() + L".exe");
}
std::filesystem::path reserve_pending_package() {
    const auto directory = package_cache_directory();
    if (directory.empty()) return {};
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return {};
    constexpr wchar_t kHex[] = L"0123456789abcdef";
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        UCHAR random[16]{};
        if (::BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return {};
        std::wstring name = L"pending_";
        for (const auto byte : random) { name += kHex[byte >> 4]; name += kHex[byte & 15]; }
        const auto path = directory / (name + L".tmp");
        OwnedFile file(::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() != INVALID_HANDLE_VALUE) return path;
        if (::GetLastError() != ERROR_FILE_EXISTS) return {};
    }
    return {};
}
std::optional<ReleaseVersion> read_running_package_version() {
    for (DWORD capacity = 512; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), capacity);
        if (!length) return std::nullopt;
        if (length == capacity) continue;
        ReleaseVersion version;
        const auto path = std::filesystem::path(std::wstring(buffer.data(), length));
        if (!parse_release_version(config::readInstalledAppVersion(path.parent_path()), version)) return std::nullopt;
        return version;
    }
    return std::nullopt;
}
}

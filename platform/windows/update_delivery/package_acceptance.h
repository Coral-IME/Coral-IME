#pragma once
#include "coral/updates/release_metadata.h"
#include <filesystem>
#include <optional>
#include <functional>

namespace coral::updates {
enum class PackageStatus { kReady, kInvalidRequest, kOpenFailed, kReadFailed, kSizeMismatch, kHashFailed, kHashMismatch, kPublishFailed, kCancelled };
PackageStatus verify_package(const std::filesystem::path& file, const PackageFingerprint& expected,
    const std::function<bool()>& cancelled = {});
// 校验并重命名同一个不可写/不可删除的文件句柄；绝不替换目标文件。
PackageStatus publish_package(const std::filesystem::path& pending, const std::filesystem::path& ready,
    const PackageFingerprint& expected, const std::function<bool()>& cancelled = {});
std::filesystem::path package_cache_directory();
std::filesystem::path ready_package_path(const ReleaseVersion& version);
std::filesystem::path reserve_pending_package();
std::optional<ReleaseVersion> read_running_package_version();
}

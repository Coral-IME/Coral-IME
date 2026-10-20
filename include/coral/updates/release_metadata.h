#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace coral::updates {
struct ReleaseVersion { std::array<std::uint16_t, 4> components{}; };
struct PackageFingerprint {
    std::uint64_t byte_count = 0;
    std::array<std::uint8_t, 32> sha256{};
};
struct ReleaseMetadata {
    std::uint32_t release_id = 0;
    ReleaseVersion version;
    PackageFingerprint package;
    std::string description;
};
inline constexpr std::uint64_t kMaximumPackageBytes = 4ull * 1024 * 1024 * 1024;
inline constexpr std::size_t kMaximumMetadataBytes = 65536;
bool parse_release_version(std::string_view text, ReleaseVersion& result) noexcept;
std::string format_release_version(const ReleaseVersion& version);
bool is_newer_version(const ReleaseVersion& candidate, const ReleaseVersion& installed) noexcept;
// 保留服务端字段名；必需 id/version/fileSize/fileSha256，可选 description。
// 必须是合法 UTF-8、有界文本，无重复字段，失败时不输出半成品。
bool parse_release_metadata(std::string_view json, ReleaseMetadata& result);
bool parse_download_location(std::string_view json, std::string& result);
}

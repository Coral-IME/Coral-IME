#pragma once
#include "coral/updates/download_wire.h"
#include <filesystem>
#include <functional>

namespace coral::updates {
struct LocationReply { DownloadError error = DownloadError::kNone; std::string body; };
class DownloadActions {
public:
    virtual ~DownloadActions() = default;
    virtual bool cancelled() = 0;
    virtual bool report(const DownloadProgress& progress) = 0;
    virtual LocationReply locate(std::uint32_t release_id) = 0;
    virtual std::filesystem::path reserve_pending() = 0;
    // 同步：回调在返回前结束，文件在校验/发布前关闭。
    virtual DownloadError download(const std::string& url, const std::filesystem::path& pending,
        std::uint64_t limit, const std::function<bool(std::uint32_t)>& progress) = 0;
    virtual bool verify(const std::filesystem::path& pending, const PackageFingerprint& package) = 0;
    virtual bool publish(const std::filesystem::path& pending, const ReleaseMetadata& release) = 0;
    virtual void discard(const std::filesystem::path& pending) noexcept = 0;
};
// kReady 表示已提交，包括提交后的取消。不查询最新版本。
DownloadError execute_download(const DownloadRequest& request, DownloadActions& actions);
}

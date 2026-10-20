#pragma once
#include "coral/updates/download_job.h"
#include "update_process.h"
#include "update_http.h"

namespace coral::updates {
class AgentActions final : public DownloadActions {
public:
    explicit AgentActions(InheritedUpdateChannel& channel) : channel_(channel) {}
    bool cancelled() override;
    bool report(const DownloadProgress& progress) override;
    LocationReply locate(std::uint32_t release_id) override;
    std::filesystem::path reserve_pending() override;
    DownloadError download(const std::string& url, const std::filesystem::path& pending, std::uint64_t limit,
        const std::function<bool(std::uint32_t)>& progress) override;
    bool verify(const std::filesystem::path& pending, const PackageFingerprint& package) override;
    bool publish(const std::filesystem::path& pending, const ReleaseMetadata& release) override;
    void discard(const std::filesystem::path& pending) noexcept override;
private:
    InheritedUpdateChannel& channel_;
    UpdateHttp http_;
};
}

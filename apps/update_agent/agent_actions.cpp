#include "agent_actions.h"
#include "package_acceptance.h"
#include "coral/updates/update_service.h"

namespace coral::updates {
bool AgentActions::cancelled() { return channel_.cancelled(); }
bool AgentActions::report(const DownloadProgress& progress) { return channel_.report(progress); }
LocationReply AgentActions::locate(std::uint32_t release_id) {
    const auto reply = http_.get(download_location_url(release_id), [this] { return cancelled(); });
    return {reply.error, reply.body};
}
std::filesystem::path AgentActions::reserve_pending() { return reserve_pending_package(); }
DownloadError AgentActions::download(const std::string& url, const std::filesystem::path& pending,
    std::uint64_t limit, const std::function<bool(std::uint32_t)>& progress) {
    return http_.save(url, pending, limit, [this] { return cancelled(); }, progress).error;
}
bool AgentActions::verify(const std::filesystem::path& pending, const PackageFingerprint& package) {
    return verify_package(pending, package, [this] { return cancelled(); }) == PackageStatus::kReady;
}
bool AgentActions::publish(const std::filesystem::path& pending, const ReleaseMetadata& release) {
    return publish_package(pending, ready_package_path(release.version), release.package, [this] { return cancelled(); }) == PackageStatus::kReady;
}
void AgentActions::discard(const std::filesystem::path& pending) noexcept { std::error_code error; std::filesystem::remove(pending, error); }
}

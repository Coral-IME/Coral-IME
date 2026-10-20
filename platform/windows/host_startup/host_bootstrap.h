#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>

namespace coral::bootstrap {
enum class ProbeStatus { kReady, kRetry, kFailed };
enum class StartupStatus { kReady, kInstallationBlocked, kLaunchFailed, kTimedOut, kProbeFailed, kInternalError };
struct StartupResult {
    StartupStatus status = StartupStatus::kInternalError;
    bool direct_attempted = false;
    bool service_attempted = false;
    explicit operator bool() const { return status == StartupStatus::kReady; }
};
class StartupActions {
public:
    virtual ~StartupActions() = default;
    virtual std::uint64_t now_ms() = 0;
    virtual ProbeStatus probe_pipe(std::uint32_t wait_ms) = 0;
    virtual void pause(std::uint32_t wait_ms) = 0;
    virtual bool installation_blocked() = 0;
    virtual bool restricted_host() = 0;
    virtual std::filesystem::path session_host_path() = 0;
    virtual bool launch_direct(const std::filesystem::path& executable) = 0;
    virtual bool launch_service() = 0;
};

StartupResult ensure_host_ready(StartupActions& actions, std::uint32_t budget_ms);
std::filesystem::path select_session_host_path(const std::filesystem::path& module_directory,
    const std::filesystem::path& install_directory,
    const std::function<bool(const std::filesystem::path&)>& is_file);
} // 命名空间 coral::bootstrap 结束

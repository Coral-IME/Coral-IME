#include "host_bootstrap.h"
#include "coral/product/product_identity.h"
#include <algorithm>

namespace coral::bootstrap {
std::filesystem::path select_session_host_path(const std::filesystem::path& module_directory,
    const std::filesystem::path& install_directory,
    const std::function<bool(const std::filesystem::path&)>& is_file) {
    for (const auto& directory : {module_directory, install_directory}) {
        if (directory.empty() || !directory.is_absolute()) continue;
        const auto executable = directory / product::kSessionHostExeWide;
        if (is_file(executable)) return executable;
    }
    return {};
}

StartupResult ensure_host_ready(StartupActions& actions, std::uint32_t budget_ms) {
    StartupResult result;
    try {
        const auto started = actions.now_ms();
        const auto remaining = [&]() -> std::uint32_t {
            const auto elapsed = actions.now_ms() - started;
            return elapsed >= budget_ms ? 0 : budget_ms - static_cast<std::uint32_t>(elapsed);
        };
        const auto initial = actions.probe_pipe(0);
        if (initial == ProbeStatus::kReady) { result.status = StartupStatus::kReady; return result; }
        if (initial == ProbeStatus::kFailed) { result.status = StartupStatus::kProbeFailed; return result; }
        if (actions.installation_blocked()) { result.status = StartupStatus::kInstallationBlocked; return result; }
        if (!remaining()) { result.status = StartupStatus::kTimedOut; return result; }
        bool launched = false;
        if (!actions.restricted_host()) {
            const auto executable = actions.session_host_path();
            if (!remaining()) { result.status = StartupStatus::kTimedOut; return result; }
            if (!executable.empty() && executable.is_absolute()) {
                result.direct_attempted = true;
                launched = actions.launch_direct(executable);
            }
        }
        if (!launched) {
            if (!remaining()) { result.status = StartupStatus::kTimedOut; return result; }
            result.service_attempted = true;
            launched = actions.launch_service();
        }
        if (!launched) { result.status = StartupStatus::kLaunchFailed; return result; }
        for (;;) {
            const auto wait_ms = remaining();
            if (!wait_ms) { result.status = StartupStatus::kTimedOut; return result; }
            const auto probe = actions.probe_pipe(std::min<std::uint32_t>(wait_ms, 25));
            if (probe == ProbeStatus::kReady) { result.status = StartupStatus::kReady; return result; }
            if (probe == ProbeStatus::kFailed) { result.status = StartupStatus::kProbeFailed; return result; }
            const auto sleep_ms = std::min<std::uint32_t>(remaining(), 10);
            if (sleep_ms) actions.pause(sleep_ms);
        }
    } catch (...) { result.status = StartupStatus::kInternalError; return result; }
}
} // 命名空间 coral::bootstrap 结束

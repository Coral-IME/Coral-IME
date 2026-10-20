#pragma once
#include "launch_request.h"

namespace coral::service {
class WindowsLaunchActions final : public LaunchActions {
public:
    bool installation_blocked() override;
    std::filesystem::path registered_settings_path() override;
    bool is_regular_file(const std::filesystem::path& path) override;
    bool already_running(const std::filesystem::path& executable, std::uint32_t session_id) override;
    ProcessAttempt launch_process(const std::filesystem::path& executable,
        std::uint32_t session_id, LaunchTarget target) override;
};
} // 命名空间 coral::service 结束

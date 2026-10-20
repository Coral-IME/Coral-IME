#pragma once
#include "host_bootstrap.h"
#include <windows.h>

namespace coral::bootstrap {
struct HostLaunchHooks {
    HMODULE module = nullptr;
    std::function<std::filesystem::path()> install_directory;
    std::function<bool()> installation_blocked;
    std::function<bool()> request_service_launch;
};
bool is_restricted_text_host();
StartupResult start_session_host(const HostLaunchHooks& hooks, std::uint32_t budget_ms = 2000);
} // 命名空间 coral::bootstrap 结束

#include "pch.h"
#include "session_bridge.h"
#include "windows_host_bootstrap.h"
#include "coreapi.h"
#include "processmonitor.h"
#include "pipe_session_client.h"
#include "input_timing.h"

namespace coral::text_service {
std::shared_ptr<SessionBridge> shared_session_bridge() {
    static auto bridge = std::make_shared<SessionBridge>([](std::uint32_t wait_ms) {
        coral::input_timing::Scope timing(L"text_service.connect");
        bootstrap::HostLaunchHooks hooks;
        hooks.module = g_hInst;
        hooks.install_directory = [] { return hlp::getInstallPath(); };
        hooks.installation_blocked = [] { return isInstalling(); };
        hooks.request_service_launch = [] { return Coreapi::LaunchEngineHostViaService(); };
        const auto result = bootstrap::start_session_host(hooks, wait_ms);
        if (!result) {
            LOG_ERROR_W(L"[text_service] event=host_start_failed status=" +
                std::to_wstring(static_cast<unsigned>(result.status)));
            return std::unique_ptr<composition::SessionPort>{};
        }
        return transport::make_pipe_session_client(transport::default_session_pipe(), 75);
    });
    return bridge;
}
} // 命名空间 coral::text_service 结束

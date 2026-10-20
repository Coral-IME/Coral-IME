#include "windows_host_bootstrap.h"
#include "pipe_session_client.h"
#include <vector>

namespace coral::bootstrap {
namespace {
class OwnedHandle {
public:
    explicit OwnedHandle(HANDLE handle) : handle_(handle) {}
    ~OwnedHandle() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_); }
    OwnedHandle(const OwnedHandle&) = delete;
    OwnedHandle& operator=(const OwnedHandle&) = delete;
private:
    HANDLE handle_;
};

std::filesystem::path module_path(HMODULE module) {
    for (DWORD capacity = 512; capacity <= 32768; capacity *= 2) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = ::GetModuleFileNameW(module, buffer.data(), capacity);
        if (!length) return {};
        if (length < capacity) return std::filesystem::path(std::wstring(buffer.data(), length));
    }
    return {}; // 绝不把截断的缓冲区解释成有效的可执行路径。
}

class WindowsStartupActions final : public StartupActions {
public:
    explicit WindowsStartupActions(const HostLaunchHooks& hooks) : hooks_(hooks), pipe_(transport::default_session_pipe()) {}
    std::uint64_t now_ms() override { return ::GetTickCount64(); }
    void pause(std::uint32_t wait_ms) override { ::Sleep(wait_ms); }
    ProbeStatus probe_pipe(std::uint32_t wait_ms) override {
        // Win32 中零值即 NMPWAIT_USE_DEFAULT；初始探测应使用 NOWAIT。
        if (::WaitNamedPipeW(pipe_.c_str(), wait_ms ? wait_ms : NMPWAIT_NOWAIT)) return ProbeStatus::kReady;
        switch (::GetLastError()) {
        case ERROR_FILE_NOT_FOUND: case ERROR_SEM_TIMEOUT: case ERROR_PIPE_BUSY: return ProbeStatus::kRetry;
        default: return ProbeStatus::kFailed;
        }
    }
    bool installation_blocked() override { return hooks_.installation_blocked && hooks_.installation_blocked(); }
    bool restricted_host() override { return is_restricted_text_host(); }
    std::filesystem::path session_host_path() override {
        const auto module = module_path(hooks_.module);
        const auto directory = module.empty() ? std::filesystem::path{} : module.parent_path();
        const auto is_file = [](const std::filesystem::path& path) {
            std::error_code error;
            return std::filesystem::is_regular_file(path, error) && !error;
        };
        const auto adjacent = select_session_host_path(directory, {}, is_file);
        if (!adjacent.empty()) return adjacent;
        return select_session_host_path({}, hooks_.install_directory ? hooks_.install_directory() : std::filesystem::path{}, is_file);
    }
    bool launch_direct(const std::filesystem::path& executable) override {
        std::wstring command = L"\"" + executable.wstring() + L"\"";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &process)) return false;
        OwnedHandle thread(process.hThread);
        OwnedHandle child(process.hProcess);
        return true;
    }
    bool launch_service() override { return hooks_.request_service_launch && hooks_.request_service_launch(); }
private:
    const HostLaunchHooks& hooks_;
    std::wstring pipe_;
};
}

bool is_restricted_text_host() {
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        OwnedHandle owned_token(token);
        DWORD app_container = 0, bytes = 0;
        if (::GetTokenInformation(token, TokenIsAppContainer, &app_container, sizeof(app_container), &bytes) && app_container) return true;
    }
    const auto image = module_path(nullptr);
    return !image.empty() && _wcsicmp(image.filename().c_str(), L"SearchHost.exe") == 0;
}

StartupResult start_session_host(const HostLaunchHooks& hooks, std::uint32_t budget_ms) {
    try {
        WindowsStartupActions actions(hooks);
        return ensure_host_ready(actions, budget_ms);
    } catch (...) { return {}; }
}
} // 命名空间 coral::bootstrap 结束

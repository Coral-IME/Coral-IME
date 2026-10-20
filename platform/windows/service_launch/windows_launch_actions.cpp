#include "windows_launch_actions.h"
#include "coral/product/product_identity.h"
#include <windows.h>
#include <tlhelp32.h>
#include <userenv.h>
#include <wtsapi32.h>
#include <vector>

namespace coral::service {
namespace {
class KernelHandle {
public:
    explicit KernelHandle(HANDLE handle) : handle_(handle) {}
    ~KernelHandle() { if (handle_ && handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_); }
    KernelHandle(const KernelHandle&) = delete;
    KernelHandle& operator=(const KernelHandle&) = delete;
private:
    HANDLE handle_;
};
class EnvironmentBlock {
public:
    explicit EnvironmentBlock(void* block) : block_(block) {}
    ~EnvironmentBlock() { if (block_) ::DestroyEnvironmentBlock(block_); }
    EnvironmentBlock(const EnvironmentBlock&) = delete;
    EnvironmentBlock& operator=(const EnvironmentBlock&) = delete;
private:
    void* block_;
};
}

bool WindowsLaunchActions::installation_blocked() {
    const auto mutex = ::OpenMutexW(SYNCHRONIZE, FALSE, product::kInstallationMutexWide);
    const auto error = ::GetLastError();
    KernelHandle owned_mutex(mutex);
    return mutex || error == ERROR_ACCESS_DENIED;
}

std::filesystem::path WindowsLaunchActions::registered_settings_path() {
    constexpr DWORD kFlags = RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY;
    DWORD bytes = 0;
    if (::RegGetValueW(HKEY_LOCAL_MACHINE, product::kLauncherRegistryWide, product::kInstallPathValueWide,
        kFlags, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t) || bytes > 32768 * sizeof(wchar_t) || bytes % sizeof(wchar_t)) return {};
    std::vector<wchar_t> value(bytes / sizeof(wchar_t));
    const auto status = ::RegGetValueW(HKEY_LOCAL_MACHINE, product::kLauncherRegistryWide, product::kInstallPathValueWide,
        kFlags, nullptr, value.data(), &bytes);
    if (status != ERROR_SUCCESS || bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) || bytes > value.size() * sizeof(wchar_t)) return {};
    const auto count = bytes / sizeof(wchar_t);
    if (value[count - 1] != L'\0') return {};
    const std::wstring text(value.data(), count - 1);
    if (text.find(L'\0') != std::wstring::npos) return {};
    return std::filesystem::path(text);
}

bool WindowsLaunchActions::is_regular_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error;
}

bool WindowsLaunchActions::already_running(const std::filesystem::path& executable, std::uint32_t session_id) {
    const auto snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    KernelHandle owned_snapshot(snapshot);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!::Process32FirstW(snapshot, &entry)) return false;
    do {
        if (_wcsicmp(entry.szExeFile, executable.filename().c_str()) != 0) continue;
        DWORD actual_session = 0;
        if (!::ProcessIdToSessionId(entry.th32ProcessID, &actual_session) || actual_session != session_id) continue;
        const auto process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        KernelHandle owned_process(process);
        if (!process) continue;
        std::vector<wchar_t> image(32768);
        DWORD length = static_cast<DWORD>(image.size());
        if (::QueryFullProcessImageNameW(process, 0, image.data(), &length) &&
            _wcsicmp(std::filesystem::path(std::wstring(image.data(), length)).lexically_normal().c_str(), executable.c_str()) == 0) return true;
    } while (::Process32NextW(snapshot, &entry));
    return false;
}

ProcessAttempt WindowsLaunchActions::launch_process(const std::filesystem::path& executable, std::uint32_t session_id, LaunchTarget target) {
    HANDLE user_token = nullptr;
    if (!::WTSQueryUserToken(session_id, &user_token)) return {false, ::GetLastError()};
    KernelHandle owned_user(user_token);
    HANDLE primary_token = nullptr;
    if (!::DuplicateTokenEx(user_token, TOKEN_ALL_ACCESS, nullptr, SecurityImpersonation, TokenPrimary, &primary_token)) return {false, ::GetLastError()};
    KernelHandle owned_primary(primary_token);
    void* environment = nullptr;
    if (!::CreateEnvironmentBlock(&environment, primary_token, FALSE)) return {false, ::GetLastError()};
    EnvironmentBlock owned_environment(environment);
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    if (target == LaunchTarget::kSettings) command += L" --startup-type=2";
    STARTUPINFOW startup{sizeof(startup)};
    wchar_t desktop[] = L"winsta0\\default";
    startup.lpDesktop = desktop;
    PROCESS_INFORMATION process{};
    if (!::CreateProcessAsUserW(primary_token, executable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | NORMAL_PRIORITY_CLASS | CREATE_UNICODE_ENVIRONMENT, environment,
        executable.parent_path().c_str(), &startup, &process)) return {false, ::GetLastError()};
    KernelHandle owned_thread(process.hThread);
    KernelHandle owned_child(process.hProcess);
    return {true, 0};
}
} // 命名空间 coral::service 结束

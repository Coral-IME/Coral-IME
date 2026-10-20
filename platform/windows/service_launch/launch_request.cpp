#include "launch_request.h"
#include "coral/product/product_identity.h"
#include <limits>

namespace coral::service {
bool parse_session_id(std::wstring_view text, std::uint32_t& session_id) noexcept {
    if (text.empty() || text.size() > 10 || text.front() == L'0') return false;
    std::uint32_t value = 0;
    constexpr auto limit = std::numeric_limits<std::uint32_t>::max() - 1;
    for (const auto ch : text) {
        if (ch < L'0' || ch > L'9') return false;
        const auto digit = static_cast<std::uint32_t>(ch - L'0');
        if (value > (limit - digit) / 10) return false;
        value = value * 10 + digit;
    }
    session_id = value;
    return true;
}

namespace {
bool same_basename(const std::wstring& actual, const wchar_t* expected) {
    std::wstring name(expected);
    if (actual.size() != name.size()) return false;
    const auto lower = [](wchar_t ch) { return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch; };
    for (std::size_t i = 0; i < name.size(); ++i) if (lower(actual[i]) != lower(name[i])) return false;
    return true;
}
}

LaunchReply handle_launch(const LaunchRequest& request, const std::optional<CallerSession>& caller, LaunchActions& actions) noexcept {
    if (!request.session_id || request.session_id == std::numeric_limits<std::uint32_t>::max() ||
        (request.target != LaunchTarget::kSettings && request.target != LaunchTarget::kSessionHost && request.target != LaunchTarget::kRenderer))
        return {LaunchStatus::kInvalidArgument};
    if (!caller || !caller->process_id || caller->session_id != request.session_id)
        return {LaunchStatus::kAccessDenied};
    try {
        if (actions.installation_blocked()) return {LaunchStatus::kInstalling};
        const auto settings = actions.registered_settings_path();
        const auto text = settings.native();
        if (settings.empty() || !settings.is_absolute() || text.find(L'\0') != std::wstring::npos ||
            text.find(L'"') != std::wstring::npos || !same_basename(settings.filename().wstring(), product::kSettingsExeWide))
            return {LaunchStatus::kPathNotFound};
        auto executable = settings.lexically_normal();
        if (request.target == LaunchTarget::kSessionHost) executable = executable.parent_path() / product::kSessionHostExeWide;
        if (request.target == LaunchTarget::kRenderer) executable = executable.parent_path() / product::kRendererExeWide;
        if (!actions.is_regular_file(executable)) return {LaunchStatus::kPathNotFound};
        if (actions.already_running(executable, request.session_id)) return {LaunchStatus::kAlreadyRunning};
        const auto attempt = actions.launch_process(executable, request.session_id, request.target);
        return {attempt.started ? LaunchStatus::kStarted : LaunchStatus::kLaunchFailed, attempt.started ? 0 : attempt.error};
    } catch (...) { return {LaunchStatus::kInternalError}; }
}

const wchar_t* launch_status_name(LaunchStatus status) noexcept {
    switch (status) {
    case LaunchStatus::kStarted: return L"launched";
    case LaunchStatus::kAlreadyRunning: return L"already_running";
    case LaunchStatus::kInvalidArgument: return L"invalid_argument";
    case LaunchStatus::kAccessDenied: return L"access_denied";
    case LaunchStatus::kInstalling: return L"installing";
    case LaunchStatus::kPathNotFound: return L"path_not_found";
    case LaunchStatus::kLaunchFailed: return L"launch_failed";
    default: return L"internal_error";
    }
}
} // 命名空间 coral::service 结束

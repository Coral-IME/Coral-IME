#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace coral::service {
enum class LaunchTarget { kSettings, kSessionHost, kRenderer };
enum class LaunchStatus { kStarted, kAlreadyRunning, kInvalidArgument, kAccessDenied, kInstalling, kPathNotFound, kLaunchFailed, kInternalError };
struct LaunchRequest {
    LaunchTarget target = LaunchTarget::kSettings;
    std::uint32_t session_id = 0;
};
struct CallerSession {
    std::uint32_t process_id = 0;
    std::uint32_t session_id = 0;
};
struct LaunchReply {
    LaunchStatus status = LaunchStatus::kInternalError;
    std::uint32_t error = 0;
};
struct ProcessAttempt {
    bool started = false;
    std::uint32_t error = 0;
};
class LaunchActions {
public:
    virtual ~LaunchActions() = default;
    virtual bool installation_blocked() = 0;
    virtual std::filesystem::path registered_settings_path() = 0;
    virtual bool is_regular_file(const std::filesystem::path& path) = 0;
    virtual bool already_running(const std::filesystem::path& executable, std::uint32_t session_id) = 0;
    virtual ProcessAttempt launch_process(const std::filesystem::path& executable,
        std::uint32_t session_id, LaunchTarget target) = 0;
};

// 唯一的规范非零交互会话；不得含空白/符号/尾部文本。
// 失败时保持调用方的目标值不变。
bool parse_session_id(std::wstring_view text, std::uint32_t& session_id) noexcept;
LaunchReply handle_launch(const LaunchRequest& request, const std::optional<CallerSession>& caller, LaunchActions& actions) noexcept;
const wchar_t* launch_status_name(LaunchStatus status) noexcept;
} // 命名空间 coral::service 结束

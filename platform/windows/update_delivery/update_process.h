#pragma once
#include "coral/updates/download_wire.h"
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace coral::updates {
std::filesystem::path adjacent_update_agent();
enum class UpdateLaunchStage { kPath, kRequest, kMapping, kHandles, kAttributes, kJob, kCreate, kAssign, kResume };
struct UpdateLaunchFailure { UpdateLaunchStage stage = UpdateLaunchStage::kPath; std::uint32_t error = 0; };
class UpdateProcess {
public:
    static std::unique_ptr<UpdateProcess> start(const std::filesystem::path& executable, const ReleaseMetadata& release,
        UpdateLaunchFailure* failure = nullptr);
    ~UpdateProcess();
    UpdateProcess(const UpdateProcess&) = delete;
    UpdateProcess& operator=(const UpdateProcess&) = delete;
    DownloadProgress poll();
    void cancel() noexcept;
    bool stop() noexcept; // 若拥有的作业在宽限期后仍需强制终止则为 true。
    bool wait_for_exit(std::uint32_t milliseconds) const noexcept;
    std::uint32_t child_id() const noexcept;
    const DownloadRequest& request() const noexcept;
private:
    struct State;
    explicit UpdateProcess(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};
class InheritedUpdateChannel {
public:
    static std::unique_ptr<InheritedUpdateChannel> open(const std::vector<std::wstring_view>& arguments);
    ~InheritedUpdateChannel();
    InheritedUpdateChannel(const InheritedUpdateChannel&) = delete;
    InheritedUpdateChannel& operator=(const InheritedUpdateChannel&) = delete;
    bool cancelled() const noexcept;
    bool report(const DownloadProgress& progress) noexcept;
    const DownloadRequest& request() const noexcept;
    std::uint32_t parent_id() const noexcept;
private:
    struct State;
    explicit InheritedUpdateChannel(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};
}

#pragma once
#ifndef CORAL_MODULES_FOUNDATION_BACKGROUND_TASKS_H_INCLUDED
#define CORAL_MODULES_FOUNDATION_BACKGROUND_TASKS_H_INCLUDED

#include <functional>
#include <future>
#include <mutex>
#include <vector>

namespace coral::foundation {

// 由宿主持有的有界批次。任务不得对自身所有者调用 start/finish。
// 宿主必须在卸载包含任务代码的 DLL 之前 finish。
class BackgroundTasks final {
public:
    using Task = std::function<bool()>;

    BackgroundTasks() = default;
    ~BackgroundTasks() { finish(); }
    BackgroundTasks(const BackgroundTasks&) = delete;
    BackgroundTasks& operator=(const BackgroundTasks&) = delete;

    bool start(std::vector<Task> tasks) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pending_.empty()) return false;
        for (const auto& task : tasks) if (!task) return false;
        std::vector<std::future<bool>> pending;
        try {
            pending.reserve(tasks.size());
            for (auto& task : tasks)
                pending.push_back(std::async(std::launch::async, std::move(task)));
        } catch (...) {
            // 销毁这些 future 会等待所有已成功启动的作业。
            return false;
        }
        pending_ = std::move(pending);
        return true;
    }

    // 排空每个作业，包括失败之后的。异常按失败上报。
    // 已完成的批次随后可由 start() 替换；空批次 finish 视为成功。
    bool finish() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        bool succeeded = true;
        for (auto& job : pending_) {
            try { if (!job.get()) succeeded = false; }
            catch (...) { succeeded = false; }
        }
        pending_.clear();
        return succeeded;
    }

private:
    std::mutex mutex_;
    std::vector<std::future<bool>> pending_;
};

} // 命名空间 coral::foundation 结束
#endif

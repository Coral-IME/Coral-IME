#pragma once
#ifndef CORAL_APPS_SETTINGS_APP_DEFAULT_INPUT_SETTINGS_H_INCLUDED
#define CORAL_APPS_SETTINGS_APP_DEFAULT_INPUT_SETTINGS_H_INCLUDED

#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace coral::settings_app
{
// 只导航到系统页面，不选择或写入默认输入法。
class DefaultInputSettings
{
public:
    enum class State { idle, running, succeeded, failed, cancelled };
    DefaultInputSettings() = default;
    ~DefaultInputSettings();
    DefaultInputSettings(const DefaultInputSettings&) = delete;
    DefaultInputSettings& operator=(const DefaultInputSettings&) = delete;
    bool start(HWND owner);
    void stop();
    State state() const { return state_.load(); }

private:
    void navigate();
    std::atomic<State> state_{ State::idle };
    std::atomic<bool> cancelled_{ false };
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
};
}
#endif

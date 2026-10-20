#pragma once
#include "pch.h"
#include "upgrademode.h"

struct UpdateInfoLite {
    std::string serverVersion;
    std::string updateDesc;
    bool packageAvailable = false;
};
class CoralUpdateWindow {
public:
    CoralUpdateWindow() = default;
    ~CoralUpdateWindow();
    bool create(const UpdateInfoLite& info, UpgradeMode mode);
    void show(bool activate = false);
    void showFailure(const wchar_t* message);
    void set_busy(bool busy);
    void set_progress(std::uint32_t percent, const wchar_t* message);
    void show_installer_started();
    bool is_alive() const { return m_hwnd != nullptr; }
    HWND hwnd() const { return m_hwnd; }
    std::function<void()> onUpdate;
    std::function<void()> onTimer;
    std::function<void()> onClosed;
private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    HWND m_hwnd = nullptr;
    HFONT m_font = nullptr;
    UpgradeMode m_mode = UpgradeMode::Manual;
    UpdateInfoLite m_info;
    HWND m_lblStatus = nullptr;
    bool m_busy = false;
    static constexpr int ID_BTN_OPEN = 0x2200;
    static constexpr int ID_BTN_CLOSE = 0x2202;
};

#pragma once
#include "pch.h"
#include "upgrademode.h"

struct UpdateInfoLite {
    std::string serverVersion;
    std::string updateDesc;
};
class CoralUpdateWindow {
public:
    CoralUpdateWindow() = default;
    ~CoralUpdateWindow();
    bool create(const UpdateInfoLite& info, UpgradeMode mode);
    void show(bool activate = false);
    void showFailure(const wchar_t* message);
    bool is_alive() const { return m_hwnd != nullptr; }
    HWND hwnd() const { return m_hwnd; }
    std::function<void()> onOpenRelease;
    std::function<void()> onClosed;
private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    HWND m_hwnd = nullptr;
    HFONT m_font = nullptr;
    UpgradeMode m_mode = UpgradeMode::Manual;
    UpdateInfoLite m_info;
    HWND m_lblStatus = nullptr;
    static constexpr int ID_BTN_OPEN = 0x2200;
    static constexpr int ID_BTN_CLOSE = 0x2202;
};

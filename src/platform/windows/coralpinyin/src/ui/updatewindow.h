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
    static LRESULT CALLBACK control_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass, DWORD_PTR context);
    void rebuild_fonts();
    void release_drawing_resources();
    void layout_controls();
    void paint(HDC dc);
    void paint_button(const DRAWITEMSTRUCT& item);
    HWND m_hwnd = nullptr;
    HFONT m_font = nullptr;
    HFONT title_font_ = nullptr;
    HFONT section_font_ = nullptr;
    HFONT caption_font_ = nullptr;
    HBRUSH surface_brush_ = nullptr;
    COLORREF surface_color_ = 0;
    HICON logo_ = nullptr;
    UINT dpi_ = 96;
    UpgradeMode m_mode = UpgradeMode::Manual;
    UpdateInfoLite m_info;
    HWND description_ = nullptr;
    std::wstring status_;
    std::uint32_t progress_ = 0;
    int hovered_button_ = 0;
    bool failed_ = false;
    bool installer_started_ = false;
    bool m_busy = false;
    static constexpr int ID_BTN_OPEN = 0x2200;
    static constexpr int ID_BTN_CLOSE = 0x2202;
};

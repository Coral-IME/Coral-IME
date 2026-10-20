#include "pch.h"
#include "updatewindow.h"
#include "settings_dpi.h"
#include "utf8conv.h"

CoralUpdateWindow::~CoralUpdateWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
    if (m_font) DeleteObject(m_font);
}
bool CoralUpdateWindow::create(const UpdateInfoLite& info, UpgradeMode mode) {
    coral::settings_ui::DpiContext context(DPI_AWARENESS_CONTEXT_UNAWARE);
    if (m_hwnd) return false;
    m_info = info;
    m_mode = mode;
    static const ATOM registered = [] {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = &CoralUpdateWindow::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"CoralPinyinUpdateWnd";
        return RegisterClassExW(&wc);
    }();
    if (!registered) return false;
    RECT area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    RECT rc{ 0, 0, 460, 340 };
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rc, style, FALSE);
    const int width = rc.right - rc.left, height = rc.bottom - rc.top;
    const int x = mode == UpgradeMode::Manual ? area.left + (area.right - area.left - width) / 2 : area.right - width - 16;
    const int y = mode == UpgradeMode::Manual ? area.top + (area.bottom - area.top - height) / 2 : area.bottom - height - 16;
    CreateWindowExW(WS_EX_APPWINDOW, L"CoralPinyinUpdateWnd", L"CORAL拼音更新", style,
        x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    return m_hwnd != nullptr;
}
void CoralUpdateWindow::show(bool activate) {
    if (!m_hwnd) return;
    const bool foreground = activate || m_mode == UpgradeMode::Manual;
    ShowWindow(m_hwnd, foreground ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
    if (foreground) SetForegroundWindow(m_hwnd);
}
void CoralUpdateWindow::showFailure(const wchar_t* message) {
    if (m_lblStatus) SetWindowTextW(m_lblStatus, message);
}
LRESULT CALLBACK CoralUpdateWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CoralUpdateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<CoralUpdateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    switch (msg) {
    case WM_CREATE: {
        self->m_font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
        const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id,
            int x, int y, int w, int h, DWORD ex = 0) {
            const HWND child = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(self->m_font), TRUE);
            return child;
        };
        const auto title = L"发现新版本 " + utf8ToWstr(self->m_info.serverVersion);
        make(L"STATIC", title.c_str(), SS_LEFT, 0, 24, 22, 412, 28);
        std::wstring description;
        const auto original = utf8ToWstr(self->m_info.updateDesc);
        for (std::size_t i = 0; i < original.size(); ++i) {
            if (original[i] == L'\r' || original[i] == L'\n') {
                if (original[i] == L'\r' && i + 1 < original.size() && original[i + 1] == L'\n') ++i;
                description += L"\r\n";
            } else description += original[i];
        }
        make(L"EDIT", description.c_str(),
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
            0, 24, 62, 412, 150, WS_EX_CLIENTEDGE);
        self->m_lblStatus = make(L"STATIC", L"前往 GitHub 下载后，请手动运行安装包。", SS_LEFT,
            0, 24, 226, 412, 44);
        make(L"BUTTON", L"前往下载", BS_PUSHBUTTON | WS_TABSTOP, ID_BTN_OPEN, 306, 286, 130, 32);
        make(L"BUTTON", L"稍后", BS_PUSHBUTTON | WS_TABSTOP, ID_BTN_CLOSE, 164, 286, 130, 32);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_OPEN && self && self->onOpenRelease) self->onOpenRelease();
        if (LOWORD(wParam) == ID_BTN_CLOSE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (self) {
            self->m_hwnd = nullptr;
            if (self->m_font) { DeleteObject(self->m_font); self->m_font = nullptr; }
            const auto closed = self->onClosed;
            if (closed) closed();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

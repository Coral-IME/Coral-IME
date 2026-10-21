#include "pch.h"
#include "updatewindow.h"
#include "settings_dpi.h"
#include "settingswindow.h"
#include "utf8conv.h"
#include <dwmapi.h>

namespace {
using namespace coral::win32::soft;
using coral::settings_ui::DpiContext;
using coral::settings_ui::DpiScale;
constexpr int kWidth = 540;
constexpr int kHeight = 470;

std::uint32_t blend_color(std::uint32_t from, std::uint32_t to, float amount) {
    const auto channel = [&](int shift) {
        return static_cast<std::uint32_t>(((from >> shift) & 255) * (1 - amount) +
            ((to >> shift) & 255) * amount + 0.5f) << shift;
    };
    return channel(24) | channel(16) | channel(8) | channel(0);
}

void draw_text(HDC dc, const std::wstring& text, RECT rect, HFONT font,
    std::uint32_t color, UINT flags = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX) {
    const auto previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ToColorRef(color));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
    SelectObject(dc, previous);
}
}

CoralUpdateWindow::~CoralUpdateWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
    release_drawing_resources();
}

void CoralUpdateWindow::release_drawing_resources() {
    for (HFONT* font : {&m_font, &title_font_, &section_font_, &caption_font_}) {
        if (*font) { DeleteObject(*font); *font = nullptr; }
    }
    if (surface_brush_) { DeleteObject(surface_brush_); surface_brush_ = nullptr; }
    if (logo_) { DestroyIcon(logo_); logo_ = nullptr; }
}

void CoralUpdateWindow::rebuild_fonts() {
    release_drawing_resources();
    const DpiScale scale(dpi_);
    const auto& skin = SettingsWindow::skin();
    const auto font = [&](int height, int weight) {
        return CreateFontW(-scale.pixels(height), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, skin.fontName.c_str());
    };
    m_font = font(14, FW_NORMAL);
    section_font_ = font(14, FW_SEMIBOLD);
    title_font_ = font(24, FW_SEMIBOLD);
    caption_font_ = font(12, FW_NORMAL);
    surface_color_ = ToColorRef(skin.surface);
    surface_brush_ = CreateSolidBrush(surface_color_);
    logo_ = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_PINYIN),
        IMAGE_ICON, scale.pixels(56), scale.pixels(56), 0));
    if (description_) SendMessageW(description_, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    for (const int id : {ID_BTN_OPEN, ID_BTN_CLOSE})
        SendDlgItemMessageW(m_hwnd, id, WM_SETFONT, reinterpret_cast<WPARAM>(section_font_), TRUE);
}

bool CoralUpdateWindow::create(const UpdateInfoLite& info, UpgradeMode mode) {
    DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (m_hwnd) return false;
    m_info = info;
    m_mode = mode;
    m_busy = failed_ = installer_started_ = false;
    progress_ = 0;
    hovered_button_ = 0;
    status_ = info.packageAvailable ? L"下载并校验完成后，将自动启动安装程序。"
        : L"前往发布页下载后，请手动运行安装包。";
    static const ATOM registered = [] {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = &CoralUpdateWindow::wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_PINYIN));
        wc.hIconSm = wc.hIcon;
        wc.lpszClassName = L"CoralPinyinUpdateWnd";
        return RegisterClassExW(&wc);
    }();
    if (!registered) return false;
    const HWND active = GetActiveWindow();
    dpi_ = active ? GetDpiForWindow(active) : GetDpiForSystem();
    const DpiScale scale(dpi_);
    MONITORINFO monitor{ sizeof(monitor) };
    GetMonitorInfoW(MonitorFromWindow(active, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT area = monitor.rcWork;
    RECT rc{0, 0, scale.pixels(kWidth), scale.pixels(kHeight)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_APPWINDOW, dpi_);
    const int width = rc.right - rc.left, height = rc.bottom - rc.top;
    const int x = mode == UpgradeMode::Manual ? area.left + (area.right - area.left - width) / 2 : area.right - width - scale.pixels(16);
    const int y = mode == UpgradeMode::Manual ? area.top + (area.bottom - area.top - height) / 2 : area.bottom - height - scale.pixels(16);
    CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, L"CoralPinyinUpdateWnd", L"CORAL拼音更新", style,
        x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    return m_hwnd != nullptr;
}

void CoralUpdateWindow::layout_controls() {
    if (!m_hwnd) return;
    const DpiScale scale(dpi_);
    RECT client{};
    GetClientRect(m_hwnd, &client);
    client = scale.logical(client);
    const auto move = [&](HWND control, int x, int y, int width, int height) {
        MoveWindow(control, scale.pixels(x), scale.pixels(y), scale.pixels(width), scale.pixels(height), TRUE);
    };
    move(description_, 40, 196, client.right - 80, (std::max)(40L, client.bottom - 352));
    move(GetDlgItem(m_hwnd, ID_BTN_CLOSE), client.right - 280, client.bottom - 52, 108, 36);
    move(GetDlgItem(m_hwnd, ID_BTN_OPEN), client.right - 160, client.bottom - 52, 136, 36);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CoralUpdateWindow::show(bool activate) {
    if (!m_hwnd) return;
    const bool foreground = activate || m_mode == UpgradeMode::Manual;
    ShowWindow(m_hwnd, foreground ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
    if (foreground) {
        SetForegroundWindow(m_hwnd);
        SetFocus(GetDlgItem(m_hwnd, installer_started_ ? ID_BTN_CLOSE : ID_BTN_OPEN));
    }
}

void CoralUpdateWindow::showFailure(const wchar_t* message) {
    set_busy(false);
    failed_ = true;
    status_ = message;
    if (!m_hwnd) return;
    SetDlgItemTextW(m_hwnd, ID_BTN_OPEN, m_info.packageAvailable ? L"重试更新" : L"重新打开");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CoralUpdateWindow::set_busy(bool busy) {
    m_busy = busy;
    if (busy) { failed_ = false; progress_ = 0; }
    if (!m_hwnd) return;
    EnableWindow(GetDlgItem(m_hwnd, ID_BTN_OPEN), !busy && !installer_started_);
    SetDlgItemTextW(m_hwnd, ID_BTN_OPEN, busy ? L"正在更新…" : m_info.packageAvailable ? L"立即更新" : L"前往下载");
    SetDlgItemTextW(m_hwnd, ID_BTN_CLOSE, busy ? L"取消下载" : L"稍后");
    if (busy) SetTimer(m_hwnd, 1, 200, nullptr);
    else KillTimer(m_hwnd, 1);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CoralUpdateWindow::set_progress(std::uint32_t percent, const wchar_t* message) {
    progress_ = (std::min)(percent, 100u);
    status_ = message;
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CoralUpdateWindow::show_installer_started() {
    set_busy(false);
    installer_started_ = true;
    status_ = L"安装程序已启动，请在安装窗口中继续。";
    if (!m_hwnd) return;
    EnableWindow(GetDlgItem(m_hwnd, ID_BTN_OPEN), FALSE);
    SetDlgItemTextW(m_hwnd, ID_BTN_OPEN, L"已启动安装");
    SetDlgItemTextW(m_hwnd, ID_BTN_CLOSE, L"关闭");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void CoralUpdateWindow::paint(HDC dc) {
    const DpiScale scale(dpi_);
    RECT client{};
    GetClientRect(m_hwnd, &client);
    const auto logical = scale.logical(client);
    const int width = logical.right, height = logical.bottom;
    Canvas canvas(client.right, client.bottom);
    Surface surface(canvas.pixels(), canvas.width(), canvas.height(), scale.factor());
    const auto& skin = SettingsWindow::skin();
    surface.fill(skin.window);
    const RectF hero{24, 20, static_cast<float>(width - 24), 140};
    DrawShadow(surface, hero, 14, {1, 3, 0, skin.cardShadow}, 1);
    surface.shade(hero, [&](float x, float) {
        return blend_color(skin.heroFrom, skin.heroTo, (std::clamp)((x - hero.left) / (hero.right - hero.left), 0.0f, 1.0f));
    }, [&](float x, float y) { return Coverage(RoundRectDistance(x, y, hero, 14)); });
    StrokeRoundRect(surface, hero, 14, 1, skin.line);
    const RectF notes{24, 156, static_cast<float>(width - 24), static_cast<float>(height - 140)};
    FillRoundRect(surface, notes, 10, skin.surface);
    StrokeRoundRect(surface, notes, 10, 1, skin.line);
    // 软件表面完成后再绘制原生文字和图标，避免覆盖 ClearType 边缘。
    if (logo_) DrawIconEx(canvas.dc(), scale.pixels(42), scale.pixels(52), logo_,
        scale.pixels(56), scale.pixels(56), 0, nullptr, DI_NORMAL);
    draw_text(canvas.dc(), L"CORAL拼音 · 软件更新", scale.pixels(RECT{116, 30, width - 42, 50}), caption_font_, skin.sub);
    draw_text(canvas.dc(), L"发现新版本", scale.pixels(RECT{114, 52, width - 42, 86}), title_font_, skin.text);
    draw_text(canvas.dc(), L"版本 " + utf8ToWstr(m_info.serverVersion), scale.pixels(RECT{116, 91, width - 42, 114}), m_font, skin.sub);
    draw_text(canvas.dc(), L"更新内容", scale.pixels(RECT{40, 170, width - 40, 190}), section_font_, skin.sub);
    draw_text(canvas.dc(), status_, scale.pixels(RECT{24, height - 122, width - (m_busy ? 80 : 24), height - 86}),
        caption_font_, failed_ ? skin.danger : skin.sub, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    if (m_busy) {
        draw_text(canvas.dc(), std::to_wstring(progress_) + L"%", scale.pixels(RECT{width - 76, height - 122, width - 24, height - 100}),
            caption_font_, skin.sub, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        const RectF track{24, static_cast<float>(height - 78), static_cast<float>(width - 24), static_cast<float>(height - 74)};
        FillRoundRect(surface, track, 2, skin.sliderTrack);
        if (progress_) FillRoundRect(surface, {track.left, track.top, track.left + (track.right - track.left) * progress_ / 100, track.bottom}, 2, skin.accent);
    }
    canvas.blit(dc, 0, 0);
}

void CoralUpdateWindow::paint_button(const DRAWITEMSTRUCT& item) {
    const DpiScale scale(dpi_);
    const auto& skin = SettingsWindow::skin();
    const bool primary = item.CtlID == ID_BTN_OPEN;
    const bool enabled = !(item.itemState & ODS_DISABLED);
    const bool pressed = item.itemState & ODS_SELECTED;
    const bool hover = hovered_button_ == static_cast<int>(item.CtlID);
    Canvas canvas(item.rcItem.right - item.rcItem.left, item.rcItem.bottom - item.rcItem.top);
    Surface surface(canvas.pixels(), canvas.width(), canvas.height(), scale.factor());
    surface.fill(skin.window);
    const RectF rect{0.5f, 0.5f, canvas.width() / scale.factor() - 0.5f, canvas.height() / scale.factor() - 0.5f};
    auto fill = primary ? skin.accent : skin.surface;
    if (!enabled) fill = blend_color(fill, skin.window, 0.55f);
    else if (pressed) fill = primary ? skin.accentPressed : skin.pressed;
    else if (hover) fill = primary ? skin.accentHover : skin.hover;
    FillRoundRect(surface, rect, 6, fill);
    if (!primary) StrokeRoundRect(surface, rect, 6, 1, skin.lineStrong);
    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
    draw_text(canvas.dc(), text, {0, 0, canvas.width(), canvas.height()}, section_font_,
        !enabled ? skin.muted : primary ? skin.onAccent : skin.text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = scale.pixels(RECT{3, 3, scale.logical(canvas.width()) - 3, scale.logical(canvas.height()) - 3});
        DrawFocusRect(canvas.dc(), &focus);
    }
    canvas.blit(item.hDC, item.rcItem.left, item.rcItem.top);
}

LRESULT CALLBACK CoralUpdateWindow::control_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass, DWORD_PTR context) {
    auto* self = reinterpret_cast<CoralUpdateWindow*>(context);
    const int id = GetDlgCtrlID(hwnd);
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) { PostMessageW(self->m_hwnd, WM_CLOSE, 0, 0); return 0; }
    if (msg == WM_KEYDOWN && wparam == VK_TAB) {
        SetFocus(GetNextDlgTabItem(self->m_hwnd, hwnd, GetKeyState(VK_SHIFT) < 0));
        return 0;
    }
    if (id == ID_BTN_OPEN || id == ID_BTN_CLOSE) {
        if (msg == WM_MOUSEMOVE && self->hovered_button_ != id) {
            self->hovered_button_ = id;
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (msg == WM_MOUSELEAVE) {
            self->hovered_button_ = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (msg == WM_SETCURSOR && IsWindowEnabled(hwnd)) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, control_proc, subclass);
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK CoralUpdateWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<CoralUpdateWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<CoralUpdateWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_CREATE: {
        self->dpi_ = GetDpiForWindow(hwnd);
        const auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
            HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 0, 0,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SetWindowSubclass(child, control_proc, 1, reinterpret_cast<DWORD_PTR>(self));
            return child;
        };
        std::wstring description;
        const auto original = utf8ToWstr(self->m_info.updateDesc);
        for (std::size_t index = 0; index < original.size(); ++index) {
            if (original[index] == L'\r' || original[index] == L'\n') {
                if (original[index] == L'\r' && index + 1 < original.size() && original[index + 1] == L'\n') ++index;
                description += L"\r\n";
            } else description += original[index];
        }
        if (description.empty()) description = L"此版本优化了使用体验，建议更新。";
        self->description_ = make(L"EDIT", description.c_str(), ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0x2203);
        make(L"BUTTON", self->m_info.packageAvailable ? L"立即更新" : L"前往下载", BS_OWNERDRAW, ID_BTN_OPEN);
        make(L"BUTTON", L"稍后", BS_OWNERDRAW, ID_BTN_CLOSE);
        self->rebuild_fonts();
        self->layout_controls();
        const BOOL dark = SettingsWindow::skin().darkCaption;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        const COLORREF caption = ToColorRef(SettingsWindow::skin().window);
        DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        self->paint(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_PRINTCLIENT: self->paint(reinterpret_cast<HDC>(wParam)); return 0;
    case WM_SIZE: self->layout_controls(); return 0;
    case WM_DPICHANGED: {
        DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        self->dpi_ = LOWORD(wParam);
        self->rebuild_fonts();
        const auto& suggested = *reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested.left, suggested.top, suggested.right - suggested.left,
            suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
        self->layout_controls();
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const auto& skin = SettingsWindow::skin();
        const auto dc = reinterpret_cast<HDC>(wParam);
        const COLORREF color = ToColorRef(skin.surface);
        if (color != self->surface_color_) {
            DeleteObject(self->surface_brush_);
            self->surface_brush_ = CreateSolidBrush(color);
            self->surface_color_ = color;
        }
        SetTextColor(dc, ToColorRef(skin.text));
        SetBkColor(dc, color);
        return reinterpret_cast<LRESULT>(self->surface_brush_);
    }
    case WM_DRAWITEM: self->paint_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_OPEN && !self->m_busy && IsWindowEnabled(GetDlgItem(hwnd, ID_BTN_OPEN)) && self->onUpdate) self->onUpdate();
        if (LOWORD(wParam) == ID_BTN_CLOSE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_TIMER:
        if (wParam == 1 && self->m_busy && self->onTimer) self->onTimer();
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: {
        self->m_hwnd = nullptr;
        self->description_ = nullptr;
        self->release_drawing_resources();
        const auto closed = self->onClosed;
        if (closed) closed();
        return 0;
    }
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

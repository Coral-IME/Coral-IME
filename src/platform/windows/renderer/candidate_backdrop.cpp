#include "candidate_backdrop.h"
#include <dwmapi.h>

namespace coral::win32
{
namespace
{
constexpr wchar_t kBackdropClass[] = L"CoralPinyin.CandidateBackdrop.v1";
// 暂停启用（2026-10-15）：用户实测透明文字仍有毛边/笔画异常，
// 现有像素回归尚不足以证明显示问题已解决。保留毛玻璃实现供后续处理，
// 待文字显示与展开/收起联动重新验收后，再显式打开此开关。
constexpr bool kCandidateBackdropEnabled = false;
// 对这种非活动弹窗，DWM Desktop Acrylic 材质会变为不透明。
// 强调色模糊动态解析，且仅限于 Win11 实验特性。
// accent-policy ABI 并非稳定、有文档的 Windows 契约。
struct AccentPolicy { int state; int flags; DWORD gradient_color; int animation_id; };
struct CompositionData { int attribute; void* data; SIZE_T size; };
using SetCompositionFn = BOOL(WINAPI*)(HWND, CompositionData*);

bool apply_blur(HWND window)
{
    static const auto set_composition = reinterpret_cast<SetCompositionFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
    if (!set_composition) return false;
    AccentPolicy policy{3, 0, 0, 0}; // ACCENT_ENABLE_BLURBEHIND（背后模糊）
    CompositionData data{19, &policy, sizeof(policy)}; // WCA_ACCENT_POLICY（窗口强调策略属性）
    return set_composition(window, &data) != FALSE;
}

LRESULT CALLBACK backdrop_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        // 黑色/零 alpha 的客户区像素会暴露扩展的 DWM 玻璃边框。
        PatBlt(dc, paint.rcPaint.left, paint.rcPaint.top,
            paint.rcPaint.right - paint.rcPaint.left,
            paint.rcPaint.bottom - paint.rcPaint.top, BLACKNESS);
        EndPaint(window, &paint);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool transparency_allowed()
{
    HIGHCONTRASTW contrast{sizeof(contrast)};
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) ||
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) return false;
    BOOL composition = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) return false;
    DWORD enabled = 1;
    DWORD size = sizeof(enabled);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &enabled, &size);
    return enabled != 0;
}
}

bool CandidateBackdrop::supported()
{
    static const bool available = [] {
        using VersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        const auto version_fn = reinterpret_cast<VersionFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        OSVERSIONINFOEXW version{sizeof(version)};
        return version_fn && version_fn(reinterpret_cast<OSVERSIONINFOW*>(&version)) == 0 &&
            version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000 &&
            version.wProductType == VER_NT_WORKSTATION;
    }();
    return available;
}

bool CandidateBackdrop::update(HWND foreground, RECT panel_screen, bool enabled, bool dark)
{
    if (!kCandidateBackdropEnabled || !enabled || !foreground || !IsWindowVisible(foreground) ||
        IsRectEmpty(&panel_screen) || !supported() || !transparency_allowed()) {
        hide();
        return false;
    }
    if (!window_) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = backdrop_proc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = kBackdropClass;
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
            WS_EX_TRANSPARENT, kBackdropClass, L"", WS_POPUP | WS_DISABLED,
            0, 0, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window_) return false;
        const int corner = 2; // DWMWCP_ROUND（圆角）
        DwmSetWindowAttribute(window_, 33, &corner, sizeof(corner));
        const COLORREF border = 0xFFFFFFFE; // DWMWA_COLOR_NONE（无颜色）
        DwmSetWindowAttribute(window_, 34, &border, sizeof(border));
        const MARGINS glass{-1, -1, -1, -1};
        if (FAILED(DwmExtendFrameIntoClientArea(window_, &glass)) || !apply_blur(window_)) {
            reset();
            return false;
        }
        dark_ = !dark; // 确保初始颜色模式在下方被应用。
    }
    if (dark_ != dark) {
        const BOOL use_dark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(window_, 20, &use_dark, sizeof(use_dark));
        dark_ = dark;
    }
    // 没有所有权关系：隐藏这个可选表面绝不能隐藏
    // 文字窗口。显式插入使其位于候选墨迹之下。
    RECT previous{};
    GetWindowRect(window_, &previous);
    if (IsWindowVisible(window_) && EqualRect(&previous, &panel_screen) &&
        GetWindow(foreground, GW_HWNDNEXT) == window_) return true;
    if (!SetWindowPos(window_, foreground, panel_screen.left, panel_screen.top,
        panel_screen.right - panel_screen.left, panel_screen.bottom - panel_screen.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        hide();
        return false;
    }
    return true;
}

void CandidateBackdrop::hide()
{
    if (window_) ShowWindow(window_, SW_HIDE);
}

void CandidateBackdrop::reset()
{
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
}
}

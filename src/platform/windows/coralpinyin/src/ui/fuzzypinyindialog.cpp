#include "pch.h"
#include "fuzzypinyindialog.h"
#include "settingswindow.h"
#include "sheetbackdrop.h"
#include "soft_canvas.h"

#include <windowsx.h>
#include <array>
#include <memory>

using namespace coral::win32::soft;

namespace {
constexpr int kWidth = 420, kHeight = 430;
constexpr int kRowHeight = 40, kGap = 12, kPadding = 16, kFooter = 54;
constexpr int kCount = 19, kDone = 19, kNoHit = -1, kScroll = 20;
constexpr UINT_PTR kAnimation = 1;
const wchar_t* const kTitles[kCount] = {
    L"z = zh", L"c = ch", L"s = sh", L"l = n", L"g = k", L"f = h", L"r = l",
    L"an = ang", L"en = eng", L"in = ing", L"an = ai", L"eng = ong", L"on = ong",
    L"ian = iang", L"uan = uang", L"un = ong", L"un = iong",
    L"hui = fei", L"huang = wang"
};
constexpr int kStarts[] = { 0, 7, 17 };
constexpr int kSizes[] = { 7, 10, 2 };

struct Dialog {
    std::unique_ptr<Canvas> backdrop;
    HWND previousFocus = nullptr;
    std::vector<int> choice;
    std::array<float, kCount> from{};
    std::array<ULONGLONG, kCount> started{};
    float scale = 1.0f;
    int scroll = 0, wheel = 0;
    int hover = kNoHit, pressed = kNoHit, focused = kDone;
    bool keyboard = false, dragging = false;
    int grabY = 0, grabScroll = 0;
    int px(int value) const { return static_cast<int>(std::lround(value * scale)); }
};

RECT Panel(HWND hwnd, const Dialog& d) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int x = (rc.right - d.px(kWidth)) / 2, y = (rc.bottom - d.px(kHeight)) / 2;
    return { x, y, x + d.px(kWidth), y + d.px(kHeight) };
}
RECT View(HWND hwnd, const Dialog& d) {
    const auto p = Panel(hwnd, d);
    return { p.left + d.px(kPadding), p.top + d.px(kPadding),
        p.right - d.px(kPadding), p.bottom - d.px(kFooter) };
}
RECT Done(HWND hwnd, const Dialog& d) {
    const auto p = Panel(hwnd, d);
    return { p.right - d.px(82), p.bottom - d.px(41),
        p.right - d.px(16), p.bottom - d.px(13) };
}
int RowTop(int index) {
    return index * kRowHeight + (index >= 7 ? kGap : 0) + (index >= 17 ? kGap : 0);
}
int ContentHeight(const Dialog& d) { return d.px(kCount * kRowHeight + 2 * kGap); }
int MaxScroll(HWND hwnd, const Dialog& d) {
    const auto v = View(hwnd, d);
    return (std::max)(0, ContentHeight(d) - static_cast<int>(v.bottom - v.top));
}
RECT Row(HWND hwnd, const Dialog& d, int index) {
    const auto v = View(hwnd, d);
    const int y = v.top + d.px(RowTop(index)) - d.scroll;
    return { v.left, y, v.right - d.px(6), y + d.px(kRowHeight) };
}
RECT Thumb(HWND hwnd, const Dialog& d) {
    const auto v = View(hwnd, d);
    const int height = static_cast<int>(v.bottom - v.top);
    const int size = (std::max)(d.px(28), height * height / ContentHeight(d));
    const int top = v.top + (height - size) * d.scroll / (std::max)(1, MaxScroll(hwnd, d));
    const auto p = Panel(hwnd, d);
    return { p.right - d.px(9), top, p.right - d.px(3), top + size };
}
int Hit(HWND hwnd, const Dialog& d, POINT point) {
    const auto done = Done(hwnd, d);
    if (PtInRect(&done, point)) return kDone;
    const auto v = View(hwnd, d);
    const auto p = Panel(hwnd, d);
    if (point.y >= v.top && point.y < v.bottom && point.x >= p.right - d.px(14) && point.x < p.right)
        return kScroll;
    if (!PtInRect(&v, point)) return kNoHit;
    for (int i = 0; i < kCount; ++i) {
        const auto row = Row(hwnd, d, i);
        if (PtInRect(&row, point)) return i;
    }
    return kNoHit;
}
float Progress(const Dialog& d, int index) {
    const float to = d.choice[index] ? 1.0f : 0.0f;
    const float t = (std::min)(1.0f, static_cast<float>(GetTickCount64() - d.started[index]) / 140.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t);
    return d.from[index] + (to - d.from[index]) * eased;
}
void ScrollTo(HWND hwnd, Dialog& d, int value) {
    d.scroll = (std::clamp)(value, 0, MaxScroll(hwnd, d));
    InvalidateRect(hwnd, nullptr, FALSE);
}
void EnsureVisible(HWND hwnd, Dialog& d, int index) {
    if (index == kDone) return;
    const auto v = View(hwnd, d);
    const int top = d.px(RowTop(index)), height = static_cast<int>(v.bottom - v.top);
    if (top < d.scroll) ScrollTo(hwnd, d, top);
    else if (top + d.px(kRowHeight) > d.scroll + height)
        ScrollTo(hwnd, d, top + d.px(kRowHeight) - height);
}
void Toggle(HWND hwnd, Dialog& d, int index) {
    d.from[index] = Progress(d, index);
    d.started[index] = GetTickCount64();
    d.choice[index] = !d.choice[index];
    sConfig->setFuzzyChoice(d.choice);
    SetTimer(hwnd, kAnimation, 16, nullptr);
    InvalidateRect(hwnd, nullptr, FALSE);
}
using sheet::CaptureBackdrop;
void Paint(HWND hwnd, const Dialog& d, HDC target) {
    RECT client{}; GetClientRect(hwnd, &client);
    Canvas canvas(client.right, client.bottom);
    if (!canvas.valid()) return;
    auto surface = canvas.surface();
    const auto& skin = SettingsWindow::skin();
    sheet::PaintBackdrop(canvas, d.backdrop.get());
    const auto panel = Panel(hwnd, d), view = View(hwnd, d), done = Done(hwnd, d);
    const float radius = static_cast<float>(d.px(10));
    DrawShadow(surface, ToRectF(panel), radius, { 4, 16, 0, 0x30000000 }, d.scale);
    FillRoundRect(surface, ToRectF(panel), radius, skin.window);
    StrokeRoundRect(surface, ToRectF(panel), radius, 1, skin.lineStrong);

    surface.setClip(view);
    for (int group = 0; group < 3; ++group) {
        const auto first = Row(hwnd, d, kStarts[group]);
        RECT card{ first.left, first.top, first.right, first.top + d.px(kSizes[group] * kRowHeight) };
        FillRoundRect(surface, ToRectF(card), static_cast<float>(d.px(8)), skin.surface);
        StrokeRoundRect(surface, ToRectF(card), static_cast<float>(d.px(8)), 1, skin.line);
    }
    for (int i = 0; i < kCount; ++i) {
        const auto row = Row(hwnd, d, i);
        if (row.bottom <= view.top || row.top >= view.bottom) continue;
        if (d.hover == i || (d.keyboard && d.focused == i)) {
            RECT highlight{ row.left + d.px(2), row.top + d.px(2), row.right - d.px(2), row.bottom - d.px(2) };
            FillRoundRect(surface, ToRectF(highlight), static_cast<float>(d.px(6)), skin.hover);
            if (d.keyboard && d.focused == i)
                StrokeRoundRect(surface, ToRectF(highlight), static_cast<float>(d.px(6)), 1, skin.accent);
        }
        if (i != 6 && i != 16 && i != 18)
            FillRoundRect(surface, { static_cast<float>(row.left + d.px(16)), static_cast<float>(row.bottom - 1),
                static_cast<float>(row.right - d.px(16)), static_cast<float>(row.bottom) }, 0, skin.line);
        const float progress = Progress(d, i), height = static_cast<float>(d.px(18));
        const float right = static_cast<float>(row.right - d.px(16));
        const float top = static_cast<float>(row.top + (row.bottom - row.top - d.px(18)) / 2);
        const RectF track{ right - d.px(34), top, right, top + height };
        FillRoundRect(surface, track, height * 0.5f, Lerp(skin.darkCaption ? 0xFF62656A : 0xFFB0B0B0, skin.accent, progress));
        const float inset = static_cast<float>(d.px(2)), knob = height - 2 * inset;
        const float left = track.left + inset + (track.right - track.left - 2 * inset - knob) * progress;
        const RectF knobRect{ left, top + inset, left + knob, top + inset + knob };
        DrawShadow(surface, knobRect, knob * 0.5f, { 0.5f, 2, 0, 0x4D000000 }, d.scale);
        FillRoundRect(surface, knobRect, knob * 0.5f, 0xFFFFFFFF);
    }
    surface.resetClip();
    const auto thumb = Thumb(hwnd, d);
    FillRoundRect(surface, ToRectF(thumb), static_cast<float>(d.px(3)),
        d.hover == kScroll || d.dragging ? skin.scrollThumbHover : skin.scrollThumb);
    FillRoundRect(surface, { static_cast<float>(panel.left + 1), static_cast<float>(view.bottom),
        static_cast<float>(panel.right - 1), static_cast<float>(view.bottom + 1) }, 0, skin.line);
    const auto buttonColor = d.pressed == kDone ? skin.accentPressed : d.hover == kDone ? skin.accentHover : skin.accent;
    FillRoundRect(surface, ToRectF(done), static_cast<float>(d.px(5)), buttonColor);
    if (d.keyboard && d.focused == kDone) {
        RECT ring = done; InflateRect(&ring, d.px(3), d.px(3));
        StrokeRoundRect(surface, ToRectF(ring), static_cast<float>(d.px(7)), 1, skin.accent);
    }
    canvas.beginText();
    ScopedFont font(skin.fontName, d.px(14), FW_NORMAL);
    const auto oldFont = SelectObject(canvas.dc(), font.get());
    SetTextColor(canvas.dc(), ToColorRef(skin.text));
    IntersectClipRect(canvas.dc(), view.left, view.top, view.right, view.bottom);
    for (int i = 0; i < kCount; ++i) {
        auto row = Row(hwnd, d, i);
        row.left += d.px(16); row.right -= d.px(62);
        DrawTextW(canvas.dc(), kTitles[i], -1, &row, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectClipRgn(canvas.dc(), nullptr);
    SetTextColor(canvas.dc(), ToColorRef(skin.onAccent));
    RECT label = done;
    DrawTextW(canvas.dc(), L"完成", -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(canvas.dc(), oldFont);
    canvas.endText();
    canvas.blit(target, 0, 0);
}
} // namespace

void FuzzyPinyinDialog::showInstance(HWND parent) {
    if (!IsWindow(parent)) return;
    if (IsWindow(s_hwnd)) { SetFocus(s_hwnd); return; }
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"CoralPinyinFuzzyDlg";
    static const ATOM registered = RegisterClassExW(&wc);
    if (!registered) return;
    auto d = std::make_unique<Dialog>();
    d->previousFocus = GetFocus();
    d->scale = static_cast<float>(GetDpiForWindow(parent)) / 96.0f;
    d->choice = sConfig->getFuzzyChoice();
    d->choice.resize(kCount, 0);
    for (int i = 0; i < kCount; ++i) d->from[i] = d->choice[i] ? 1.0f : 0.0f;
    RECT client{}; GetClientRect(parent, &client);
    d->backdrop = CaptureBackdrop(parent);
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"模糊音设置",
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, client.right, client.bottom,
        parent, nullptr, wc.hInstance, d.get());
    if (!hwnd) return;
    d.release(); // 从这里开始由 WM_NCDESTROY 持有状态。
    s_hwnd = hwnd;
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(hwnd);
}
void FuzzyPinyinDialog::closeInstance() {
    if (IsWindow(s_hwnd)) DestroyWindow(s_hwnd);
}
void FuzzyPinyinDialog::resizeToParent() {
    if (!IsWindow(s_hwnd)) return;
    const HWND parent = GetParent(s_hwnd);
    RECT rc{}; GetClientRect(parent, &rc);
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(s_hwnd, GWLP_USERDATA));
    if (d) d->backdrop = CaptureBackdrop(parent, s_hwnd);
    SetWindowPos(s_hwnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_NOACTIVATE);
    InvalidateRect(s_hwnd, nullptr, FALSE);
}
LRESULT CALLBACK FuzzyPinyinDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!d) return DefWindowProcW(hwnd, msg, wParam, lParam);
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        Paint(hwnd, *d, dc); EndPaint(hwnd, &ps); return 0;
    }
    case WM_PRINTCLIENT: Paint(hwnd, *d, reinterpret_cast<HDC>(wParam)); return 0;
    case WM_SIZE: ScrollTo(hwnd, *d, d->scroll); return 0;
    case WM_MOUSEWHEEL:
        d->wheel += GET_WHEEL_DELTA_WPARAM(wParam);
        ScrollTo(hwnd, *d, d->scroll - d->px(3 * kRowHeight) * (d->wheel / WHEEL_DELTA));
        d->wheel %= WHEEL_DELTA; return 0;
    case WM_MOUSEMOVE: {
        const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (d->dragging) {
            const auto v = View(hwnd, *d), thumb = Thumb(hwnd, *d);
            const int travel = static_cast<int>(v.bottom - v.top - (thumb.bottom - thumb.top));
            ScrollTo(hwnd, *d, d->grabScroll + (p.y - d->grabY) * MaxScroll(hwnd, *d) / (std::max)(1, travel));
        } else {
            const int hover = Hit(hwnd, *d, p);
            if (hover != d->hover) { d->hover = hover; InvalidateRect(hwnd, nullptr, FALSE); }
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 }; TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE: d->hover = kNoHit; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd); d->keyboard = false;
        const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        d->pressed = Hit(hwnd, *d, p);
        if (d->pressed >= 0) SetCapture(hwnd);
        if (d->pressed == kScroll) {
            const auto thumb = Thumb(hwnd, *d);
            if (!PtInRect(&thumb, p)) ScrollTo(hwnd, *d, d->scroll + (p.y < thumb.top ? -1 : 1) * (View(hwnd, *d).bottom - View(hwnd, *d).top));
            d->dragging = true; d->grabY = p.y; d->grabScroll = d->scroll;
        } else if (d->pressed >= 0) d->focused = d->pressed;
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_LBUTTONUP: {
        const int pressed = d->pressed;
        const int hit = Hit(hwnd, *d, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        d->pressed = kNoHit; d->dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (pressed == hit && hit == kDone) { DestroyWindow(hwnd); return 0; }
        if (pressed == hit && hit >= 0 && hit < kCount) Toggle(hwnd, *d, hit);
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_CAPTURECHANGED: d->dragging = false; d->pressed = kNoHit; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_KEYDOWN:
        d->keyboard = true;
        switch (wParam) {
        case VK_ESCAPE: DestroyWindow(hwnd); return 0;
        case VK_TAB: d->focused = (d->focused + ((GetKeyState(VK_SHIFT) & 0x8000) ? kCount : 1)) % (kCount + 1); break;
        case VK_DOWN: d->focused = (std::min)(kDone, d->focused + 1); break;
        case VK_UP: d->focused = (std::max)(0, d->focused - 1); break;
        case VK_HOME: d->focused = 0; break;
        case VK_END: d->focused = kCount - 1; break;
        case VK_PRIOR: ScrollTo(hwnd, *d, d->scroll - d->px(320)); return 0;
        case VK_NEXT: ScrollTo(hwnd, *d, d->scroll + d->px(320)); return 0;
        case VK_SPACE: case VK_RETURN:
            if (d->focused == kDone) { DestroyWindow(hwnd); return 0; }
            Toggle(hwnd, *d, d->focused); return 0;
        default: return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
        EnsureVisible(hwnd, *d, d->focused); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_TIMER: {
        bool moving = false;
        for (int i = 0; i < kCount; ++i) if (Progress(*d, i) != (d->choice[i] ? 1.0f : 0.0f)) moving = true;
        if (!moving) KillTimer(hwnd, kAnimation);
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd, &p);
            SetCursor(LoadCursorW(nullptr, Hit(hwnd, *d, p) >= 0 ? IDC_HAND : IDC_ARROW)); return TRUE;
        }
        break;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY: {
        const HWND focus = d->previousFocus;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        s_hwnd = nullptr; delete d;
        if (IsWindow(focus) && IsWindowVisible(focus) && IsWindowEnabled(focus)) SetFocus(focus);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

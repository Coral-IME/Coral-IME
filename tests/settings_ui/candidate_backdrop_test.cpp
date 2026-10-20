#include "candidate_backdrop.h"
#include "candidate_view.h"
#include <dwmapi.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace coral::win32;

namespace
{
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

LRESULT CALLBACK test_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
        const COLORREF colors[]{RGB(255, 215, 180), RGB(175, 215, 250), RGB(190, 235, 195)};
        for (int i = 0; i < 3; ++i) {
            HBRUSH brush = CreateSolidBrush(colors[i]);
            RECT block{i * 300, 0, (i + 1) * 300, client.bottom};
            FillRect(dc, &block, brush);
            DeleteObject(brush);
        }
        for (int x = 0; x < client.right; x += 12) {
            RECT stripe{x, 0, x + 6, client.bottom};
            FillRect(dc, &stripe, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        }
        SetBkMode(dc, TRANSPARENT);
        for (int y = 0; y < client.bottom; y += 35)
            TextOutW(dc, 15, y, L"CORAL  background text  0123456789", 33);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void settle()
{
    // 让 DWM 合成测试持有的窗口，不派发用户输入。
    for (int i = 0; i < 8; ++i) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        DwmFlush();
        Sleep(25);
    }
}

std::vector<std::uint32_t> save_visual(const std::filesystem::path& path, HWND pattern)
{
    DWORD cloaked = 0;
    DwmGetWindowAttribute(pattern, 14 /*  DWMWA_CLOAKED（窗口遮蔽属性） */, &cloaked, sizeof(cloaked));
    require(!cloaked && WindowFromPoint(POINT{102, 102}) == pattern &&
        WindowFromPoint(POINT{997, 537}) == pattern,
        "Test region is not visibly covered by our pattern; refusing screen capture");
    soft::Canvas canvas(900, 440);
    HDC screen = GetDC(nullptr);
    BitBlt(canvas.dc(), 0, 0, 900, 440, screen, 100, 100, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, screen);
    SkinImage image;
    image.width = canvas.width(); image.height = canvas.height();
    image.pixels.assign(canvas.pixels(), canvas.pixels() + canvas.count());
    for (auto& pixel : image.pixels) pixel |= 0xFF000000;
    require(SaveSkinImagePng(image, path.wstring()), "Cannot save composed backdrop proof");
    return image.pixels;
}

double stripe_contrast(const std::vector<std::uint32_t>& pixels, RECT panel)
{
    const int y = panel.top + 5 - 100;
    const int begin = panel.left + 40 - 100;
    const int end = (std::min)(panel.right - 40 - 100, 898L);
    double sum = 0;
    for (int x = begin; x < end; ++x) {
        const auto a = pixels[y * 900 + x], b = pixels[y * 900 + x + 1];
        for (int shift : {0, 8, 16})
            sum += std::abs(static_cast<int>((a >> shift) & 255) - static_cast<int>((b >> shift) & 255));
    }
    return sum / (3 * (end - begin));
}
}

int main(int argc, char** argv)
{
    const bool visual = argc == 3 && std::string(argv[1]) == "--visual";
    HWND pattern = nullptr, ink = nullptr;
    try {
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Cannot initialize image codecs");
        if (visual) std::filesystem::create_directories(argv[2]);
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const auto foreground_before = GetForegroundWindow();
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = test_proc;
        cls.lpszClassName = L"Coral.CandidateBackdrop.Test";
        RegisterClassW(&cls);
        const int left = visual ? 100 : -30000;
        const int top = visual ? 100 : -30000;
        pattern = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            cls.lpszClassName, L"", WS_POPUP, left, top, 900, 440,
            nullptr, nullptr, cls.hInstance, nullptr);
        ink = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC", L"", WS_POPUP, left + 30, top + 50, 800, 200,
            nullptr, nullptr, cls.hInstance, nullptr);
        require(pattern && ink, "Cannot create test windows");
        ShowWindow(pattern, SW_SHOWNOACTIVATE);
        UpdateWindow(pattern);
        CandidateView view;
        coral::composition::CandidateRenderSnapshot snapshot;
        snapshot.visible = true; snapshot.fontSize = 16;
        snapshot.skin = "k:s|s:5|o:45";
        for (const char* text : {"珊瑚", "毛玻璃", "候选词", "清晰"}) {
            coral::composition::RenderCandidate item;
            item.id = snapshot.candidates.size() + 1; item.text = text;
            snapshot.candidates.push_back(item);
        }
        view.update(ink, snapshot);
        const POINT position{left + 25, top + 125};
        view.present(ink, position);
        SetWindowPos(ink, HWND_TOPMOST, position.x, position.y, view.width(), view.height(),
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        CandidateBackdrop backdrop;
        RECT panel = view.panelRect();
        OffsetRect(&panel, position.x, position.y);
        require(!backdrop.update(ink, panel, false, false) && !backdrop.window(),
            "Disabled blur created a backdrop window");
        settle();
        std::vector<std::uint32_t> before;
        if (visual) before = save_visual(std::filesystem::path(argv[2]) / "candidate-before.png", pattern);
        const HWND focus_before_effect = GetForegroundWindow();
        const bool active = backdrop.update(ink, panel, true, false);
        require(GetForegroundWindow() == focus_before_effect, "Enabling blur stole keyboard focus");
        std::cout << "supported=" << CandidateBackdrop::supported() << " active=" << active << '\n';
        if (active) {
            RECT bounds{};
            GetWindowRect(backdrop.window(), &bounds);
            require(EqualRect(&panel, &bounds), "Blur bounds differ from candidate panel");
            require(GetWindow(ink, GW_HWNDNEXT) == backdrop.window(), "Blur is not directly below candidate ink");
            require(!IsWindowEnabled(backdrop.window()), "Backdrop can receive input");
            settle();
            if (visual) {
                const auto after = save_visual(std::filesystem::path(argv[2]) / "candidate-frosted.png", pattern);
                const auto before_contrast = stripe_contrast(before, panel);
                const auto after_contrast = stripe_contrast(after, panel);
                std::cout << "stripe contrast " << before_contrast << " -> " << after_contrast << '\n';
                require(before_contrast > 5 && after_contrast < before_contrast * 0.4,
                    "Visible compositor did not blur the background stripes");
                const int y = panel.top + 5 - 100;
                const auto a = after[y * 900 + panel.left + 80 - 100];
                const auto b = after[y * 900 + panel.right - 80 - 100];
                int color_difference = 0;
                for (int shift : {0, 8, 16})
                    color_difference = (std::max)(color_difference,
                        std::abs(static_cast<int>((a >> shift) & 255) - static_cast<int>((b >> shift) & 255)));
                std::cout << "background color difference=" << color_difference << '\n';
                require(color_difference > 5,
                    "Backdrop became a solid fill instead of transmitting background colors");
            }
            OffsetRect(&panel, 15, 10);
            require(backdrop.update(ink, panel, true, true), "Cannot update dark/moved backdrop");
            GetWindowRect(backdrop.window(), &bounds);
            require(EqualRect(&panel, &bounds), "Backdrop did not follow movement");
            snapshot.expanded = true;
            view.update(ink, snapshot);
            const POINT expanded_position{position.x + 15, position.y + 10};
            view.present(ink, expanded_position);
            SetWindowPos(ink, HWND_TOPMOST, expanded_position.x, expanded_position.y,
                view.width(), view.height(), SWP_NOACTIVATE);
            panel = view.panelRect();
            OffsetRect(&panel, expanded_position.x, expanded_position.y);
            require(backdrop.update(ink, panel, true, false), "Cannot follow expanded candidate layout");
            GetWindowRect(backdrop.window(), &bounds);
            require(EqualRect(&panel, &bounds), "Expanded blur bounds differ from candidate panel");
            ShowWindow(ink, SW_HIDE);
            require(!backdrop.update(ink, panel, true, false) && !IsWindowVisible(backdrop.window()),
                "Hidden candidate left a visible blur layer");
            ShowWindow(ink, SW_SHOWNOACTIVATE);
            backdrop.update(ink, panel, false, true);
            require(!IsWindowVisible(backdrop.window()) && IsWindowVisible(ink),
                "Disabling blur hid candidate ink or left an orphan surface");
        } else {
            require(!backdrop.window() || !IsWindowVisible(backdrop.window()),
                "Unsupported effect left a visible backdrop");
        }
        const HWND old = backdrop.window();
        backdrop.reset();
        require(!backdrop.window() && (!old || !IsWindow(old)), "Backdrop window was not released");
        DWORD foreground_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
        require(GetForegroundWindow() == foreground_before || foreground_pid != GetCurrentProcessId(),
            "Test windows acquired keyboard focus");
        DestroyWindow(ink); DestroyWindow(pattern);
        CoUninitialize();
        std::cout << "[PASS] backdrop gating, placement, focus, hide and teardown\n";
        return 0;
    } catch (const std::exception& error) {
        if (ink) DestroyWindow(ink);
        if (pattern) DestroyWindow(pattern);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

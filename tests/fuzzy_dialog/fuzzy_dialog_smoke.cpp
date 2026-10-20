// 用内存中的配置替身编译生产窗口过程。
// 消息演练真实的 Win32 创建、输入、渲染与销毁。
#include "pch.h"
#include "../../src/platform/windows/coralpinyin/src/ui/fuzzypinyindialog.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void Click(HWND hwnd, POINT point) {
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
}
POINT Center(const RECT& r) { return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 }; }
void SavePanel(HWND hwnd, Dialog& d, const std::filesystem::path& path, bool full = false) {
    RECT client{}; GetClientRect(hwnd, &client);
    Canvas canvas(client.right, client.bottom);
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.dc()), PRF_CLIENT);
    GdiFlush();
    const auto panel = full ? client : Panel(hwnd, d);
    const int width = panel.right - panel.left, height = panel.bottom - panel.top;
    BITMAPFILEHEADER header{};
    header.bfType = 0x4D42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info); info.biWidth = width; info.biHeight = -height;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<char*>(&header), sizeof(header));
    file.write(reinterpret_cast<char*>(&info), sizeof(info));
    for (int y = panel.top; y < panel.bottom; ++y)
        file.write(reinterpret_cast<char*>(canvas.pixels() + y * canvas.width() + panel.left), width * 4);
    Require(static_cast<bool>(file), "Cannot save rendering");
}

LRESULT CALLBACK BackdropOwnerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PRINTCLIENT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{}; GetClientRect(hwnd, &client);
        // 与设置主窗口一样，只绘制侧边栏；页面覆盖其余部分。
        client.right = 64;
        HBRUSH background = CreateSolidBrush(RGB(230, 235, 240));
        FillRect(dc, &client, background); DeleteObject(background);
        RECT marker{ 12, 12, 28, 28 };
        HBRUSH ink = CreateSolidBrush(RGB(20, 80, 160));
        FillRect(dc, &marker, ink); DeleteObject(ink);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
LRESULT CALLBACK BackdropChildProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PRINTCLIENT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{}; GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(50, 160, 90));
        FillRect(dc, &client, background); DeleteObject(background);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
void CheckBackdrop(HWND parent, const Dialog& d, HWND viewport, HWND content) {
    Require(d.backdrop && d.backdrop->valid(), "Missing backdrop");
    GdiFlush();
    const auto pixel = [&](int x, int y) {
        return d.backdrop->pixels()[y * d.backdrop->width() + x] & 0x00FFFFFF;
    };
    Require(pixel(2, 2) == 0xE6EBF0, "Non-client offset left a black border in backdrop");
    Require(pixel(16, 16) == 0x1450A0, "Owner content shifted by caption/frame offset");
    Require(pixel(d.backdrop->width() - 2, 2) == (SettingsWindow::skin().window & 0x00FFFFFF),
        "Unpainted client area became a black border");
    RECT client{}; GetClientRect(parent, &client);
    Require(d.backdrop->width() == client.right && d.backdrop->height() == client.bottom,
        "Backdrop size is stale after resize");
    POINT point{ 10, 90 }; MapWindowPoints(content, parent, &point, 1);
    Require(pixel(point.x, point.y) == 0x32A05A, "Scrolled descendant rendered at wrong origin");
    POINT clipped{ 10, 10 }; MapWindowPoints(viewport, parent, &clipped, 1);
    // 嵌套内容从 y=-80 开始，但始终裁剪在其视口内。
    Require(pixel(clipped.x, clipped.y - 20) == (SettingsWindow::skin().window & 0x00FFFFFF), "Scrolled content escaped its viewport");
}

int main(int argc, char** argv) {
    HWND parent = nullptr;
    try {
        Require(argc == 2, "Expected render output directory");
        const auto output = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(output);
        WNDCLASSW ownerClass{};
        ownerClass.lpfnWndProc = BackdropOwnerProc;
        ownerClass.hInstance = GetModuleHandleW(nullptr);
        ownerClass.lpszClassName = L"FuzzyBackdropTestOwner";
        Require(RegisterClassW(&ownerClass) != 0, "Cannot register test owner");
        WNDCLASSW childClass = ownerClass;
        childClass.lpfnWndProc = BackdropChildProc;
        childClass.lpszClassName = L"FuzzyBackdropTestChild";
        Require(RegisterClassW(&childClass) != 0, "Cannot register test child");
        parent = CreateWindowExW(0, ownerClass.lpszClassName, L"Dialog test owner",
            WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME,
            0, 0, 1000, 700, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(parent != nullptr, "Cannot create test parent");
        const HWND viewport = CreateWindowExW(0, childClass.lpszClassName, L"Viewport",
            WS_CHILD | WS_VISIBLE, 100, 110, 200, 120, parent, nullptr, ownerClass.hInstance, nullptr);
        const HWND content = CreateWindowExW(0, childClass.lpszClassName, L"Scrolled content",
            WS_CHILD | WS_VISIBLE, 0, -80, 180, 300, viewport, nullptr, ownerClass.hInstance, nullptr);
        Require(viewport && content, "Cannot create nested test controls");
        RECT beforeViewport{}, beforeContent{};
        GetWindowRect(viewport, &beforeViewport); GetWindowRect(content, &beforeContent);
        auto open = [&]() {
            FuzzyPinyinDialog::showInstance(parent);
            const HWND child = FindWindowExW(parent, nullptr, L"CoralPinyinFuzzyDlg", nullptr);
            Require(child != nullptr, "Missing child dialog");
            return child;
        };
        HWND child = open();
        Require(GetParent(child) == parent, "Dialog is not embedded");
        const auto style = GetWindowLongPtrW(child, GWL_STYLE);
        Require((style & WS_CHILD) && !(style & (WS_CAPTION | WS_POPUP)), "Unexpected top-level chrome");
        auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(child, GWLP_USERDATA));
        CheckBackdrop(parent, *d, viewport, content);
        RECT afterViewport{}, afterContent{};
        GetWindowRect(viewport, &afterViewport); GetWindowRect(content, &afterContent);
        Require(EqualRect(&beforeViewport, &afterViewport) && EqualRect(&beforeContent, &afterContent),
            "Opening sheet moved existing controls");
        d->scale = 1; // 确定性的 96-DPI 渲染
        const auto panel = Panel(child, *d);
        Require(panel.right - panel.left == 420 && panel.bottom - panel.top == 430, "Incorrect panel dimensions");
        const RECT footer = Done(child, *d);
        SavePanel(child, *d, output / "fuzzy-light.bmp");
        SavePanel(child, *d, output / "fuzzy-background.bmp", true);
        RECT client{}; GetClientRect(child, &client);
        Canvas composited(client.right, client.bottom);
        Paint(child, *d, composited.dc()); GdiFlush();
        const std::uint32_t corner = Over(0x48000000, 0xFFE6EBF0);
        Require(composited.pixels()[2 * composited.width() + 2] == corner,
            "Mask shifted or corrupted the background corner");
        Click(child, Center(Row(child, *d, 0)));
        Require(testConfig.writes == 1 && testConfig.choice[0] == 1, "First switch did not persist");
        for (int i = 1; i < 19; ++i) Require(testConfig.choice[i] == 0, "Wrong config index modified");
        // 用与按键相同的展开/滚动行为遍历每一项。
        for (int i = 1; i < 19; ++i) {
            EnsureVisible(child, *d, i);
            const auto row = Row(child, *d, i), view = View(child, *d);
            Require(row.top >= view.top && row.bottom <= view.bottom, "Item is inaccessible");
            Click(child, Center(row));
            Require(testConfig.choice[i] == 1, "Switch index mapping failed");
        }
        Require(testConfig.writes == 19, "Wrong number of immediate saves");
        ScrollTo(child, *d, 0);
        auto thumb = Thumb(child, *d);
        POINT grab = Center(thumb);
        SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(grab.x, grab.y));
        SendMessageW(child, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(grab.x, View(child, *d).bottom + 300));
        SendMessageW(child, WM_LBUTTONUP, 0, MAKELPARAM(grab.x, View(child, *d).bottom + 300));
        Require(d->scroll == MaxScroll(child, *d), "Scrollbar cannot reach bottom");
        const RECT currentFooter = Done(child, *d);
        Require(EqualRect(&footer, &currentFooter), "Footer scrolled with content");
        SavePanel(child, *d, output / "fuzzy-bottom.bmp");
        SendMessageW(child, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        Require(d->scroll < MaxScroll(child, *d), "Mouse wheel did not scroll");
        SendMessageW(child, WM_KEYDOWN, VK_HOME, 0);
        Require(d->focused == 0 && d->scroll == 0, "Home did not reveal first item");
        SendMessageW(child, WM_KEYDOWN, VK_SPACE, 0);
        Require(testConfig.choice[0] == 0 && testConfig.writes == 20, "Keyboard toggle failed");
        SendMessageW(child, WM_KEYDOWN, VK_END, 0);
        Require(d->focused == 18 && d->scroll == MaxScroll(child, *d), "End did not reveal last item");
        Click(child, Center(Done(child, *d)));
        Require(!IsWindow(child), "Done did not close dialog");
        child = open();
        d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(child, GWLP_USERDATA));
        Require(d->scroll == 0 && d->choice == testConfig.choice, "Reopen lost config or scroll reset");
        auto& skin = SettingsWindow::testSkin;
        skin.darkCaption = true; skin.window = 0xFF191B20; skin.surface = 0xFF25282E;
        skin.hover = 0xFF30343C; skin.text = 0xFFF2F4F8; skin.line = 0x26FFFFFF;
        skin.lineStrong = 0x40FFFFFF;
        SavePanel(child, *d, output / "fuzzy-dark.bmp");
        d->scale = 1.5f;
        SetWindowPos(parent, nullptr, 0, 0, 1600, 1000, SWP_NOMOVE | SWP_NOZORDER);
        FuzzyPinyinDialog::resizeToParent();
        CheckBackdrop(parent, *d, viewport, content); // 不包括已经可见的掩码。
        const auto large = Panel(child, *d);
        Require(large.right - large.left == 630 && large.bottom - large.top == 645, "DPI scale failed");
        SavePanel(child, *d, output / "fuzzy-dark-150.bmp");
        SendMessageW(child, WM_KEYDOWN, VK_ESCAPE, 0);
        Require(!IsWindow(child), "Escape did not close dialog");
        child = open();
        DestroyWindow(parent); parent = nullptr;
        Require(!IsWindow(child), "Parent destruction left dialog behind");
        std::cout << "PASS: child ownership, sizing, 19 config indices, wheel/drag/keyboard, close/reopen and DPI renders\n";
        return 0;
    } catch (const std::exception& e) {
        if (parent) DestroyWindow(parent);
        std::cerr << e.what() << '\n'; return 1;
    }
}

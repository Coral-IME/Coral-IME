#include "candidate_view.h"
#include "coral/composition/assist_hints.h"
#include <filesystem>
#include <fstream>
#include <iostream>

static bool Save(coral::win32::CandidateView& view, const std::filesystem::path& path) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = view.width();
    info.bmiHeader.biHeight = -view.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC dc = ::CreateCompatibleDC(nullptr);
    HBITMAP bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) { ::DeleteDC(dc); return false; }
    const auto old = ::SelectObject(dc, bitmap);
    view.draw(dc, static_cast<std::uint32_t*>(pixels));
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + view.width() * view.height() * 4;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    file.write(static_cast<const char*>(pixels), view.width() * view.height() * 4);
    ::SelectObject(dc, old);
    ::DeleteObject(bitmap);
    ::DeleteDC(dc);
    return static_cast<bool>(file);
}
static std::string Utf8(std::wstring_view text) {
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(bytes, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (false)
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::filesystem::create_directories(argv[1]);
    HWND window = ::CreateWindowExW(0, L"STATIC", L"UV render test", WS_POPUP,
        0, 0, 640, 480, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    CHECK(window);
    using namespace coral::composition;
    CandidateRenderSnapshot snapshot;
    snapshot.context = {1, 1, 1};
    snapshot.fontSize = 16;
    snapshot.fontName = "Microsoft YaHei";
    snapshot.theme = "light";
    snapshot.candidates = {{1, u8"你好", "ni'hao"}};
    coral::win32::CandidateView view;
    view.update(window, snapshot);
    const int normalHeight = view.height();
    snapshot.candidates = {{1, "u", "u"}};
    snapshot.hint = Utf8(specialModeHint(SpecialMode::Stroke, L"u", false));
    view.update(window, snapshot);
    CHECK(view.height() > normalHeight && view.rowCount() == 1);
    CHECK(view.hit({view.panelRect().left + 20, view.panelRect().bottom - 10}) == -1);
    CHECK(Save(view, std::filesystem::path(argv[1]) / "u.bmp"));
    snapshot.letterSelection = true;
    ++snapshot.context.contextGeneration;
    snapshot.candidates = {{1, u8"一百二十三", "v123"}, {2, u8"壹百贰十叁", "v123"},
        {3, u8"一二三", "v123"}, {4, "123", "v123"}, {5, "CXXIII", "v123"}};
    snapshot.hint = Utf8(specialModeHint(SpecialMode::Convert, L"v123", true));
    view.update(window, snapshot);
    CHECK(view.rowCount() >= 1 && view.itemCenters().size() == snapshot.candidates.size());
    CHECK(Save(view, std::filesystem::path(argv[1]) / "v.bmp"));
    snapshot.theme = "dark";
    snapshot.expanded = true;
    snapshot.fontSize = 24;
    view.update(window, snapshot);
    CHECK(Save(view, std::filesystem::path(argv[1]) / "v-dark-large.bmp"));
    snapshot.theme = "light";
    snapshot.fontSize = 16;
    snapshot.letterSelection = false;
    snapshot.listLayout = true;
    snapshot.hasMore = true;
    snapshot.hint.clear();
    ++snapshot.context.contextGeneration;
    snapshot.candidates.clear();
    snapshot.candidates.push_back({1, u8"广东省广州市海珠区琶洲街道保利国际广场南塔 5 楼 张三 13800000000 这是一段很长的地址需要省略", ""});
    for (std::uint64_t i = 2; i <= 10; ++i)
        snapshot.candidates.push_back({i, u8"剪贴板记录 " + std::to_string(i), ""});
    snapshot.selection = 1;
    view.update(window, snapshot);
    CHECK(view.rowCount() == static_cast<int>(snapshot.candidates.size()));
    CHECK(view.itemCenters().size() == snapshot.candidates.size());
    CHECK(Save(view, std::filesystem::path(argv[1]) / "clipboard-list.bmp"));
    snapshot.listLayout = false;
    snapshot.hasMore = false;
    snapshot.selection = 0;
    snapshot.candidates.clear();
    snapshot.expanded = false;
    snapshot.hint = Utf8(specialModeHint(SpecialMode::Symbols, L"uu", false));
    view.update(window, snapshot);
    CHECK(view.width() > 0 && view.rowCount() == 0 && view.hit({50, 50}) == -1);
    const RECT expand = view.expandRect();
    CHECK(::IsRectEmpty(&expand));
    CHECK(Save(view, std::filesystem::path(argv[1]) / "uu-hint-only.bmp"));
    snapshot.hint.clear();
    view.update(window, snapshot);
    CHECK(view.width() == 0 && view.height() == 0);
    ::DestroyWindow(window);
    std::cout << "Candidate hint layout, hit testing and empty-state checks passed\n";
}

#include "candidate_view.h"
#include "coral/composition/assist_hints.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>

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
    snapshot.expanded = false;
    // 同时核对引擎数量和真实字体排版，短词必须占满初始行且完整显示。
    for (const char* font : {"Microsoft YaHei UI", "Microsoft YaHei", "SimSun", "DengXian"}) {
        snapshot.fontName = font;
        for (int font_size : {12, 16, 24, 28}) {
            snapshot.fontSize = font_size;
            snapshot.letterSelection = true;
            for (const auto [length, expected] : {std::pair{2,7}, {3,6}, {4,5}, {8,3}, {12,2}, {13,2}, {14,1}, {28,1}}) {
                const std::wstring word(length, L'\x4f60');
                const auto text = Utf8(word);
                ComposedCandidateCapacity capacity;
                snapshot.candidates.clear();
                while (capacity.try_append(text))
                    snapshot.candidates.push_back({snapshot.candidates.size() + 1, text, ""});
                CHECK(snapshot.candidates.size() == static_cast<size_t>(expected));
                view.update(window, snapshot);
                CHECK(view.rowCount() == 1 && view.visibleRows() == 1);
                HDC dc = ::GetDC(window);
                const UINT dpi = ::GetDpiForWindow(window);
                const std::wstring font_name(font, font + std::strlen(font));
                const auto scale = [&](int value) { return ::MulDiv(::MulDiv(value, dpi, 96), font_size, 16); };
                HFONT word_font = ::CreateFontW(-::MulDiv(font_size, dpi, 96), 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, font_name.c_str());
                const auto old_font = ::SelectObject(dc, word_font);
                SIZE word_size{}, index_size{};
                CHECK(::GetTextExtentPoint32W(dc, word.c_str(), length, &word_size));
                HFONT index_font = ::CreateFontW(-scale(12), 0, 0, 0, FW_NORMAL,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, font_name.c_str());
                ::SelectObject(dc, index_font);
                CHECK(::GetTextExtentPoint32W(dc, L"W", 1, &index_size));
                int cell_width = scale(CandidateRowMetrics::kPanelWidth) - scale(CandidateRowMetrics::kPaddingX) * 2 -
                    scale(CandidateRowMetrics::kToggleGap) - scale(CandidateRowMetrics::kToggleSize);
                const auto& centers = view.itemCenters();
                for (size_t i = 1; i < centers.size(); ++i)
                    cell_width = (std::min)(cell_width, static_cast<int>(centers[i] - centers[i - 1]));
                CHECK(word_size.cx + scale(8) * 2 + scale(5) + index_size.cx <= cell_width);
                ::SelectObject(dc, old_font);
                ::DeleteObject(word_font);
                ::DeleteObject(index_font);
                ::ReleaseDC(window, dc);
            }
        }
    }
    snapshot.fontName = "Microsoft YaHei UI";
    snapshot.fontSize = 16;
    snapshot.letterSelection = false;
    const std::vector<std::vector<std::string>> examples{
        {u8"输入法",u8"候选词",u8"新功能",u8"看电影",u8"去公园",u8"吃晚饭"},
        {u8"今天下雨",u8"明天晴天",u8"一起出发",u8"测试候选",u8"完整显示"},
        {u8"今天下午我们一起去公园吧",u8"明天上午我们一起去图书馆"}};
    for (size_t example = 0; example < examples.size(); ++example) {
        snapshot.candidates.clear();
        for (const auto& word : examples[example])
            snapshot.candidates.push_back({snapshot.candidates.size() + 1, word, ""});
        view.update(window, snapshot);
        CHECK(view.rowCount() == 1 && view.visibleRows() == 1);
        CHECK(Save(view, std::filesystem::path(argv[1]) / ("capacity-" + std::to_string(example) + ".bmp")));
    }
    snapshot.candidates.insert(snapshot.candidates.begin(), {99, Utf8(std::wstring(60, L'\x8bcd')), ""});
    view.update(window, snapshot);
    CHECK(view.rowCount() == 2 && view.rowStarts()[1] == 1 && view.itemCenters().size() == 3);
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

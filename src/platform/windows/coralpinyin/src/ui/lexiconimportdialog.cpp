#include "pch.h"
#include "lexiconimportdialog.h"
#include "coral/lexicon_io/text_import.h"
#include "settingswindow.h"
#include "sheetbackdrop.h"
#include "soft_canvas.h"
#include "core_operations.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <array>
#include <memory>
#include <unordered_map>

using namespace coral::win32::soft;

namespace {
enum class Stage { Select, Preview, Done, Failures };
enum class Filter { All, Check, Skip };
enum : int {
    kNoHit = -1, kClose, kPick, kDrop, kAutoPinyin, kSkipExisting, kExamples,
    kChange, kTabAll, kTabCheck, kTabSkip, kCancel, kImport, kScroll,
    kFailures, kFinish, kUndo, kBack, kRow = 1000,
};
enum FontId { FTitle, FBody, FBodyBold, FSmall, FNumber, FCount };
constexpr int kFontSizes[FCount] = { 18, 14, 14, 12, 22 };
constexpr int kFontWeights[FCount] = { FW_BOLD, FW_NORMAL, FW_BOLD, FW_NORMAL, FW_BOLD };
constexpr int kRowHeight = 34;
constexpr int kGutter = 10;
constexpr UINT kEditDone = WM_APP + 1;
constexpr UINT kText = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
constexpr UINT kCenter = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
constexpr UINT kRight = DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;

// 24 单位线性图标
constexpr const char* kIconBook[] = {
    "M4 5.5A2.5 2.5 0 0 1 6.5 3H11v16H6.5A2.5 2.5 0 0 0 4 21.5v-16Z"
    "M20 5.5A2.5 2.5 0 0 0 17.5 3H13v16h4.5a2.5 2.5 0 0 1 2.5 2.5v-16Z" };
constexpr const char* kIconClose[] = { "M6 6l12 12M18 6L6 18" };
constexpr const char* kIconUpload[] = {
    "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z", "M14 3v5h5", "M12 18v-6M9.5 14.5 12 12l2.5 2.5" };
constexpr const char* kIconFile[] = {
    "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z", "M14 3v5h5", "M9 13h6M9 17h4" };
constexpr const char* kIconCheckCircle[] = { "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0", "M8 12.5l2.5 2.5L16 9.5" };
constexpr const char* kIconCheck[] = { "M5 12.5l4.5 4.5L19 7.5" };
constexpr const char* kIconInfo[] = { "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0", "M12 11v5M12 7.5h.01" };
constexpr const char* kIconArrow[] = { "M5 12h14M13 6l6 6-6 6" };

struct Dialog {
    std::unique_ptr<Canvas> backdrop;
    HWND previousFocus = nullptr;
    float scale = 1.0f;
    Stage stage = Stage::Select;
    UserLexiconImportOptions options;
    coral::LanguageVariant charset = coral::LanguageVariant::Simplified;
    bool examples = false;
    std::wstring error;
    coral::lexicon_io::ImportedText file;
    Filter filter = Filter::All;
    std::vector<int> rows;  // 当前列表展示的 items 下标
    int scroll = 0, wheel = 0;
    int hover = kNoHit, pressed = kNoHit;
    bool dragging = false;
    int grabY = 0, grabScroll = 0;
    int ready = 0, exists = 0, repeated = 0, invalid = 0, polyphonic = 0;
    UserLexiconImportReport report;
    bool reverted = false, undoFailed = false;
    HWND edit = nullptr;
    int editing = -1;  // 正在修改拼音的 items 下标
    std::unique_ptr<ScopedFont> editFont;
    HBRUSH editBrush = nullptr;
    int px(int value) const { return static_cast<int>(std::lround(value * scale)); }
};

std::uint32_t Green(const SettingsSkin& skin) { return skin.darkCaption ? 0xFF6CCB75 : 0xFF2E7D32; }
std::uint32_t Orange(const SettingsSkin& skin) { return skin.darkCaption ? 0xFFF0B04A : 0xFFB45F06; }
std::uint32_t Tint(std::uint32_t color, std::uint32_t alpha = 0x1F) { return (color & 0x00FFFFFF) | (alpha << 24); }

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>((std::max)(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}
std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>((std::max)(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::wstring Number(int value) {
    std::wstring text = std::to_wstring(value);
    for (int i = static_cast<int>(text.size()) - 3; i > 0; i -= 3) text.insert(static_cast<size_t>(i), L",");
    return text;
}
const wchar_t* CharsetName(coral::LanguageVariant charset) {
    return charset == coral::LanguageVariant::Traditional ? L"繁体" : L"简体";
}
bool NeedsCheck(const UserLexiconImportItem& item) {
    return item.status == UserLexiconImportStatus::Invalid ||
        (item.status == UserLexiconImportStatus::Ready && item.polyphonic);
}
bool Skipped(const UserLexiconImportItem& item) {
    return item.status == UserLexiconImportStatus::Exists || item.status == UserLexiconImportStatus::Repeated;
}

// ---- 布局（96 DPI 逻辑像素，按 scale 缩放） ----
SIZE PanelSize(Stage stage) {
    switch (stage) {
    case Stage::Select: return { 600, 548 };
    case Stage::Done: return { 520, 340 };
    default: return { 640, 640 };
    }
}
RECT Panel(HWND hwnd, const Dialog& d) {
    RECT rc{}; GetClientRect(hwnd, &rc);
    const SIZE size = PanelSize(d.stage);
    const int w = (std::min)(d.px(size.cx), static_cast<int>(rc.right) - d.px(24));
    const int h = (std::min)(d.px(size.cy), static_cast<int>(rc.bottom) - d.px(24));
    const int x = (rc.right - w) / 2, y = (rc.bottom - h) / 2;
    return { x, y, x + w, y + h };
}
// 正值相对面板左/上边，负值相对右/下边
RECT Box(const RECT& p, const Dialog& d, int left, int top, int right, int bottom) {
    const auto x = [&](int v) { return v >= 0 ? p.left + d.px(v) : p.right - d.px(-v); };
    const auto y = [&](int v) { return v >= 0 ? p.top + d.px(v) : p.bottom - d.px(-v); };
    return { x(left), y(top), x(right), y(bottom) };
}
RECT ListHeader(const RECT& p, const Dialog& d) {
    return d.stage == Stage::Failures ? Box(p, d, 24, 88, -24, 114) : Box(p, d, 24, 244, -24, 270);
}
RECT ListView(HWND hwnd, const Dialog& d) {
    const auto p = Panel(hwnd, d);
    return d.stage == Stage::Failures ? Box(p, d, 24, 114, -24, -76) : Box(p, d, 24, 270, -24, -108);
}
int MaxScroll(HWND hwnd, const Dialog& d) {
    const auto v = ListView(hwnd, d);
    return (std::max)(0, static_cast<int>(d.rows.size()) * d.px(kRowHeight) - static_cast<int>(v.bottom - v.top));
}
RECT RowRect(HWND hwnd, const Dialog& d, int index) {
    const auto v = ListView(hwnd, d);
    const int y = v.top + index * d.px(kRowHeight) - d.scroll;
    return { v.left, y, v.right - d.px(kGutter), y + d.px(kRowHeight) };
}
RECT Thumb(HWND hwnd, const Dialog& d) {
    const auto v = ListView(hwnd, d);
    const int height = static_cast<int>(v.bottom - v.top);
    const int content = (std::max)(1, static_cast<int>(d.rows.size()) * d.px(kRowHeight));
    const int size = (std::max)(d.px(28), height * height / content);
    const int top = v.top + (height - size) * d.scroll / (std::max)(1, MaxScroll(hwnd, d));
    return { v.right - d.px(6), top, v.right, top + size };
}
struct Cells { RECT line, phrase, pinyin, status; };
Cells CellsOf(const RECT& row, const Dialog& d) {
    Cells c{};
    const int lineWidth = d.px(52);
    c.line = { row.left + d.px(8), row.top, row.left + lineWidth, row.bottom };
    c.phrase = { row.left + lineWidth, row.top, row.left + lineWidth + (row.right - row.left - lineWidth) * 30 / 100, row.bottom };
    c.status = d.stage == Stage::Failures ? RECT{ row.right, row.top, row.right, row.bottom }
        : RECT{ row.right - d.px(80), row.top, row.right - d.px(8), row.bottom };
    c.pinyin = { c.phrase.right + d.px(8), row.top, c.status.left - d.px(8), row.bottom };
    return c;
}
RECT PickButton(const RECT& p, const Dialog& d) {
    const auto drop = Box(p, d, 24, 90, -24, 270);
    const int cx = (drop.left + drop.right) / 2;
    return { cx - d.px(64), drop.top + d.px(124), cx + d.px(64), drop.top + d.px(160) };
}
RECT ExamplesCard(const RECT& p, const Dialog& d) { return Box(p, d, 24, 330, -24, 488); }

std::vector<std::pair<int, RECT>> Targets(HWND hwnd, const Dialog& d) {
    const auto p = Panel(hwnd, d);
    std::vector<std::pair<int, RECT>> targets;
    switch (d.stage) {
    case Stage::Select:
        targets.push_back({ kClose, Box(p, d, -52, 18, -20, 50) });
        targets.push_back({ kPick, PickButton(p, d) });
        targets.push_back({ kDrop, Box(p, d, 24, 90, -24, 270) });
        targets.push_back({ kExamples, Box(p, d, -144, 496, -24, 528) });
        if (!d.examples) {
            targets.push_back({ kAutoPinyin, Box(p, d, 16, 380, -16, 428) });
            targets.push_back({ kSkipExisting, Box(p, d, 16, 432, -16, 480) });
        }
        break;
    case Stage::Preview:
        targets.push_back({ kChange, Box(p, d, -100, 77, -40, 107) });
        targets.push_back({ kTabAll, Box(p, d, -312, 208, -220, 236) });
        targets.push_back({ kTabCheck, Box(p, d, -220, 208, -122, 236) });
        targets.push_back({ kTabSkip, Box(p, d, -122, 208, -24, 236) });
        targets.push_back({ kCancel, Box(p, d, 24, -50, 112, -16) });
        if (d.ready > 0) targets.push_back({ kImport, Box(p, d, -244, -50, -24, -16) });
        break;
    case Stage::Done: {
        const int cx = (p.left + p.right) / 2;
        const auto row = Box(p, d, 24, 236, -24, 272);
        if (d.report.failed > 0) {
            targets.push_back({ kFailures, { row.left, row.top, cx - d.px(6), row.bottom } });
            targets.push_back({ kFinish, { cx + d.px(6), row.top, row.right, row.bottom } });
        } else {
            targets.push_back({ kFinish, row });
        }
        if (d.report.imported > 0 && !d.reverted)
            targets.push_back({ kUndo, { cx - d.px(70), p.top + d.px(288), cx + d.px(70), p.top + d.px(312) } });
        break;
    }
    case Stage::Failures:
        targets.push_back({ kBack, Box(p, d, 24, -50, 112, -16) });
        targets.push_back({ kFinish, Box(p, d, -112, -50, -24, -16) });
        break;
    }
    return targets;
}
int Hit(HWND hwnd, const Dialog& d, POINT point) {
    if (d.stage == Stage::Select && d.examples) {
        const auto card = ExamplesCard(Panel(hwnd, d), d);
        if (PtInRect(&card, point)) return kNoHit;
    }
    for (const auto& [id, rect] : Targets(hwnd, d))
        if (PtInRect(&rect, point)) return id;
    if (d.stage != Stage::Preview && d.stage != Stage::Failures) return kNoHit;
    const auto v = ListView(hwnd, d);
    if (!PtInRect(&v, point)) return kNoHit;
    if (point.x >= v.right - d.px(kGutter)) return MaxScroll(hwnd, d) > 0 ? kScroll : kNoHit;
    const int index = (point.y - v.top + d.scroll) / d.px(kRowHeight);
    return index >= 0 && index < static_cast<int>(d.rows.size()) ? kRow + index : kNoHit;
}

// ---- 数据 ----
void Recount(Dialog& d) {
    d.ready = d.exists = d.repeated = d.invalid = d.polyphonic = 0;
    for (const auto& item : d.file.items) {
        switch (item.status) {
        case UserLexiconImportStatus::Ready: ++d.ready; if (item.polyphonic) ++d.polyphonic; break;
        case UserLexiconImportStatus::Exists: ++d.exists; break;
        case UserLexiconImportStatus::Repeated: ++d.repeated; break;
        case UserLexiconImportStatus::Invalid: ++d.invalid; break;
        }
    }
}
void Refilter(HWND hwnd, Dialog& d, bool keepScroll = false) {
    d.rows.clear();
    for (int i = 0; i < static_cast<int>(d.file.items.size()); ++i) {
        const auto& item = d.file.items[i];
        const bool visible = d.stage == Stage::Failures ? item.status == UserLexiconImportStatus::Invalid
            : d.filter == Filter::Check ? NeedsCheck(item)
            : d.filter == Filter::Skip ? Skipped(item) : true;
        if (visible) d.rows.push_back(i);
    }
    d.scroll = keepScroll ? (std::clamp)(d.scroll, 0, MaxScroll(hwnd, d)) : 0;
}
void ScrollTo(HWND hwnd, Dialog& d, int value) {
    d.scroll = (std::clamp)(value, 0, MaxScroll(hwnd, d));
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---- 绘制 ----
struct TextOp { std::wstring text; RECT rect; FontId font; std::uint32_t color; UINT format; RECT clip; };
struct Scene {
    Canvas& canvas;
    Surface surface;
    const Dialog& d;
    const SettingsSkin& skin;
    std::vector<TextOp> texts;
    void text(std::wstring value, const RECT& rect, FontId font, std::uint32_t color, UINT format = kText, const RECT* clip = nullptr) {
        texts.push_back({ std::move(value), rect, font, color, format, clip ? *clip : RECT{} });
    }
    void icon(const char* const* paths, int count, int left, int top, int size, std::uint32_t color, float stroke = 1.6f) {
        StrokeSvgIcon(surface, paths, count, static_cast<float>(left), static_cast<float>(top),
            static_cast<float>(size), 24.0f, stroke * d.scale, color);
    }
    void line(int left, int top, int right) {
        FillRoundRect(surface, { static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(top + 1) }, 0, skin.line);
    }
    void flush() {
        std::array<std::unique_ptr<ScopedFont>, FCount> fonts;
        for (int i = 0; i < FCount; ++i)
            fonts[i] = std::make_unique<ScopedFont>(skin.fontName, d.px(kFontSizes[i]), kFontWeights[i]);
        canvas.beginText();
        const HDC dc = canvas.dc();
        const auto oldFont = SelectObject(dc, fonts[FBody]->get());
        for (auto& op : texts) {
            if (op.clip.right > op.clip.left) IntersectClipRect(dc, op.clip.left, op.clip.top, op.clip.right, op.clip.bottom);
            SelectObject(dc, fonts[op.font]->get());
            SetTextColor(dc, ToColorRef(op.color));
            DrawTextW(dc, op.text.c_str(), -1, &op.rect, op.format);
            if (op.clip.right > op.clip.left) SelectClipRgn(dc, nullptr);
        }
        SelectObject(dc, oldFont);
        canvas.endText();
        texts.clear();
    }
};

void Button(Scene& s, const RECT& rect, int id, const std::wstring& label, bool primary, bool enabled = true) {
    const auto& skin = s.skin;
    const bool hot = s.d.hover == id && enabled;
    const bool down = hot && s.d.pressed == id;
    const float radius = static_cast<float>(s.d.px(6));
    if (primary) {
        const std::uint32_t fill = !enabled ? Tint(skin.text, 0x1A) : down ? skin.accentPressed : hot ? skin.accentHover : skin.accent;
        FillRoundRect(s.surface, ToRectF(rect), radius, fill);
        s.text(label, rect, FBody, enabled ? skin.onAccent : skin.muted, kCenter);
    } else {
        FillRoundRect(s.surface, ToRectF(rect), radius, down ? skin.pressed : hot ? skin.hover : skin.surface);
        StrokeRoundRect(s.surface, ToRectF(rect), radius, 1, skin.lineStrong);
        s.text(label, rect, FBody, skin.text, kCenter);
    }
}
void Pill(Scene& s, const RECT& rect, const std::wstring& label, std::uint32_t color) {
    FillRoundRect(s.surface, ToRectF(rect), (rect.bottom - rect.top) * 0.5f, Tint(color));
    s.text(label, rect, FSmall, color, kCenter);
}
void Toggle(Scene& s, const RECT& track, bool on) {
    const float height = static_cast<float>(track.bottom - track.top);
    FillRoundRect(s.surface, ToRectF(track), height * 0.5f, on ? s.skin.accent : (s.skin.darkCaption ? 0xFF62656A : 0xFFB0B0B0));
    const float inset = static_cast<float>(s.d.px(2)), knob = height - 2 * inset;
    const float left = on ? track.right - inset - knob : track.left + inset;
    const RectF knobRect{ left, track.top + inset, left + knob, track.top + inset + knob };
    DrawShadow(s.surface, knobRect, knob * 0.5f, { 0.5f, 2, 0, 0x4D000000 }, s.d.scale);
    FillRoundRect(s.surface, knobRect, knob * 0.5f, 0xFFFFFFFF);
}
// 数字 + 标签的统计列
void Stats(Scene& s, const RECT& area, const std::vector<std::tuple<int, const wchar_t*, std::uint32_t>>& columns) {
    const int width = (area.right - area.left) / static_cast<int>(columns.size());
    for (size_t i = 0; i < columns.size(); ++i) {
        const int left = area.left + width * static_cast<int>(i);
        if (i > 0) FillRoundRect(s.surface, { static_cast<float>(left), static_cast<float>(area.top + s.d.px(8)),
            static_cast<float>(left + 1), static_cast<float>(area.bottom - s.d.px(8)) }, 0, s.skin.line);
        const auto& [value, label, color] = columns[i];
        s.text(Number(value), { left, area.top, left + width, area.top + s.d.px(32) }, FNumber, color, kCenter);
        s.text(label, { left, area.top + s.d.px(32), left + width, area.bottom }, FSmall, s.skin.muted, kCenter);
    }
}

void PaintSelect(Scene& s, const RECT& p) {
    const auto& d = s.d;
    const auto& skin = s.skin;
    s.icon(kIconBook, 1, p.left + d.px(24), p.top + d.px(25), d.px(20), skin.text);
    s.text(L"导入用户词库", Box(p, d, 52, 22, -60, 50), FTitle, skin.text);
    const auto close = Box(p, d, -52, 18, -20, 50);
    if (d.hover == kClose) FillRoundRect(s.surface, ToRectF(close), static_cast<float>(d.px(6)), skin.hover);
    s.icon(kIconClose, 1, close.left + d.px(8), close.top + d.px(8), d.px(16), skin.sub);
    s.text(L"将其他输入法中的常用词语迁移到当前输入法", Box(p, d, 24, 54, -24, 76), FSmall, skin.sub);

    const auto drop = Box(p, d, 24, 90, -24, 270);
    const bool hot = d.hover == kDrop || d.hover == kPick;
    FillRoundRect(s.surface, ToRectF(drop), static_cast<float>(d.px(10)), hot ? Tint(skin.accent, 0x10) : skin.window);
    StrokeRoundRect(s.surface, ToRectF(drop), static_cast<float>(d.px(10)), 1, hot ? skin.accent : skin.lineStrong);
    const int cx = (drop.left + drop.right) / 2;
    s.icon(kIconUpload, 3, cx - d.px(15), drop.top + d.px(22), d.px(30), hot ? skin.accent : skin.sub);
    s.text(L"拖入词库文件，或点击选择文件", { drop.left, drop.top + d.px(62), drop.right, drop.top + d.px(88) }, FBodyBold, skin.text, kCenter);
    s.text(std::wstring(L"支持 TXT、CSV、TSV 文本词库，将写入") + CharsetName(d.charset) + L"词库",
        { drop.left, drop.top + d.px(90), drop.right, drop.top + d.px(112) }, FSmall, skin.sub, kCenter);
    Button(s, PickButton(p, d), kPick, L"选择文件", true);
    if (!d.error.empty())
        s.text(L"⚠ " + d.error, Box(p, d, 24, 274, -24, 296), FSmall, skin.danger);

    s.icon(kIconCheckCircle, 2, p.left + d.px(24), p.top + d.px(304), d.px(18), Green(skin));
    s.text(L"自动识别格式和编码", Box(p, d, 50, 300, 320, 326), FBody, skin.text);
    s.text(L"UTF-8 / GBK / UTF-16", Box(p, d, 320, 300, -24, 326), FSmall, skin.muted, kRight);
    s.line(p.left + d.px(24), p.top + d.px(340), p.right - d.px(24));
    s.text(L"导入选项", Box(p, d, 24, 350, 240, 376), FBodyBold, skin.text);

    struct Option { int id; const wchar_t* title; const wchar_t* desc; bool on; };
    const Option options[] = {
        { kAutoPinyin, L"自动补全拼音", L"词库中没有拼音时，根据系统词典自动生成", d.options.autoPinyin },
        { kSkipExisting, L"跳过已存在的词条", L"保护已有的输入习惯和词频", d.options.skipExisting },
    };
    for (const auto& option : options) {
        const RECT row = Box(p, d, 16, option.id == kAutoPinyin ? 380 : 432, -16, option.id == kAutoPinyin ? 428 : 480);
        if (d.hover == option.id) FillRoundRect(s.surface, ToRectF(row), static_cast<float>(d.px(6)), skin.hover);
        const RECT track{ row.right - d.px(48), (row.top + row.bottom) / 2 - d.px(11), row.right - d.px(8), (row.top + row.bottom) / 2 + d.px(11) };
        Toggle(s, track, option.on);
        if (option.id == kAutoPinyin)
            Pill(s, { track.left - d.px(78), track.top, track.left - d.px(12), track.bottom }, L"推荐开启", Green(skin));
        s.text(option.title, { row.left + d.px(8), row.top + d.px(3), track.left - d.px(90), row.top + d.px(25) }, FBody, skin.text);
        s.text(option.desc, { row.left + d.px(8), row.top + d.px(25), track.left - d.px(90), row.bottom - d.px(3) }, FSmall, skin.sub);
    }

    s.text(L"支持格式示例", Box(p, d, 24, 496, 240, 528), FBody, skin.sub);
    const auto examples = Box(p, d, -144, 496, -24, 528);
    Button(s, examples, kExamples, L"", false);
    s.text(L"查看示例", { examples.left + d.px(14), examples.top, examples.right - d.px(30), examples.bottom }, FBody, skin.text);
    DrawChevron(s.surface, static_cast<float>(examples.right - d.px(20)), (examples.top + examples.bottom) * 0.5f,
        static_cast<float>(d.px(16)), 1.5f * d.scale, d.examples, skin.sub);

    if (!d.examples) return;
    s.flush(); // 弹层盖住下方文字
    const auto card = ExamplesCard(p, d);
    const float radius = static_cast<float>(d.px(8));
    DrawShadow(s.surface, ToRectF(card), radius, { 2, 12, 0, 0x26000000 }, d.scale);
    FillRoundRect(s.surface, ToRectF(card), radius, skin.surface);
    StrokeRoundRect(s.surface, ToRectF(card), radius, 1, skin.lineStrong);
    const int c1 = card.left + d.px(16), c2 = card.left + d.px(148), c3 = card.left + d.px(388), right = card.right - d.px(16);
    const wchar_t* table[4][3] = {
        { L"格式", L"示例", L"处理方式" },
        { L"纯词语 TXT", L"重庆", L"自动生成拼音" },
        { L"词语 + 拼音", L"重庆,chong'qing", L"使用原有拼音" },
        { L"自有 CSV", L"Phrase,Pinyin,FullPinyin,Charset", L"保留原字段" },
    };
    for (int r = 0; r < 4; ++r) {
        const int top = card.top + d.px(8) + r * d.px(36);
        if (r > 0) s.line(c1, top, right);
        const FontId font = r == 0 ? FSmall : FBody;
        const std::uint32_t color = r == 0 ? skin.muted : skin.text;
        s.text(table[r][0], { c1, top, c2 - d.px(8), top + d.px(36) }, font, color);
        s.text(table[r][1], { c2, top, c3 - d.px(8), top + d.px(36) }, r == 0 ? FSmall : FSmall, r == 0 ? skin.muted : skin.sub);
        s.text(table[r][2], { c3, top, right, top + d.px(36) }, font, color);
    }
}

void PaintList(Scene& s, HWND hwnd) {
    const auto& d = s.d;
    const auto& skin = s.skin;
    const auto p = Panel(hwnd, d);
    const auto header = ListHeader(p, d);
    const auto view = ListView(hwnd, d);
    const RECT headerRow{ header.left, header.top, header.right - d.px(kGutter), header.bottom };
    const auto headerCells = CellsOf(headerRow, d);
    const bool failures = d.stage == Stage::Failures;
    s.text(L"行", headerCells.line, FSmall, skin.muted);
    s.text(L"词语", headerCells.phrase, FSmall, skin.muted);
    s.text(failures ? L"原因" : L"拼音", headerCells.pinyin, FSmall, skin.muted);
    if (!failures) s.text(L"状态", headerCells.status, FSmall, skin.muted);
    s.line(header.left, header.bottom - 1, header.right);

    if (d.rows.empty()) {
        const wchar_t* empty = d.filter == Filter::Check ? L"没有需要检查的词条" : d.filter == Filter::Skip ? L"没有将被跳过的词条" : L"没有词条";
        s.text(empty, view, FBody, skin.muted, kCenter);
        return;
    }
    const int first = d.scroll / d.px(kRowHeight);
    const int last = (std::min)(static_cast<int>(d.rows.size()), (d.scroll + static_cast<int>(view.bottom - view.top)) / d.px(kRowHeight) + 1);
    s.surface.setClip(view);
    for (int i = first; i < last; ++i) {
        const auto row = RowRect(hwnd, d, i);
        const auto& item = d.file.items[d.rows[i]];
        if (d.hover == kRow + i || d.editing == d.rows[i])
            FillRoundRect(s.surface, ToRectF(row), static_cast<float>(d.px(6)), skin.hover);
        else if (i + 1 < static_cast<int>(d.rows.size()))
            FillRoundRect(s.surface, { static_cast<float>(row.left + d.px(8)), static_cast<float>(row.bottom - 1),
                static_cast<float>(row.right - d.px(8)), static_cast<float>(row.bottom) }, 0, skin.line);
        const auto cells = CellsOf(row, d);
        s.text(std::to_wstring(item.line), cells.line, FSmall, skin.muted, kText, &view);
        if (item.phrase.empty())
            s.text(d.file.lines[d.rows[i]], cells.phrase, FBody, skin.muted, kText, &view);
        else
            s.text(Wide(item.phrase), cells.phrase, FBody, skin.text, kText, &view);
        if (item.status == UserLexiconImportStatus::Invalid)
            s.text(Wide(item.message), cells.pinyin, FSmall, skin.danger, kText, &view);
        else
            s.text(Wide(item.pinyin), cells.pinyin, FBody, Skipped(item) ? skin.muted : skin.text, kText, &view);
        if (failures) continue;
        std::wstring label;
        std::uint32_t color = skin.muted;
        switch (item.status) {
        case UserLexiconImportStatus::Invalid: label = L"异常"; color = skin.danger; break;
        case UserLexiconImportStatus::Exists: label = L"已存在"; break;
        case UserLexiconImportStatus::Repeated: label = L"重复"; break;
        default:
            if (item.polyphonic) { label = L"多音字"; color = Orange(skin); }
            else if (item.source == UserLexiconPinyinSource::Generated) { label = L"自动生成"; color = Green(skin); }
            else { label = L"原有拼音"; color = skin.accent; }
        }
        const int mid = (row.top + row.bottom) / 2;
        const RECT pill{ cells.status.left, mid - d.px(11), cells.status.left + d.px(64), mid + d.px(11) };
        FillRoundRect(s.surface, ToRectF(pill), pill.bottom - pill.top > 0 ? (pill.bottom - pill.top) * 0.5f : 0, Tint(color));
        s.text(label, pill, FSmall, color, kCenter, &view);
    }
    s.surface.resetClip();
    if (MaxScroll(hwnd, d) > 0) {
        const auto thumb = Thumb(hwnd, d);
        FillRoundRect(s.surface, ToRectF(thumb), static_cast<float>(d.px(3)),
            d.hover == kScroll || d.dragging ? skin.scrollThumbHover : skin.scrollThumb);
    }
}

void PaintPreview(Scene& s, HWND hwnd, const RECT& p) {
    const auto& d = s.d;
    const auto& skin = s.skin;
    s.text(L"预览导入内容", Box(p, d, 24, 22, -160, 50), FTitle, skin.text);
    const bool issues = d.invalid > 0 || d.polyphonic > 0;
    const std::uint32_t badgeColor = issues ? Orange(skin) : Green(skin);
    const auto badge = Box(p, d, -124, 24, -24, 48);
    FillRoundRect(s.surface, ToRectF(badge), (badge.bottom - badge.top) * 0.5f, Tint(badgeColor));
    s.icon(issues ? kIconInfo : kIconCheckCircle, 2, badge.left + d.px(10), badge.top + d.px(5), d.px(14), badgeColor);
    s.text(issues ? L"需要检查" : L"解析成功", { badge.left + d.px(28), badge.top, badge.right - d.px(8), badge.bottom }, FSmall, badgeColor, kCenter);

    const auto card = Box(p, d, 24, 62, -24, 122);
    FillRoundRect(s.surface, ToRectF(card), static_cast<float>(d.px(8)), skin.window);
    StrokeRoundRect(s.surface, ToRectF(card), static_cast<float>(d.px(8)), 1, skin.line);
    s.icon(kIconFile, 3, card.left + d.px(14), card.top + d.px(19), d.px(22), skin.sub);
    const auto change = Box(p, d, -100, 77, -40, 107);
    s.text(d.file.file_name, { card.left + d.px(48), card.top + d.px(8), change.left - d.px(12), card.top + d.px(32) }, FBodyBold, skin.text);
    std::wstring meta = coral::lexicon_io::encoding_label(d.file.encoding) + L" · " + coral::lexicon_io::layout_label(d.file.format);
    if (d.file.format != coral::lexicon_io::ImportLayout::kCoralCsv)
        meta += L" · " + coral::lexicon_io::delimiter_label(d.file.delimiter) + L" · 写入" + CharsetName(d.charset) + L"词库";
    s.text(meta, { card.left + d.px(48), card.top + d.px(32), change.left - d.px(12), card.top + d.px(52) }, FSmall, skin.sub);
    Button(s, change, kChange, L"更换", false);

    Stats(s, Box(p, d, 24, 134, -24, 192), {
        { static_cast<int>(d.file.items.size()), L"识别词条", skin.text },
        { d.ready, L"可导入", d.ready > 0 ? Green(skin) : skin.text },
        { d.exists + d.repeated, L"已存在 / 重复", skin.text },
        { d.invalid, L"异常", d.invalid > 0 ? skin.danger : skin.text },
    });

    s.text(L"词条预览", Box(p, d, 24, 208, 200, 236), FBodyBold, skin.text);
    int checks = 0;
    for (const auto& item : d.file.items) if (NeedsCheck(item)) ++checks;
    const auto tabs = Box(p, d, -312, 208, -24, 236);
    FillRoundRect(s.surface, ToRectF(tabs), static_cast<float>(d.px(6)), skin.window);
    const std::tuple<int, Filter, std::wstring> tabItems[] = {
        { kTabAll, Filter::All, L"全部 " + Number(static_cast<int>(d.file.items.size())) },
        { kTabCheck, Filter::Check, L"需检查 " + Number(checks) },
        { kTabSkip, Filter::Skip, L"将跳过 " + Number(d.exists + d.repeated) },
    };
    for (const auto& [id, filter, label] : tabItems) {
        RECT tab{};
        for (const auto& [targetId, rect] : Targets(hwnd, d)) if (targetId == id) tab = rect;
        InflateRect(&tab, -d.px(2), -d.px(2));
        if (d.filter == filter) {
            FillRoundRect(s.surface, ToRectF(tab), static_cast<float>(d.px(5)), skin.surface);
            StrokeRoundRect(s.surface, ToRectF(tab), static_cast<float>(d.px(5)), 1, skin.line);
        } else if (d.hover == id) {
            FillRoundRect(s.surface, ToRectF(tab), static_cast<float>(d.px(5)), skin.hover);
        }
        s.text(label, tab, FSmall, d.filter == filter ? skin.text : skin.sub, kCenter);
    }

    PaintList(s, hwnd);

    std::wstring hint;
    if (d.polyphonic > 0) hint += Number(d.polyphonic) + L" 个词条含多音字，请确认拼音；";
    if (d.invalid > 0) hint += Number(d.invalid) + L" 个异常词条不会导入；";
    hint += L"双击拼音可直接修改";
    const auto hintRow = Box(p, d, 24, -100, -24, -74);
    s.icon(kIconInfo, 2, hintRow.left, (hintRow.top + hintRow.bottom) / 2 - d.px(8), d.px(16), skin.muted);
    s.text(hint, { hintRow.left + d.px(24), hintRow.top, hintRow.right, hintRow.bottom }, FSmall, skin.muted);
    s.line(p.left + 1, p.bottom - d.px(66), p.right - 1);
    Button(s, Box(p, d, 24, -50, 112, -16), kCancel, L"取消", false);
    const auto import = Box(p, d, -244, -50, -24, -16);
    Button(s, import, kImport, L"", true, d.ready > 0);
    const std::wstring label = d.ready > 0 ? L"导入 " + Number(d.ready) + L" 个词条" : L"没有可导入的词条";
    s.text(label, { import.left, import.top, import.right - d.px(d.ready > 0 ? 22 : 0), import.bottom }, FBody,
        d.ready > 0 ? skin.onAccent : skin.muted, kCenter);
    if (d.ready > 0)
        s.icon(kIconArrow, 1, import.right - d.px(34), (import.top + import.bottom) / 2 - d.px(8), d.px(16), skin.onAccent, 1.8f);
}

void PaintDone(Scene& s, const RECT& p) {
    const auto& d = s.d;
    const auto& skin = s.skin;
    const int cx = (p.left + p.right) / 2, cy = p.top + d.px(54);
    const bool success = d.report.imported > 0 && !d.reverted;
    const std::uint32_t color = success ? Green(skin) : skin.muted;
    FillCircle(s.surface, static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(d.px(28)), Tint(color, 0x24));
    if (success) s.icon(kIconCheck, 1, cx - d.px(15), cy - d.px(15), d.px(30), color, 2.2f);
    else s.icon(kIconInfo, 2, cx - d.px(15), cy - d.px(15), d.px(30), color, 2.0f);
    const wchar_t* title = d.reverted ? L"已撤销本次导入" : d.report.imported > 0 ? L"词库导入完成" : L"没有导入新词条";
    const wchar_t* subtitle = d.reverted ? L"本次导入的词条已从用户词库中移除"
        : d.report.imported > 0 ? L"你的常用词语已经添加到输入法，输入拼音即可使用" : L"词条都已存在或无法导入";
    s.text(title, Box(p, d, 24, 94, -24, 122), FTitle, skin.text, kCenter);
    s.text(subtitle, Box(p, d, 24, 124, -24, 146), FSmall, skin.sub, kCenter);
    Stats(s, Box(p, d, 24, 164, -24, 220), {
        { d.report.imported, L"成功导入", skin.text },
        { d.report.skipped, L"重复跳过", skin.text },
        { d.report.failed, L"导入失败", d.report.failed > 0 ? skin.danger : skin.text },
    });
}

void Paint(HWND hwnd, const Dialog& d, HDC target) {
    RECT client{}; GetClientRect(hwnd, &client);
    Canvas canvas(client.right, client.bottom);
    if (!canvas.valid()) return;
    const auto& skin = SettingsWindow::skin();
    sheet::PaintBackdrop(canvas, d.backdrop.get());
    Scene s{ canvas, canvas.surface(), d, skin, {} };
    const auto panel = Panel(hwnd, d);
    const float radius = static_cast<float>(d.px(12));
    DrawShadow(s.surface, ToRectF(panel), radius, { 4, 16, 0, 0x30000000 }, d.scale);
    FillRoundRect(s.surface, ToRectF(panel), radius, skin.surface);
    StrokeRoundRect(s.surface, ToRectF(panel), radius, 1, skin.lineStrong);
    switch (d.stage) {
    case Stage::Select: PaintSelect(s, panel); break;
    case Stage::Preview: PaintPreview(s, hwnd, panel); break;
    case Stage::Done: {
        PaintDone(s, panel);
        for (const auto& [id, rect] : Targets(hwnd, d)) {
            if (id == kFailures) Button(s, rect, id, L"查看失败详情", false);
            else if (id == kFinish) Button(s, rect, id, L"完成", true);
            else if (id == kUndo) s.text(L"撤销本次导入", rect, FSmall, d.hover == kUndo ? skin.accentHover : skin.accent, kCenter);
        }
        if (d.undoFailed)
            s.text(L"撤销失败，请稍后重试", Box(panel, d, 24, 312, -24, 332), FSmall, skin.danger, kCenter);
        break;
    }
    case Stage::Failures:
        s.text(L"导入失败详情", Box(panel, d, 24, 22, -24, 50), FTitle, skin.text);
        s.text(L"以下 " + Number(static_cast<int>(d.rows.size())) + L" 个词条没有导入，修改源文件后可重新导入",
            Box(panel, d, 24, 54, -24, 76), FSmall, skin.sub);
        PaintList(s, hwnd);
        s.line(panel.left + 1, panel.bottom - d.px(66), panel.right - 1);
        Button(s, Box(panel, d, 24, -50, 112, -16), kBack, L"返回", false);
        Button(s, Box(panel, d, -112, -50, -24, -16), kFinish, L"完成", true);
        break;
    }
    s.flush();
    canvas.blit(target, 0, 0);
}

// ---- 行为 ----
HWND RootOf(HWND hwnd) {
    const HWND root = GetAncestor(hwnd, GA_ROOT);
    return root ? root : hwnd;
}
void ShowStage(HWND hwnd, Dialog& d, Stage stage) {
    d.stage = stage;
    d.hover = d.pressed = kNoHit;
    d.examples = false;
    if (stage == Stage::Preview || stage == Stage::Failures) Refilter(hwnd, d);
    InvalidateRect(hwnd, nullptr, FALSE);
}
void LoadFile(HWND hwnd, Dialog& d, const std::wstring& path) {
    const HCURSOR cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    coral::lexicon_io::ImportedText parsed;
    std::wstring error;
    if (!coral::lexicon_io::read_import_file(path, d.charset, parsed, error)) {
        d.error = error;
    } else if (!coral::runtime::prepare_user_import(parsed.items, d.options)) {
        d.error = L"输入法词库尚未加载完成，请稍后再试";
    } else {
        d.error.clear();
        d.file = std::move(parsed);
        d.filter = Filter::All;
        Recount(d);
    }
    SetCursor(cursor);
    ShowStage(hwnd, d, d.error.empty() ? Stage::Preview : Stage::Select);
}
void PickFile(HWND hwnd, Dialog& d) {
    wchar_t fileName[MAX_PATH * 2] = L"";
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = RootOf(hwnd);
    ofn.lpstrFilter = L"文本词库 (*.txt;*.csv;*.tsv;*.dic;*.yaml)\0*.txt;*.csv;*.tsv;*.dic;*.yaml\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = ARRAYSIZE(fileName);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) LoadFile(hwnd, d, fileName);
}
void Commit(HWND hwnd, Dialog& d) {
    if (d.ready == 0) return;
    const HCURSOR cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    d.report = coral::runtime::commit_user_import(d.file.items);
    SetCursor(cursor);
    // 写入阶段的失败并入异常列表，供“查看失败详情”展示
    std::unordered_map<int, size_t> byLine;
    for (size_t i = 0; i < d.file.items.size(); ++i) byLine[d.file.items[i].line] = i;
    for (const auto& [line, reason] : d.report.failures) {
        const auto it = byLine.find(line);
        if (it == byLine.end()) continue;
        d.file.items[it->second].status = UserLexiconImportStatus::Invalid;
        d.file.items[it->second].message = reason;
    }
    d.reverted = d.undoFailed = false;
    ShowStage(hwnd, d, Stage::Done);
}
void Revert(HWND hwnd, Dialog& d) {
    const HCURSOR cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    d.reverted = coral::runtime::revert_user_import();
    d.undoFailed = !d.reverted;
    SetCursor(cursor);
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK EditProc(HWND edit, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR owner) {
    const HWND dialog = reinterpret_cast<HWND>(owner);
    switch (msg) {
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) {
            PostMessageW(dialog, kEditDone, wParam == VK_RETURN, 0);
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == L'\r' || wParam == 27) return 0;
        break;
    case WM_KILLFOCUS:
        PostMessageW(dialog, kEditDone, 1, 0);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(edit, EditProc, 1);
        break;
    }
    return DefSubclassProc(edit, msg, wParam, lParam);
}
void BeginEdit(HWND hwnd, Dialog& d, int rowIndex) {
    if (d.edit || rowIndex < 0 || rowIndex >= static_cast<int>(d.rows.size())) return;
    const int index = d.rows[rowIndex];
    const auto& item = d.file.items[index];
    const auto view = ListView(hwnd, d);
    auto row = RowRect(hwnd, d, rowIndex);
    if (row.top < view.top) { ScrollTo(hwnd, d, d.scroll - (view.top - row.top)); row = RowRect(hwnd, d, rowIndex); }
    if (row.bottom > view.bottom) { ScrollTo(hwnd, d, d.scroll + (row.bottom - view.bottom)); row = RowRect(hwnd, d, rowIndex); }
    const auto cell = CellsOf(row, d).pinyin;
    d.editing = index;
    if (!d.editFont) d.editFont = std::make_unique<ScopedFont>(SettingsWindow::skin().fontName, d.px(14), FW_NORMAL);
    if (!d.editBrush) d.editBrush = CreateSolidBrush(ToColorRef(SettingsWindow::skin().surface));
    const std::wstring text = Wide(item.pinyin.empty() ? item.rawPinyin : item.pinyin);
    d.edit = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        cell.left - d.px(4), row.top + d.px(5), cell.right - cell.left + d.px(4), row.bottom - row.top - d.px(10),
        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!d.edit) { d.editing = -1; return; }
    SendMessageW(d.edit, WM_SETFONT, reinterpret_cast<WPARAM>(d.editFont->get()), TRUE);
    SetWindowSubclass(d.edit, EditProc, 1, reinterpret_cast<DWORD_PTR>(hwnd));
    SendMessageW(d.edit, EM_SETSEL, 0, -1);
    SetFocus(d.edit);
    InvalidateRect(hwnd, nullptr, FALSE);
}
void EndEdit(HWND hwnd, Dialog& d, bool commit) {
    if (!d.edit) return;
    const HWND edit = d.edit;
    d.edit = nullptr;
    const int index = d.editing;
    d.editing = -1;
    if (commit && index >= 0 && index < static_cast<int>(d.file.items.size())) {
        wchar_t buffer[256] = {};
        GetWindowTextW(edit, buffer, ARRAYSIZE(buffer));
        auto& item = d.file.items[index];
        const std::string pinyin = Narrow(buffer);
        if (pinyin != item.pinyin) {
            std::vector<UserLexiconImportItem> one{ item };
            one[0].rawPinyin = pinyin;
            one[0].rawLookupKey.clear();
            if (coral::runtime::prepare_user_import(one, d.options)) {
                item = std::move(one[0]);
                for (const auto& other : d.file.items) {
                    if (&other == &item || other.status != UserLexiconImportStatus::Ready) continue;
                    if (other.charset == item.charset && other.lookupKey == item.lookupKey && other.phrase == item.phrase &&
                        item.status == UserLexiconImportStatus::Ready) {
                        item.status = UserLexiconImportStatus::Repeated;
                        item.message = "与第 " + std::to_string(other.line) + " 行重复";
                    }
                }
                Recount(d);
                Refilter(hwnd, d, true);
            }
        }
    }
    DestroyWindow(edit);
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Activate(HWND hwnd, Dialog& d, int id) {
    switch (id) {
    case kClose: case kCancel: case kFinish: DestroyWindow(hwnd); return;
    case kPick: case kDrop: PickFile(hwnd, d); return;
    case kAutoPinyin: d.options.autoPinyin = !d.options.autoPinyin; break;
    case kSkipExisting: d.options.skipExisting = !d.options.skipExisting; break;
    case kExamples: d.examples = !d.examples; break;
    case kChange: d.error.clear(); ShowStage(hwnd, d, Stage::Select); return;
    case kTabAll: d.filter = Filter::All; Refilter(hwnd, d); break;
    case kTabCheck: d.filter = Filter::Check; Refilter(hwnd, d); break;
    case kTabSkip: d.filter = Filter::Skip; Refilter(hwnd, d); break;
    case kImport: Commit(hwnd, d); return;
    case kFailures: ShowStage(hwnd, d, Stage::Failures); return;
    case kBack: ShowStage(hwnd, d, Stage::Done); return;
    case kUndo: Revert(hwnd, d); return;
    default: return;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}
} // namespace

void LexiconImportDialog::showInstance(HWND parent) {
    if (!IsWindow(parent)) return;
    if (IsWindow(s_hwnd)) { SetFocus(s_hwnd); return; }
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"CoralPinyinLexiconImportDlg";
    static const ATOM registered = RegisterClassExW(&wc);
    if (!registered) return;
    auto d = std::make_unique<Dialog>();
    d->previousFocus = GetFocus();
    d->scale = static_cast<float>(GetDpiForWindow(parent)) / 96.0f;
    d->charset = sConfig->runtime_traditional() ? coral::LanguageVariant::Traditional : coral::LanguageVariant::Simplified;
    RECT client{}; GetClientRect(parent, &client);
    d->backdrop = sheet::CaptureBackdrop(parent);
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES, wc.lpszClassName, L"导入用户词库",
        WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, client.right, client.bottom,
        parent, nullptr, wc.hInstance, d.get());
    if (!hwnd) return;
    d.release(); // 从这里开始由 WM_NCDESTROY 持有状态。
    s_hwnd = hwnd;
    // 以管理员身份运行时，资源管理器的拖放消息会被 UIPI 拦截
    ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hwnd, 0x0049 /*  WM_COPYGLOBALDATA（跨进程复制全局数据消息） */, MSGFLT_ALLOW, nullptr);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(hwnd);
}
void LexiconImportDialog::closeInstance() {
    if (IsWindow(s_hwnd)) DestroyWindow(s_hwnd);
}
void LexiconImportDialog::show_file(HWND parent, const std::wstring& path) {
    showInstance(parent);
    if (!IsWindow(s_hwnd)) return;
    auto* state = reinterpret_cast<Dialog*>(GetWindowLongPtrW(s_hwnd, GWLP_USERDATA));
    if (!state) return;
    EndEdit(s_hwnd, *state, false);
    LoadFile(s_hwnd, *state, path);
}
void LexiconImportDialog::resizeToParent() {
    if (!IsWindow(s_hwnd)) return;
    const HWND parent = GetParent(s_hwnd);
    RECT rc{}; GetClientRect(parent, &rc);
    auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(s_hwnd, GWLP_USERDATA));
    if (d) {
        EndEdit(s_hwnd, *d, true);
        d->backdrop = sheet::CaptureBackdrop(parent, s_hwnd);
    }
    SetWindowPos(s_hwnd, HWND_TOP, 0, 0, rc.right, rc.bottom, SWP_NOACTIVATE);
    if (d) ScrollTo(s_hwnd, *d, d->scroll);
    InvalidateRect(s_hwnd, nullptr, FALSE);
}
LRESULT CALLBACK LexiconImportDialog::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
    case WM_MOUSEWHEEL:
        if (d->stage != Stage::Preview && d->stage != Stage::Failures) return 0;
        EndEdit(hwnd, *d, true);
        d->wheel += GET_WHEEL_DELTA_WPARAM(wParam);
        ScrollTo(hwnd, *d, d->scroll - d->px(3 * kRowHeight) * (d->wheel / WHEEL_DELTA));
        d->wheel %= WHEEL_DELTA; return 0;
    case WM_MOUSEMOVE: {
        const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (d->dragging) {
            const auto v = ListView(hwnd, *d), thumb = Thumb(hwnd, *d);
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
        if (d->edit) { EndEdit(hwnd, *d, true); return 0; }
        SetFocus(hwnd);
        const POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        d->pressed = Hit(hwnd, *d, p);
        if (d->pressed != kNoHit) SetCapture(hwnd);
        if (d->pressed == kScroll) {
            const auto thumb = Thumb(hwnd, *d);
            const auto v = ListView(hwnd, *d);
            if (!PtInRect(&thumb, p)) ScrollTo(hwnd, *d, d->scroll + (p.y < thumb.top ? -1 : 1) * (v.bottom - v.top));
            d->dragging = true; d->grabY = p.y; d->grabScroll = d->scroll;
        }
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_LBUTTONUP: {
        const int pressed = d->pressed;
        const int hit = Hit(hwnd, *d, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        d->pressed = kNoHit; d->dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (d->examples && hit != kExamples) { d->examples = false; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (pressed == hit && hit != kNoHit && hit != kScroll && hit < kRow) { Activate(hwnd, *d, hit); return 0; }
        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_LBUTTONDBLCLK: {
        const int hit = Hit(hwnd, *d, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
        if (d->stage == Stage::Preview && hit >= kRow) BeginEdit(hwnd, *d, hit - kRow);
        else if (hit != kNoHit && hit != kScroll && hit < kRow) Activate(hwnd, *d, hit);
        return 0;
    }
    case WM_CAPTURECHANGED: d->dragging = false; d->pressed = kNoHit; InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH * 2] = {};
        const bool ok = DragQueryFileW(drop, 0, path, ARRAYSIZE(path)) > 0;
        DragFinish(drop);
        if (ok && (d->stage == Stage::Select || d->stage == Stage::Preview)) {
            EndEdit(hwnd, *d, false);
            LoadFile(hwnd, *d, path);
        }
        return 0;
    }
    case kEditDone: EndEdit(hwnd, *d, wParam != 0); return 0;
    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, ToColorRef(SettingsWindow::skin().text));
        SetBkColor(dc, ToColorRef(SettingsWindow::skin().surface));
        return reinterpret_cast<LRESULT>(d->editBrush);
    }
    case WM_KEYDOWN:
        switch (wParam) {
        case VK_ESCAPE:
            if (d->examples) { d->examples = false; InvalidateRect(hwnd, nullptr, FALSE); }
            else if (d->stage == Stage::Failures) ShowStage(hwnd, *d, Stage::Done);
            else DestroyWindow(hwnd);
            return 0;
        case VK_RETURN:
            if (d->stage == Stage::Select) PickFile(hwnd, *d);
            else if (d->stage == Stage::Preview) Commit(hwnd, *d);
            else DestroyWindow(hwnd);
            return 0;
        case VK_UP: ScrollTo(hwnd, *d, d->scroll - d->px(kRowHeight)); return 0;
        case VK_DOWN: ScrollTo(hwnd, *d, d->scroll + d->px(kRowHeight)); return 0;
        case VK_PRIOR: ScrollTo(hwnd, *d, d->scroll - d->px(8 * kRowHeight)); return 0;
        case VK_NEXT: ScrollTo(hwnd, *d, d->scroll + d->px(8 * kRowHeight)); return 0;
        case VK_HOME: ScrollTo(hwnd, *d, 0); return 0;
        case VK_END: ScrollTo(hwnd, *d, MaxScroll(hwnd, *d)); return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && reinterpret_cast<HWND>(wParam) == hwnd) {
            POINT p{}; GetCursorPos(&p); ScreenToClient(hwnd, &p);
            const int hit = Hit(hwnd, *d, p);
            const bool hand = hit != kNoHit && hit != kScroll && hit < kRow && (hit != kImport || d->ready > 0);
            SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW)); return TRUE;
        }
        break;
    case WM_GETDLGCODE: return DLGC_WANTALLKEYS;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY: {
        const HWND focus = d->previousFocus;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (d->editBrush) DeleteObject(d->editBrush);
        s_hwnd = nullptr; delete d;
        if (IsWindow(focus) && IsWindowVisible(focus) && IsWindowEnabled(focus)) SetFocus(focus);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 用内存引擎与配置替身编译生产导入面板。
// 真实文件用于演练编码/格式检测；消息驱动三个页面。
#include "pch.h"
#include "../../src/platform/windows/coralpinyin/src/ui/lexiconimportdialog.cpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
POINT Center(const RECT& r) { return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 }; }
RECT Target(HWND hwnd, const Dialog& d, int id) {
    for (const auto& [target, rect] : Targets(hwnd, d)) if (target == id) return rect;
    throw std::runtime_error("Missing hit target");
}
void Click(HWND hwnd, POINT point) {
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
}
void ClickTarget(HWND hwnd, const Dialog& d, int id) { Click(hwnd, Center(Target(hwnd, d, id))); }
void SavePanel(HWND hwnd, Dialog& d, const fs::path& path) {
    RECT client{}; GetClientRect(hwnd, &client);
    Canvas canvas(client.right, client.bottom);
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.dc()), PRF_CLIENT);
    GdiFlush();
    RECT panel = Panel(hwnd, d);
    InflateRect(&panel, 12, 12);
    IntersectRect(&panel, &panel, &client);
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
void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}
std::string Encode(const std::wstring& text, UINT codePage) {
    const int size = WideCharToMultiByte(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(codePage, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::string Utf16LE(const std::wstring& text) {
    std::string out("\xFF\xFE", 2);
    out.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    return out;
}
const UserLexiconImportItem& ItemByPhrase(const Dialog& d, const std::string& phrase, int nth = 0) {
    for (const auto& item : d.file.items) if (item.phrase == phrase && nth-- == 0) return item;
    std::string parsed;
    for (const auto& item : d.file.items) parsed += " [" + item.phrase + "|" + item.rawPinyin + "]";
    throw std::runtime_error("Missing parsed item " + phrase + ", parsed:" + parsed);
}
int RowOf(const Dialog& d, const std::string& phrase) {
    for (int i = 0; i < static_cast<int>(d.rows.size()); ++i) if (d.file.items[d.rows[i]].phrase == phrase) return i;
    throw std::runtime_error("Missing row");
}

LRESULT CALLBACK OwnerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PRINTCLIENT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        RECT client{}; GetClientRect(hwnd, &client);
        HBRUSH background = CreateSolidBrush(RGB(230, 235, 240));
        FillRect(dc, &client, background); DeleteObject(background);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int main(int argc, char** argv) {
    HWND parent = nullptr;
    try {
        Require(argc == 2, "Expected render output directory");
        const auto output = fs::u8path(argv[1]);
        fs::create_directories(output);
        const auto data = output / "data";
        fs::create_directories(data);
        // 迁移表使用命名列；表头、频次、来源位置不应成为词条。
        coral::lexicon_io::ImportedText named;
        coral::lexicon_io::parse_import_text(
            L"word\tpinyin\tsource_weight_raw_max\tsource_records\r\n重庆\tchong qing\t65535\t2\r\n",
            coral::LanguageVariant::Simplified, named);
        Require(named.items.size() == 1 && named.items[0].phrase == "重庆" &&
            named.items[0].rawPinyin == "chong qing" && named.items[0].rawLookupKey.empty() &&
            named.items[0].line == 2 && named.format == coral::lexicon_io::ImportLayout::kMigrationTsv,
            "Migration header/columns were not respected");
        coral::lexicon_io::parse_import_text(L"pinyin\tword\tsequence\nni hao\t你好\t999\n",
            coral::LanguageVariant::Simplified, named);
        Require(named.items.size() == 1 && named.items[0].phrase == "你好" && named.items[0].rawPinyin == "ni hao",
            "Reordered migration columns failed");
        coral::lexicon_io::parse_import_text(L"word\tpinyin\n你好\t\n",
            coral::LanguageVariant::Simplified, named);
        Require(named.items.size() == 1 && named.items[0].rawPinyin.empty(), "Empty reading column was lost");
        coral::lexicon_io::parse_import_text(L"word\tpinyin\n\"你\"\"好\"\tni hao\n",
            coral::LanguageVariant::Simplified, named);
        Require(named.items.size() == 1 && named.items[0].phrase == "你\"好", "TSV escaped quote was not decoded");
        coral::lexicon_io::parse_import_text(L"previous_word\tprevious_pinyin\tnext_word\n你好\tni hao\t世界\n",
            coral::LanguageVariant::Simplified, named);
        Require(named.items.empty() && !named.parse_error.empty(), "Context pairs were accepted as words");
        coral::lexicon_io::parse_import_text(L"word\tpinyin\n你好\tni hao\n错误\n",
            coral::LanguageVariant::Simplified, named);
        Require(named.items.empty() && !named.parse_error.empty(), "Malformed TSV left partially accepted items");
        const auto migration = data / L"coral_import.tsv";
        WriteBytes(migration, std::string("\xEF\xBB\xBF", 3) + Encode(
            L"word\tpinyin\n重庆\tchong qing\n你好\tni hao\n重庆\tchong qing\n", CP_UTF8));
        const auto plain = data / L"搜狗导出.txt";
        WriteBytes(plain, Encode(L"重庆\n北京\n你好\n银行\n\n# 注释行\n重庆\n龘龘\n上海\n长城\n", CP_UTF8));
        const auto gbk = data / L"词语拼音.txt";
        WriteBytes(gbk, Encode(L"重庆,chong'qing\r\n输入法,shu'ru'fa\r\n长城,chang\r\n上海，shang'hai\r\n", 936));
        const auto csv = data / L"coral_export.csv";
        WriteBytes(csv, Utf16LE(L"Phrase,Pinyin,FullPinyin,Charset\r\n重庆,cq,chong'qing,S\r\n你好,ni'hao,ni'hao,T\r\n"));
        const auto big = data / L"大词库.txt";
        {
            std::wstring text;
            const wchar_t* chars = L"重庆北京你好上海银行长城输入法";
            for (int i = 0; i < 240; ++i) {
                text += chars[i % 15]; text += chars[(i / 15 + 1) % 15];
                if (i % 7 == 0) text += chars[(i / 3) % 15];
                text += L"\n";
            }
            WriteBytes(big, Encode(text, CP_UTF8));
        }

        WNDCLASSW ownerClass{};
        ownerClass.lpfnWndProc = OwnerProc;
        ownerClass.hInstance = GetModuleHandleW(nullptr);
        ownerClass.lpszClassName = L"LexiconImportTestOwner";
        Require(RegisterClassW(&ownerClass) != 0, "Cannot register test owner");
        // 隐藏的无边框父窗不受远程桌面的最大窗口跟踪尺寸限制，保持缩放断言确定。
        parent = CreateWindowExW(0, ownerClass.lpszClassName, L"Import test owner",
            WS_POPUP, 0, 0, 1000, 760, nullptr, nullptr, ownerClass.hInstance, nullptr);
        Require(parent != nullptr, "Cannot create test parent");
        const auto open = [&]() {
            LexiconImportDialog::showInstance(parent);
            const HWND child = FindWindowExW(parent, nullptr, L"CoralPinyinLexiconImportDlg", nullptr);
            Require(child != nullptr, "Missing child dialog");
            auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(child, GWLP_USERDATA));
            d->scale = 1; // 确定性的 96-DPI 渲染
            return std::make_pair(child, d);
        };

        // ---- 选择页 ----
        auto [child, d] = open();
        Require(GetParent(child) == parent && (GetWindowLongPtrW(child, GWL_STYLE) & WS_CHILD), "Dialog is not embedded");
        Require(GetWindowLongPtrW(child, GWL_EXSTYLE) & WS_EX_ACCEPTFILES, "Dialog does not accept dropped files");
        Require(d->stage == Stage::Select && d->charset == coral::LanguageVariant::Simplified, "Unexpected initial state");
        const auto panel = Panel(child, *d);
        Require(panel.right - panel.left == 600 && panel.bottom - panel.top == 548, "Incorrect select panel size");
        SavePanel(child, *d, output / "1-select.bmp");
        ClickTarget(child, *d, kAutoPinyin);
        Require(!d->options.autoPinyin, "Auto pinyin toggle failed");
        ClickTarget(child, *d, kAutoPinyin);
        ClickTarget(child, *d, kSkipExisting);
        Require(!d->options.skipExisting, "Skip existing toggle failed");
        ClickTarget(child, *d, kSkipExisting);
        Require(d->options.autoPinyin && d->options.skipExisting, "Options not restored");
        ClickTarget(child, *d, kExamples);
        Require(d->examples, "Examples popover did not open");
        SavePanel(child, *d, output / "2-select-examples.bmp");
        Click(child, { panel.left + 5, panel.top + 5 });
        Require(!d->examples, "Clicking elsewhere did not dismiss examples");

        testEngine.ready = false;
        LoadFile(child, *d, plain.wstring());
        Require(d->stage == Stage::Select && !d->error.empty(), "Engine-not-ready error missing");
        testEngine.ready = true;
        LoadFile(child, *d, (data / L"missing.txt").wstring());
        Require(d->stage == Stage::Select && d->error == L"无法打开文件", "Missing file error wrong");
        SavePanel(child, *d, output / "3-select-error.bmp");

        // ---- 预览：纯词语 UTF-8 ----
        LoadFile(child, *d, plain.wstring());
        Require(d->stage == Stage::Preview && d->error.empty(), "Plain file did not reach preview");
        Require(d->file.encoding == coral::lexicon_io::ImportEncoding::kUtf8, "UTF-8 not detected");
        Require(d->file.format == coral::lexicon_io::ImportLayout::kWords && d->file.delimiter == 0, "Plain words not detected");
        Require(d->file.items.size() == 8, "Comment/blank lines not skipped");
        Require(d->file.items[0].line == 1 && d->file.items[5].line == 8, "Source line numbers wrong");
        Require(d->ready == 5 && d->exists == 1 && d->repeated == 1 && d->invalid == 1 && d->polyphonic == 3, "Preview counts wrong");
        Require(ItemByPhrase(*d, "重庆", 1).status == UserLexiconImportStatus::Repeated, "In-file repeat not flagged");
        Require(ItemByPhrase(*d, "北京").status == UserLexiconImportStatus::Exists, "Existing phrase not flagged");
        Require(d->rows.size() == 8, "All tab should list every item");
        SavePanel(child, *d, output / "4-preview.bmp");
        ClickTarget(child, *d, kTabCheck);
        Require(d->filter == Filter::Check && d->rows.size() == 4, "Check tab should list invalid + polyphonic");
        SavePanel(child, *d, output / "5-preview-check.bmp");
        ClickTarget(child, *d, kTabSkip);
        Require(d->rows.size() == 2, "Skip tab should list existing + repeated");
        ClickTarget(child, *d, kTabAll);

        // 双击修改拼音：异常词条补上拼音后变为可导入
        const auto editRow = RowRect(child, *d, RowOf(*d, "龘龘"));
        const auto editCell = CellsOf(editRow, *d).pinyin;
        SendMessageW(child, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(editCell.left + 10, Center(editCell).y));
        Require(d->edit != nullptr && IsWindow(d->edit), "Double click did not open editor");
        SetWindowTextW(d->edit, L"da'da");
        SavePanel(child, *d, output / "6-preview-edit.bmp");
        SendMessageW(child, kEditDone, 1, 0);
        Require(d->edit == nullptr, "Editor not closed");
        Require(ItemByPhrase(*d, "龘龘").status == UserLexiconImportStatus::Ready &&
            ItemByPhrase(*d, "龘龘").source == UserLexiconPinyinSource::File, "Edited pinyin not applied");
        Require(d->ready == 6 && d->invalid == 0, "Counts not refreshed after edit");
        // 改成与已有行一致的拼音 → 文件内重复
        SendMessageW(child, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(editCell.left + 10, Center(editCell).y));
        SendMessageW(child, kEditDone, 0, 0);
        Require(ItemByPhrase(*d, "龘龘").pinyin == "da'da", "Escape should discard edits");

        // ---- 滚动 ----
        LoadFile(child, *d, big.wstring());
        Require(d->file.items.size() == 240 && MaxScroll(child, *d) > 0, "Big file should scroll");
        SendMessageW(child, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
        Require(d->scroll > 0, "Wheel did not scroll");
        SendMessageW(child, WM_KEYDOWN, VK_END, 0);
        Require(d->scroll == MaxScroll(child, *d), "End did not reach bottom");
        SavePanel(child, *d, output / "7-preview-scrolled.bmp");
        SendMessageW(child, WM_KEYDOWN, VK_HOME, 0);
        Require(d->scroll == 0, "Home did not reach top");

        // ---- 导入 + 完成页 + 撤销 ----
        LoadFile(child, *d, plain.wstring());
        ClickTarget(child, *d, kImport);
        Require(d->stage == Stage::Done, "Import did not reach done page");
        Require(d->report.imported == 5 && d->report.skipped == 2 && d->report.failed == 1, "Report counts wrong");
        Require(testEngine.committed.size() == 5, "Engine did not receive ready items");
        const auto donePanel = Panel(child, *d);
        Require(donePanel.right - donePanel.left == 520, "Incorrect done panel size");
        SavePanel(child, *d, output / "8-done.bmp");
        ClickTarget(child, *d, kUndo);
        Require(d->reverted && testEngine.reverted, "Undo did not revert");
        SavePanel(child, *d, output / "9-done-reverted.bmp");
        ClickTarget(child, *d, kFailures);
        Require(d->stage == Stage::Failures && d->rows.size() == 1, "Failures page should list the invalid row");
        ClickTarget(child, *d, kBack);
        Require(d->stage == Stage::Done, "Back did not return to done page");
        ClickTarget(child, *d, kFinish);
        Require(!IsWindow(child), "Finish did not close dialog");

        // 工具交回路径后直接进入生产预览，再经同一提交与撤销流程。
        testEngine.committed.clear();
        testEngine.reverted = false;
        LexiconImportDialog::show_file(parent, migration.wstring());
        child = FindWindowExW(parent, nullptr, L"CoralPinyinLexiconImportDlg", nullptr);
        d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(child, GWLP_USERDATA));
        Require(d && d->stage == Stage::Preview && d->file.items.size() == 3 &&
            d->ready == 2 && d->repeated == 1, "Migration did not enter preview with duplicate checks");
        ClickTarget(child, *d, kImport);
        Require(d->stage == Stage::Done && d->report.imported == 2 && d->report.skipped == 1,
            "Migration TSV did not commit through normal import");
        ClickTarget(child, *d, kUndo);
        Require(d->reverted && testEngine.reverted, "Migration import could not be reverted");
        ClickTarget(child, *d, kFinish);

        // ---- 词语 + 拼音（GBK），写入阶段失败 ----
        std::tie(child, d) = open();
        LoadFile(child, *d, gbk.wstring());
        Require(d->file.encoding == coral::lexicon_io::ImportEncoding::kGb18030, "GBK not detected");
        Require(d->file.format == coral::lexicon_io::ImportLayout::kWordsAndPinyin, "Word+pinyin not detected");
        Require(ItemByPhrase(*d, "重庆").rawPinyin == "chong'qing", "File pinyin not parsed");
        Require(ItemByPhrase(*d, "上海").rawPinyin == "shang'hai", "Full-width comma delimiter not handled");
        Require(ItemByPhrase(*d, "长城").status == UserLexiconImportStatus::Invalid, "Wrong syllable count accepted");
        SavePanel(child, *d, output / "10-preview-pinyin.bmp");
        Commit(child, *d);
        Require(d->report.failed == 2 && d->report.imported == 2, "Write failures not counted");
        Require(ItemByPhrase(*d, "输入法").status == UserLexiconImportStatus::Invalid &&
            ItemByPhrase(*d, "输入法").message == "写入失败", "Write failure not merged into items");
        SavePanel(child, *d, output / "11-done-failed.bmp");
        ClickTarget(child, *d, kFailures);
        Require(d->rows.size() == 2, "Failures page should list both failures");
        SavePanel(child, *d, output / "12-failures.bmp");
        SendMessageW(child, WM_KEYDOWN, VK_ESCAPE, 0);
        Require(d->stage == Stage::Done, "Escape should go back from failures");
        SendMessageW(child, WM_KEYDOWN, VK_ESCAPE, 0);
        Require(!IsWindow(child), "Escape did not close dialog");

        // ---- 自有 CSV（UTF-16），繁体输入法 ----
        testConfig.traditional = true;
        std::tie(child, d) = open();
        Require(d->charset == coral::LanguageVariant::Traditional, "Default charset should follow the input method");
        LoadFile(child, *d, csv.wstring());
        Require(d->file.encoding == coral::lexicon_io::ImportEncoding::kUtf16Le, "UTF-16 not detected");
        Require(d->file.format == coral::lexicon_io::ImportLayout::kCoralCsv && d->file.items.size() == 2, "Coral CSV not detected");
        Require(d->file.items[0].rawLookupKey == "cq" && d->file.items[0].rawPinyin == "chong'qing", "CSV fields not kept");
        Require(d->file.items[0].charset == coral::LanguageVariant::Simplified &&
            d->file.items[1].charset == coral::LanguageVariant::Traditional, "CSV charset column not kept");

        // ---- 深色 + 150% ----
        auto& skin = SettingsWindow::testSkin;
        skin.darkCaption = true; skin.window = 0xFF191B20; skin.surface = 0xFF25282E;
        skin.hover = 0xFF30343C; skin.pressed = 0xFF383C44; skin.text = 0xFFF2F4F8; skin.sub = 0xFFB4BAC4;
        skin.muted = 0xFF8E96A2; skin.line = 0x26FFFFFF; skin.lineStrong = 0x40FFFFFF; skin.accent = 0xFF4CA0E0;
        skin.danger = 0xFFFF7A6B;
        LoadFile(child, *d, plain.wstring());
        SavePanel(child, *d, output / "13-preview-dark.bmp");
        d->scale = 1.5f;
        Require(SetWindowPos(parent, nullptr, 0, 0, 1600, 1200, SWP_NOMOVE | SWP_NOZORDER) != FALSE,
            "Cannot resize test parent");
        LexiconImportDialog::resizeToParent();
        const auto large = Panel(child, *d);
        Require(large.right - large.left == 960 && large.bottom - large.top == 960, "DPI scale failed");
        SavePanel(child, *d, output / "14-preview-dark-150.bmp");
        DestroyWindow(parent); parent = nullptr;
        Require(!IsWindow(child), "Parent destruction left dialog behind");
        std::cout << "PASS: parser (UTF-8/GBK/UTF-16, plain/pinyin/CSV), preview counts/tabs/edit/scroll, import/undo/failures, dark + DPI renders\n";
        return 0;
    } catch (const std::exception& e) {
        if (parent) DestroyWindow(parent);
        std::cerr << e.what() << '\n'; return 1;
    }
}

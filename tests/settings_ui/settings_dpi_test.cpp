#include "pch.h"
#include <cstdint>
#define private public
#include "settingswindow.h"
#undef private
// 捕获设置页提示，避免隐藏窗口测试阻塞于模态对话框。
std::vector<std::wstring> shortcut_messages;
int WINAPI settings_test_message_box(HWND, LPCWSTR text, LPCWSTR, UINT) {
    shortcut_messages.emplace_back(text);
    return IDOK;
}
// 捕获系统设置启动请求，测试不打开系统窗口或更改默认输入法。
std::vector<std::wstring> settings_launches;
INT_PTR settings_launch_result = 33;
#include "default_input_settings.h"
// 隐藏窗口回归使用导航替身，绝不操作用户的系统设置窗口。
coral::settings_app::DefaultInputSettings::State settings_navigation_next_state =
    coral::settings_app::DefaultInputSettings::State::succeeded;
namespace coral::settings_app {
DefaultInputSettings::~DefaultInputSettings() = default;
void DefaultInputSettings::stop() {}
bool DefaultInputSettings::start(HWND) {
    if (state() == State::running) return true;
    settings_launches.emplace_back(L"advanced-keyboard-settings");
    state_ = settings_launch_result > 32 ? settings_navigation_next_state : State::failed;
    return settings_launch_result > 32;
}
}
#define MessageBoxW settings_test_message_box
#include "../../src/platform/windows/coralpinyin/src/ui/settingswindow.cpp"
#undef MessageBoxW
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// 只渲染我们的隐藏测试窗口；绝不捕获用户桌面。
void render_children(HWND window, HWND root, Canvas& canvas)
{
    RECT client{};
    GetClientRect(window, &client);
    POINT origin{};
    MapWindowPoints(window, root, &origin, 1);
    const int saved = SaveDC(canvas.dc());
    SetViewportOrgEx(canvas.dc(), origin.x, origin.y, nullptr);
    IntersectClipRect(canvas.dc(), 0, 0, client.right, client.bottom);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.dc()), PRF_CLIENT);
    RestoreDC(canvas.dc(), saved);
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) render_children(child, root, canvas);
    }
}

void save_page(HWND window, const std::filesystem::path& path)
{
    RECT client{};
    GetClientRect(window, &client);
    Canvas canvas(client.right, client.bottom);
    auto surface = canvas.surface();
    surface.fill(SettingsWindow::skin().window);
    render_children(window, window, canvas);
    GdiFlush();
    coral::win32::SkinImage image;
    image.width = canvas.width(); image.height = canvas.height();
    image.pixels.assign(canvas.pixels(), canvas.pixels() + canvas.count());
    require(coral::win32::SaveSkinImagePng(image, path.wstring()), "Cannot save DPI rendering");
}

void check_scale(HWND window, UINT dpi, const std::filesystem::path& output)
{
    DpiScale scale(dpi);
    RECT suggested{ 0, 0, scale.pixels(WINDOW_W), scale.pixels(WINDOW_H) };
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
    require(g_dpi.dpi() == dpi, "DPI change did not update rendering scale");
    LOGFONTW font{};
    GetObjectW(g_fonts[F_LABEL], sizeof(font), &font);
    require(font.lfHeight == -scale.pixels(14), "Font is not rendered at device resolution");
    SettingsWindow::switchPage(static_cast<int>(SettingsEnum::SettingType::lexicon));
    const HWND lexicon = SettingsWindow::s_pages[static_cast<int>(SettingsEnum::SettingType::lexicon)];
    const HWND migration = GetDlgItem(lexicon, ID_L_IMPORT_OTHER);
    require(migration != nullptr, "Other IME import button missing");
    wchar_t migration_label[64]{};
    GetWindowTextW(migration, migration_label, 64);
    require(std::wstring(migration_label) == L"从其他输入法导入", "Other IME import label wrong");
    save_page(window, output / ("lexicon-" + std::to_string(dpi) + ".png"));
    const int speech_index = static_cast<int>(SettingsEnum::SettingType::speech);
    if (!coral::speech::win32::kSpeechFeatureEnabled) {
        const RECT hidden_nav = SettingsWindow::navRect(speech_index);
        require(IsRectEmpty(&hidden_nav), "Hidden speech navigation still has a hit area");
    }
    for (int index = 0; index < PAGE_COUNT; ++index) {
        if (!coral::speech::win32::kSpeechFeatureEnabled && index == speech_index) {
            // 临时屏蔽入口后仍构建隐藏页缓存，验证保留的页面实现可正常布局。
            BuildPageCache(index, SettingsWindow::s_pages[index]);
            continue;
        }
        const RECT nav = SettingsWindow::navRect(index);
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON,
            MAKELPARAM(scale.pixels((nav.left + nav.right) / 2), scale.pixels((nav.top + nav.bottom) / 2)));
        require(SettingsWindow::s_currentPage == index, "Navigation hit-test does not match scaled drawing");
        BuildPageCache(index, SettingsWindow::s_pages[index]);
        RECT page{};
        GetClientRect(SettingsWindow::s_pages[index], &page);
        require(std::abs(g_layouts[index].cache->width() - page.right) <= 1,
            "Page cache is not allocated at physical resolution");
        require(std::abs(g_layouts[index].cache->height() - page.bottom) <= 1,
            "Page cache height differs from physical page height");
    }
    SettingsWindow::switchPage(0);
    SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    require(SettingsWindow::s_scroll[0] == kWheelStep, "Scroll distance changed with DPI");
    HWND viewport = SettingsWindow::s_viewports[0];
    const RECT thumb = ScrollThumb(viewport, 0, SettingsWindow::s_scroll[0]);
    SendMessageW(viewport, WM_LBUTTONDOWN, MK_LBUTTON,
        MAKELPARAM(scale.pixels((thumb.left + thumb.right) / 2), scale.pixels((thumb.top + thumb.bottom) / 2)));
    require(g_scrollDrag.page == 0, "Scrollbar hit-test is not scaled");
    SendMessageW(viewport, WM_LBUTTONUP, 0, 0);
    require(g_scrollDrag.page == -1, "Scrollbar did not release capture");
    SettingsWindow::scrollPage(0, 0);
    HWND toggle = GetDlgItem(SettingsWindow::s_pages[0], ID_R_ENPUNCT);
    const LRESULT before = SendMessageW(toggle, BM_GETCHECK, 0, 0);
    SendMessageW(toggle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale.pixels(20), scale.pixels(12)));
    SendMessageW(toggle, WM_LBUTTONUP, 0, MAKELPARAM(scale.pixels(20), scale.pixels(12)));
    require(SendMessageW(toggle, BM_GETCHECK, 0, 0) != before, "Scaled toggle cannot be clicked");
    save_page(window, output / ("routine-" + std::to_string(dpi) + ".png"));
    HWND default_input = GetDlgItem(SettingsWindow::s_pages[0], ID_R_DEFAULT_INPUT);
    require(default_input && IsWindowEnabled(default_input), "Default input method entry is missing or disabled");
    SettingsWindow::scrollPage(0, 100000);
    RECT entry{}, viewport_bounds{};
    GetWindowRect(default_input, &entry);
    GetWindowRect(viewport, &viewport_bounds);
    require(entry.top >= viewport_bounds.top && entry.bottom <= viewport_bounds.bottom,
        "Cannot scroll the default input method entry into view");
    const auto launches_before = settings_launches.size();
    const auto messages_before = shortcut_messages.size();
    SendMessageW(default_input, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale.pixels(20), scale.pixels(12)));
    SendMessageW(default_input, WM_LBUTTONUP, 0, MAKELPARAM(scale.pixels(20), scale.pixels(12)));
    require(settings_launches.size() == launches_before + 1 && settings_launches.back() == L"advanced-keyboard-settings",
        "Default input method entry did not request advanced keyboard navigation once");
    require(shortcut_messages.size() == messages_before, "Successful settings launch displayed an error");
    save_page(window, output / ("routine-default-input-" + std::to_string(dpi) + ".png"));
    SettingsWindow::scrollPage(0, 0);
    SettingsWindow::switchPage(1);
    HWND paging = GetDlgItem(SettingsWindow::s_pages[1], ID_K_FLIPMINUS);
    require(paging && SendMessageW(paging, BM_GETCHECK, 0, 0) == BST_UNCHECKED,
        "Minus/plus paging missing or enabled by default");
    save_page(window, output / ("keyboard-" + std::to_string(dpi) + ".png"));
    SettingsWindow::switchPage(5);
    HWND advanced = SettingsWindow::s_pages[5];
    HWND prediction = GetDlgItem(advanced, ID_S_PREDICTION);
    require(prediction && SendMessageW(prediction, BM_GETCHECK, 0, 0) == BST_CHECKED,
        "Prediction toggle is missing or not enabled by default");
    HWND shortcut = GetDlgItem(advanced, ID_S_SHORTCUT);
    require(shortcut && IsWindowEnabled(shortcut), "Cannot choose shortcut before enabling translation");
    require(SendMessageW(shortcut, CB_GETCOUNT, 0, 0) == 13, "Missing = or F1-F12 choices");
    require(SendMessageW(shortcut, CB_GETCURSEL, 0, 0) == 0, "Translation shortcut default is not =");
    HWND api_key = GetDlgItem(advanced, ID_S_KEYID);
    require(api_key && (GetWindowLongPtrW(api_key, GWL_STYLE) & ES_PASSWORD), "AccessKey ID is not masked");
    require(GetWindowLongPtrW(GetDlgItem(advanced, ID_S_SECRET), GWL_STYLE) & ES_PASSWORD, "Secret is not masked");
    require(SendMessageW(GetDlgItem(advanced, ID_S_TARGET), CB_GETCOUNT, 0, 0) == 15, "Missing target languages");
    require(SendMessageW(GetDlgItem(advanced, ID_S_TARGET), CB_GETCURSEL, 0, 0) == 0, "Default target is not English");
    require(!IsWindowEnabled(api_key), "Disabled translation exposes editable key");
    HWND translation = GetDlgItem(advanced, ID_S_TRANSLATION);
    SendMessageW(translation, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(advanced, WM_COMMAND, MAKEWPARAM(ID_S_TRANSLATION, BN_CLICKED), reinterpret_cast<LPARAM>(translation));
    require(IsWindowEnabled(api_key), "Translation toggle did not enable key entry");
    save_page(window, output / ("translation-" + std::to_string(dpi) + ".png"));
    SendMessageW(translation, BM_SETCHECK, BST_UNCHECKED, 0);
    SendMessageW(advanced, WM_COMMAND, MAKEWPARAM(ID_S_TRANSLATION, BN_CLICKED), reinterpret_cast<LPARAM>(translation));

    SettingsWindow::switchPage(2);
    save_page(window, output / ("appearance-" + std::to_string(dpi) + ".png"));
    SettingsWindow::scrollPage(2, 430);
    save_page(window, output / ("themes-" + std::to_string(dpi) + ".png"));
    SettingsWindow::scrollPage(2, 0);
    SettingsWindow::switchPage((int)SettingsEnum::SettingType::speech);
    HWND speech = SettingsWindow::s_pages[(int)SettingsEnum::SettingType::speech];
    require(GetDlgItem(speech, ID_V_ENABLED) && GetDlgItem(speech, ID_V_MIC) && GetDlgItem(speech, ID_V_PUSHTOTALK),
        "Speech page missing");
    require(SendMessageW(GetDlgItem(speech, ID_V_ENABLED), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
        !IsWindowEnabled(GetDlgItem(speech, ID_V_SECRET)), "Speech must default off");
    require(SendMessageW(GetDlgItem(speech, ID_V_PUSHTOTALK), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
        IsWindowEnabled(GetDlgItem(speech, ID_V_SHORTCUT)), "Push-to-talk must default off");
    require(SendMessageW(GetDlgItem(speech, ID_V_SHORTCUT), CB_GETCURSEL, 0, 0) == 0, "Wrong default speech shortcut");
    save_page(window, output / ("speech-" + std::to_string(dpi) + ".png"));
    SettingsWindow::switchPage((int)SettingsEnum::SettingType::about);
    HWND auto_check = GetDlgItem(SettingsWindow::s_pages[(int)SettingsEnum::SettingType::about], ID_AB_AUTOCHECK);
    require(auto_check != nullptr && SendMessageW(auto_check, BM_GETCHECK, 0, 0) == BST_CHECKED,
        "Automatic update check switch missing or not enabled by default");
    save_page(window, output / ("about-" + std::to_string(dpi) + ".png"));
    SettingsWindow::switchPage(2);
    HWND slider = GetDlgItem(SettingsWindow::s_pages[2], ID_A_OPACITY);
    RECT slider_client{};
    logical_client_rect(slider, &slider_client);
    SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale.pixels(slider_client.right - 10), scale.pixels(12)));
    SendMessageW(slider, WM_LBUTTONUP, 0, 0);
    require(SendMessageW(slider, TBM_GETPOS, 0, 0) == 100, "Slider right endpoint does not match scaled drawing");
    SendMessageW(slider, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(scale.pixels(10), scale.pixels(12)));
    SendMessageW(slider, WM_LBUTTONUP, 0, 0);
    require(SendMessageW(slider, TBM_GETPOS, 0, 0) == coral::win32::kCandidateOpacityMin, "Slider left endpoint does not match scaled drawing");
    HWND combo = GetDlgItem(SettingsWindow::s_pages[2], ID_A_FONT);
    require(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0) == scale.pixels(28), "Native dropdown rows are not scaled");
    SettingsWindow::switchPage(0);
    SettingsWindow::scrollPage(0, kWheelStep);
}
}

void check_shortcut_conflicts(HWND window, const std::filesystem::path& output)
{
    // 使用唯一命名共享设置及空的本机夹具，不接触已安装配置或凭据。
    const auto scratch = std::filesystem::absolute(output / ("shortcut-" + std::to_string(GetCurrentProcessId())));
    std::filesystem::create_directories(scratch);
    const auto ini_path = scratch / "settings.ini";
    std::ofstream(ini_path) << "[settingsconfig]\n";
    _putenv_s("CORAL_TEST_NAMESPACE", ("shortcut-" + std::to_string(GetCurrentProcessId())).c_str());
    _putenv_s("CORAL_USER_DATA_DIR", scratch.string().c_str());
    _putenv_s("CORAL_LANGENGINE_DATA_DIR", scratch.string().c_str());
    sConfig->init(ini_path.string());
    SettingsWindow::loadAllPages();
    HWND advanced = SettingsWindow::s_pages[5];
    HWND keyboard = SettingsWindow::s_pages[1];
    const auto click = [](HWND page, int id, bool checked) {
        HWND control = GetDlgItem(page, id);
        SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(control));
    };
    const auto choose = [advanced](int index) {
        HWND control = GetDlgItem(advanced, ID_S_SHORTCUT);
        SendMessageW(control, CB_SETCURSEL, index, 0);
        SendMessageW(advanced, WM_COMMAND, MAKEWPARAM(ID_S_SHORTCUT, CBN_SELENDOK), reinterpret_cast<LPARAM>(control));
    };
    HWND about = SettingsWindow::s_pages[(int)SettingsEnum::SettingType::about];
    click(about, ID_AB_AUTOCHECK, false);
    SettingsIniManager update_saved(ini_path.string());
    require(!sConfig->getIsAutoCheck() && update_saved.load()
        && !update_saved.getSettingsConfig<bool>("isAutoCheck", true), "About page did not persist disabling automatic checks");
    click(about, ID_AB_AUTOCHECK, true);
    require(sConfig->getIsAutoCheck() && update_saved.load()
        && update_saved.getSettingsConfig<bool>("isAutoCheck", false), "About page did not persist enabling automatic checks");
    require(!sConfig->get_minus_equals_page_enabled() && sConfig->get_translation_shortcut() == "=", "Wrong clean defaults");
    shortcut_messages.clear();
    click(keyboard, ID_K_FLIPMINUS, true);
    require(shortcut_messages.empty(), "Paging warned while translation was disabled");
    click(advanced, ID_S_TRANSLATION, true);
    require(shortcut_messages.size() == 1 && shortcut_messages.back().find(L"冲突") != std::wstring::npos,
        "Enabling translation after paging did not warn");
    require(!sConfig->get_translation_enabled() && SendMessageW(GetDlgItem(advanced, ID_S_TRANSLATION), BM_GETCHECK, 0, 0) == BST_UNCHECKED,
        "Conflicting translation was enabled");
    require(!IsWindowEnabled(GetDlgItem(advanced, ID_S_KEYID)), "Rejected toggle enabled credentials");
    choose(1);
    click(advanced, ID_S_TRANSLATION, true);
    require(sConfig->get_translation_enabled() && IsWindowEnabled(GetDlgItem(advanced, ID_S_KEYID)), "F1 did not resolve conflict");
    for (int i = 1; i <= 12; ++i) {
        choose(i);
        require(sConfig->get_translation_shortcut() == coral::translation::kShortcutOptions[i].value, "Function key was not saved");
    }
    require(shortcut_messages.size() == 1, "Function key falsely conflicted");
    choose(0);
    require(shortcut_messages.size() == 2 && sConfig->get_translation_shortcut() == "F12" &&
        SendMessageW(GetDlgItem(advanced, ID_S_SHORTCUT), CB_GETCURSEL, 0, 0) == 12, "Conflicting dropdown choice was not rolled back");
    click(keyboard, ID_K_FLIPMINUS, false);
    choose(0);
    require(sConfig->get_translation_shortcut() == "=", "= not restored after disabling paging");
    click(keyboard, ID_K_FLIPMINUS, true);
    require(shortcut_messages.size() == 3 && shortcut_messages.back().find(L"失效") != std::wstring::npos,
        "Enabling paging after translation did not warn that shortcut is inactive");
    require(sConfig->get_translation_enabled() && sConfig->get_minus_equals_page_enabled(), "Reverse-order toggle changed feature state");
    choose(6);
    SettingsWindow::loadSeniorPage();
    SettingsWindow::loadKeyboardPage();
    require(SendMessageW(GetDlgItem(advanced, ID_S_SHORTCUT), CB_GETCURSEL, 0, 0) == 6, "Shortcut readback lost selection");
    require(SendMessageW(GetDlgItem(keyboard, ID_K_FLIPMINUS), BM_GETCHECK, 0, 0) == BST_CHECKED, "Paging readback lost selection");
    SettingsIniManager saved(ini_path.string());
    require(saved.load() && saved.getSettingsConfig<std::string>("translationShortcut", "") == "F6" &&
        saved.getSettingsConfig<bool>("isMinusAndPlusFlipPageOpen", false), "Shortcuts were not persisted");
    SharedMemoryConfig reader;
    require(reader.initialize(), "Cannot open isolated shared preferences");
    require(reader.getConfig("translationShortcut") == "F6" &&
        reader.getConfig("isMinusAndPlusFlipPageOpen") == "true", "Shortcuts were not published to other processes");
    // 以词定字复用按键时，取消对应翻页并同步界面、持久配置与共享配置。
    const auto choose_word_key = [keyboard](int index) {
        HWND control = GetDlgItem(keyboard, ID_K_WORD2CHAR);
        SendMessageW(control, CB_SETCURSEL, index, 0);
        SendMessageW(keyboard, WM_COMMAND, MAKEWPARAM(ID_K_WORD2CHAR, CBN_SELENDOK), reinterpret_cast<LPARAM>(control));
    };
    click(keyboard, ID_K_FLIPBRACE, true);
    choose_word_key(1);
    require(sConfig->getWordToCharacterKey() == WordToCharacterKeyType::MINUS &&
        !sConfig->get_minus_equals_page_enabled(), "Minus word-to-character did not disable matching paging");
    require(SendMessageW(GetDlgItem(keyboard, ID_K_FLIPMINUS), BM_GETCHECK, 0, 0) == BST_UNCHECKED,
        "Minus paging checkbox remained selected");
    require(sConfig->getIsBraceFlipPageOpen(), "Minus word-to-character changed bracket paging");
    require(saved.load() && !saved.getSettingsConfig<bool>("isMinusAndPlusFlipPageOpen", true) &&
        reader.getConfig("isMinusAndPlusFlipPageOpen") == "false", "Paging cancellation was not persisted or published");
    SettingsWindow::loadKeyboardPage();
    require(SendMessageW(GetDlgItem(keyboard, ID_K_FLIPMINUS), BM_GETCHECK, 0, 0) == BST_UNCHECKED &&
        SendMessageW(GetDlgItem(keyboard, ID_K_WORD2CHAR), CB_GETCURSEL, 0, 0) == 1, "Word key readback lost mutual exclusion");
    choose_word_key(0);
    require(!sConfig->get_minus_equals_page_enabled(), "Disabling word-to-character unexpectedly enabled paging");
    click(keyboard, ID_K_FLIPMINUS, true);
    choose_word_key(2);
    require(!sConfig->getIsBraceFlipPageOpen() && sConfig->get_minus_equals_page_enabled() &&
        SendMessageW(GetDlgItem(keyboard, ID_K_FLIPBRACE), BM_GETCHECK, 0, 0) == BST_UNCHECKED,
        "Existing bracket word-to-character interaction changed");
    choose_word_key(1);
    require(!sConfig->get_minus_equals_page_enabled(), "Switching bracket to minus word key retained minus paging");
    sConfig->set_translation_shortcut("F13");
    require(sConfig->get_translation_shortcut() == "F6", "Invalid shortcut accepted");
    sConfig->setBatchConfigs({{"translationShortcut", "invalid"}});
    require(sConfig->get_translation_shortcut() == "=", "Invalid persisted shortcut did not fall back to =");
    std::cout << "[PASS] Both toggle orders, warnings, 13 choices, persistence and shared configuration\n";
}

void check_speech_settings(HWND window, const std::filesystem::path& output)
{
    const int index = (int)SettingsEnum::SettingType::speech;
    SettingsWindow::switchPage(index);
    HWND page = SettingsWindow::s_pages[index];
    auto click = [&](int id, bool checked = false) {
        HWND control = GetDlgItem(page, id);
        SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(control));
    };
    require(!sConfig->get_speech_enabled() && !sConfig->get_speech_push_to_talk() &&
        sConfig->get_speech_shortcut() == "ctrl_win_shift", "Speech defaults");
    click(ID_V_ENABLED, true);
    require(sConfig->get_speech_enabled() && IsWindowEnabled(GetDlgItem(page, ID_V_SECRET)), "Speech toggle not applied");
    SetDlgItemTextW(page, ID_V_KEYID, L"speech-fixture-id");
    SetDlgItemTextW(page, ID_V_SECRET, L"speech-fixture-secret");
    click(ID_V_SAVEKEY);
    const auto credentials = sConfig->get_speech_credentials();
    require(credentials.first == "speech-fixture-id" && credentials.second == "speech-fixture-secret" &&
        sConfig->get_speech_key_configured(), "Speech credentials did not round-trip through protection");
    const auto revision = sConfig->get_speech_credential_revision();
    SharedMemoryConfig reader; require(reader.initialize(), "Cannot read speech preferences");
    require(reader.getConfig("speechEnabled") == "true" && reader.getConfig("speechKeyConfigured") == "true" &&
        reader.getConfig("speechCredentials").empty(), "Speech secrets leaked into shared settings");
    click(ID_V_PUSHTOTALK, true);
    require(reader.getConfig("speechPushToTalk") == "true" && sConfig->get_speech_push_to_talk() &&
        !IsWindowEnabled(GetDlgItem(page, ID_V_SHORTCUT)), "Push-to-talk did not persist or suspend the shortcut");
    click(ID_V_PUSHTOTALK, false);
    require(reader.getConfig("speechPushToTalk") == "false" && !sConfig->get_speech_push_to_talk() &&
        IsWindowEnabled(GetDlgItem(page, ID_V_SHORTCUT)), "Push-to-talk did not restore the shortcut");
    const auto scratch = std::filesystem::absolute(output / ("shortcut-" + std::to_string(GetCurrentProcessId())));
    std::ifstream input(scratch / "settings.ini");
    const std::string ini((std::istreambuf_iterator<char>(input)), {});
    input.close();
    require(ini.find("speech-fixture-id") == std::string::npos && ini.find("speech-fixture-secret") == std::string::npos &&
        ini.find("speechCredentials") != std::string::npos, "Speech secrets were saved as plaintext");
    // 显式模拟不允许删除共享的扫描句柄，验证临时占用释放后原子替换能完成。
    HANDLE held = CreateFileW((scratch / "settings.ini").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    require(held != INVALID_HANDLE_VALUE, "Cannot hold isolated settings fixture");
    std::thread release([held] { std::this_thread::sleep_for(std::chrono::milliseconds(70)); CloseHandle(held); });
    SettingsIniManager retry_store((scratch / "settings.ini").string());
    const bool replaced = retry_store.updateAtomically([](IniFileManager& store) { store.setIniValue("settingsconfig", "speechRetryFixture", "ready"); });
    release.join();
    require(replaced && retry_store.getSettingsConfig<std::string>("speechRetryFixture", "") == "ready", "Transient file occupancy prevented atomic settings replacement");
    for (int i = 0; i < (int)coral::speech::kSpeechShortcuts.size(); ++i) {
        HWND combo = GetDlgItem(page, ID_V_SHORTCUT);
        SendMessageW(combo, CB_SETCURSEL, i, 0);
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(ID_V_SHORTCUT, CBN_SELENDOK), reinterpret_cast<LPARAM>(combo));
        require(sConfig->get_speech_shortcut() == coral::speech::kSpeechShortcuts[i].value, "Speech shortcut persistence");
    }
    SettingsWindow::loadSpeechPage();
    require(SendMessageW(GetDlgItem(page, ID_V_SHORTCUT), CB_GETCURSEL, 0, 0) == 3, "Speech shortcut readback");
    save_page(window, output / "speech-enabled-144.png");
    SetDlgItemTextW(page, ID_V_SECRET, L""); click(ID_V_SAVEKEY);
    require(sConfig->get_speech_credentials() == credentials && sConfig->get_speech_credential_revision() == revision,
        "Incomplete credential update changed valid settings");
    SetDlgItemTextW(page, ID_V_KEYID, L""); click(ID_V_SAVEKEY);
    require(!sConfig->get_speech_key_configured() && sConfig->get_speech_credentials().first.empty(), "Speech credential removal");
    click(ID_V_ENABLED, false);
    require(reader.getConfig("speechEnabled") == "false" && !IsWindowEnabled(GetDlgItem(page, ID_V_SECRET)), "Speech disable did not propagate");
}

void check_update_notice_window(const std::filesystem::path& output)
{
    DpiContext dpi_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoralUpdateWindow notice;
    UpdateInfoLite info;
    info.serverVersion = "1.0.0.4";
    info.updateDesc = u8"改进输入体验，修复已知问题。\n前往 GitHub 查看更新说明并下载安装包。";
    require(notice.create(info, UpgradeMode::Manual), "Cannot create update notice window");
    HWND window = notice.hwnd();
    HWND button = GetDlgItem(window, 0x2200);
    wchar_t text[128]{};
    GetWindowTextW(button, text, 128);
    require(std::wstring(text) == L"前往下载", "Update action still implies automatic installation");
    require(!GetDlgItem(window, 0x2201), "Update notice exposes installer action");
    require(!FindWindowExW(window, nullptr, PROGRESS_CLASSW, nullptr), "Update notice exposes download progress");
    HWND description = FindWindowExW(window, nullptr, L"EDIT", nullptr);
    require(description && (GetWindowLongPtrW(description, GWL_STYLE) & ES_READONLY), "Update notes are not read-only");
    GetWindowTextW(description, text, 128);
    require(std::wstring(text).find(L"\r\n") != std::wstring::npos, "Update notes lost line breaks");
    int opened = 0, closed = 0;
    notice.onUpdate = [&] { ++opened; }; // 间谍动作：测试中不打开浏览器或安装程序。
    notice.onClosed = [&] { ++closed; };
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(0x2200, BN_CLICKED), reinterpret_cast<LPARAM>(button));
    require(opened == 1 && IsWindow(window), "Download button did not invoke the release-page action");
    save_page(window, output / "update-notice.png");
    SendMessageW(window, WM_CLOSE, 0, 0);
    require(!notice.is_alive() && closed == 1, "Closing the notice did not release its window");
    CoralUpdateWindow package_notice;
    info.packageAvailable = true;
    info.serverVersion = "1.0.0.3";
    info.updateDesc = u8"修复了一些内存和锁的安全问题。\n\n优化配置同步与候选显示，让输入更稳定。\n改进客户端更新体验。";
    require(package_notice.create(info, UpgradeMode::Manual), "Cannot create package update window");
    window = package_notice.hwnd();
    button = GetDlgItem(window, 0x2200);
    GetWindowTextW(button, text, 128);
    require(std::wstring(text) == L"立即更新", "Package action is missing");
    for (const UINT dpi : {96u, 120u, 144u, 192u}) {
        DpiScale scale(dpi);
        RECT frame{0, 0, scale.pixels(540), scale.pixels(470)};
        AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi);
        SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&frame));
        HWND notes = GetDlgItem(window, 0x2203);
        LOGFONTW font{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(notes, WM_GETFONT, 0, 0)), sizeof(font), &font);
        require(font.lfHeight == -scale.pixels(14), "Update text is not rendered at device DPI");
        RECT client{};
        GetClientRect(window, &client);
        for (HWND child : {notes, button, GetDlgItem(window, 0x2202)}) {
            RECT bounds{};
            GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
            require(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= client.right && bounds.bottom <= client.bottom,
                "Update controls clipped at scaled DPI");
        }
        save_page(window, output / ("update-ready-" + std::to_string(dpi) + ".png"));
    }
    RECT frame{0, 0, 810, 705};
    AdjustWindowRectExForDpi(&frame, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), 144);
    SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&frame));
    opened = 0;
    int ticks = 0;
    package_notice.onUpdate = [&] { ++opened; };
    package_notice.onTimer = [&] { ++ticks; };
    package_notice.set_busy(true);
    require(!IsWindowEnabled(button), "Downloading allows duplicate clicks");
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(0x2200, BN_CLICKED), 0);
    SendMessageW(window, WM_TIMER, 1, 0);
    require(opened == 0 && ticks == 1, "Busy state did not guard updates or poll progress");
    package_notice.set_progress(42, L"正在下载安装包");
    save_page(window, output / "update-progress-144.png");
    package_notice.showFailure(L"下载暂时中断，请检查网络连接后重试。");
    save_page(window, output / "update-failure-144.png");
    require(IsWindowEnabled(button), "Failed download cannot be retried");
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(0x2200, BN_CLICKED), 0);
    require(opened == 1, "Retry action did not fire");
    package_notice.show_installer_started();
    require(!IsWindowEnabled(button), "Installer can be started twice");
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(0x2200, BN_CLICKED), 0);
    require(opened == 1, "Disabled installer action can still be invoked");
    save_page(window, output / "update-installed-144.png");
    SendMessageW(window, WM_CLOSE, 0, 0);
    SettingsWindow::setSkin(SettingsSkin::dark());
    CoralUpdateWindow dark_notice;
    require(dark_notice.create(info, UpgradeMode::Manual), "Cannot create themed update window");
    SendMessageW(dark_notice.hwnd(), WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&frame));
    save_page(dark_notice.hwnd(), output / "update-dark-144.png");
    SendMessageW(dark_notice.hwnd(), WM_CLOSE, 0, 0);
    SettingsWindow::setSkin(SettingsSkin::light());
}

int main(int argc, char** argv)
{
    try {
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Cannot initialize image codecs");
        const auto original = GetThreadDpiAwarenessContext();
        const auto output = std::filesystem::path(argc > 1 ? argv[1] : "dpi-renders");
        std::filesystem::create_directories(output);
        check_update_notice_window(output);
        {
            DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
            InitCommonControlsEx(&controls);
            SettingsWindow::registerClass();
            HWND window = CreateWindowExW(0, kWindowClass, L"Settings DPI test", WS_OVERLAPPED | WS_CAPTION | WS_CLIPCHILDREN,
                0, 0, 1500, 1050, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            require(window != nullptr, "Cannot create production settings window");
            require(settings_launches.empty(), "Opening preferences launched system settings automatically");
            require(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(window),
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2), "Settings window did not opt into per-monitor V2");
            require(g_dpi.dpi() == GetDpiForWindow(window), "Opening scale differs from the actual monitor DPI");
            RECT initial{};
            GetWindowRect(window, &initial);
            require(initial.right - initial.left == g_dpi.pixels(WINDOW_W) &&
                initial.bottom - initial.top == g_dpi.pixels(WINDOW_H), "Initial window size does not follow monitor DPI");
            std::cout << "[PASS] Actual window DPI " << GetDpiForWindow(window) << " with per-monitor V2 awareness\n";
            for (UINT dpi : { 96u, 120u, 144u, 192u }) {
                check_scale(window, dpi, output);
                std::cout << "[PASS] DPI " << dpi << " fonts, caches, navigation, scrolling, toggle, slider, combo\n";
            }
            HWND default_input = GetDlgItem(SettingsWindow::s_pages[0], ID_R_DEFAULT_INPUT);
            for (const INT_PTR failure : { 0, 32 }) {
                settings_launch_result = failure;
                const auto messages_before = shortcut_messages.size();
                SendMessageW(SettingsWindow::s_pages[0], WM_COMMAND, MAKEWPARAM(ID_R_DEFAULT_INPUT, BN_CLICKED),
                    reinterpret_cast<LPARAM>(default_input));
                require(shortcut_messages.size() == messages_before + 1 &&
                    shortcut_messages.back().find(L"高级键盘设置") != std::wstring::npos,
                    "Failed settings launch did not explain the manual path");
            }
            settings_launch_result = 33;
            using NavigationState = coral::settings_app::DefaultInputSettings::State;
            const auto navigate = [&] {
                SendMessageW(SettingsWindow::s_pages[0], WM_COMMAND, MAKEWPARAM(ID_R_DEFAULT_INPUT, BN_CLICKED),
                    reinterpret_cast<LPARAM>(default_input));
            };
            settings_navigation_next_state = NavigationState::failed;
            const auto async_messages = shortcut_messages.size();
            navigate();
            SendMessageW(window, WM_TIMER, kDefaultInputTimer, 0);
            require(shortcut_messages.size() == async_messages + 1 &&
                shortcut_messages.back().find(L"未能自动进入") != std::wstring::npos,
                "Navigation timeout did not explain the manual fallback");
            settings_navigation_next_state = NavigationState::cancelled;
            navigate();
            const auto cancelled_messages = shortcut_messages.size();
            SendMessageW(window, WM_TIMER, kDefaultInputTimer, 0);
            require(shortcut_messages.size() == cancelled_messages, "Cancelled navigation displayed an error");
            settings_navigation_next_state = NavigationState::running;
            const auto pending_launches = settings_launches.size();
            navigate();
            navigate();
            require(settings_launches.size() == pending_launches + 1, "Repeated clicks restarted pending navigation");
            g_default_input_settings.reset();
            settings_navigation_next_state = NavigationState::succeeded;
            SettingsWindow::switchPage(2);
            SettingsWindow::scrollPage(2, kWheelStep);
            RECT suggested{ 0, 0, 1500, 1050 };
            SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(144,144), reinterpret_cast<LPARAM>(&suggested));
            require(SettingsWindow::s_currentPage == 2, "DPI change lost the active page");
            require(SettingsWindow::s_scroll[0] == kWheelStep && SettingsWindow::s_scroll[2] == kWheelStep,
                "DPI change lost the scroll position");
            SettingsWindow::scrollPage(2, 0);
            save_page(window, output / "appearance-144.png");
            const auto opaque_skin = g_candidateSkin;
            g_candidateSkin.opacity = 65;
            SettingsWindow::scrollPage(2, 430);
            save_page(window, output / "translucent-144.png");
            g_candidateSkin = opaque_skin;
            SettingsWindow::setSkin(SettingsSkin::dark());
            SettingsWindow::switchPage(0);
            save_page(window, output / "dark-144.png");
            check_shortcut_conflicts(window, output);
            SettingsWindow::setSkin(SettingsSkin::light());
            check_speech_settings(window, output);
            DestroyWindow(window);
        }
        require(AreDpiAwarenessContextsEqual(original, GetThreadDpiAwarenessContext()), "DPI scope leaked into other windows");
        CoUninitialize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

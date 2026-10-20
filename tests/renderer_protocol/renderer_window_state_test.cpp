// 在不显示窗口的情况下演练渲染器真实的消息仲裁。
#include "../../src/platform/windows/renderer/main.cpp"
#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { \
	std::fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); return 1; } } while (false)

namespace
{
bool menuEntered = false;
bool menuHideDelivered = false;
bool menuFallbackUsed = false;
int hideScenario = 0;
constexpr UINT_PTR kMenuHideTimer = 0x1234;
constexpr UINT_PTR kMenuFallbackTimer = 0x1235;
int toolbar_paints = 0;
int toolbar_hides = 0;
int speech_open_requests = 0;
LRESULT CALLBACK SpeechTestHostProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == coral::speech::win32::kSpeechOpenMessage) { ++speech_open_requests; return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK ToolbarTestProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    if (message == WM_PAINT) ++toolbar_paints;
    if (message == WM_SHOWWINDOW && !w_param) ++toolbar_hides;
    return ToolbarProc(window, message, w_param, l_param);
}

bool test_toolbar_handoff()
{
    WNDCLASSW cls{};
    cls.hInstance = ::GetModuleHandleW(nullptr);
    cls.lpszClassName = L"Coral.ToolbarHandoff.Test";
    cls.lpfnWndProc = ToolbarTestProc;
    if (!::RegisterClassW(&cls)) return false;
    g_toolbarWindow = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        cls.lpszClassName, L"", WS_POPUP, 10, 10, 0, 0, nullptr, nullptr, cls.hInstance, nullptr);
    if (!g_toolbarWindow) { ::UnregisterClassW(cls.lpszClassName, cls.hInstance); return false; }
    g_toolbarPosition = {10, 10};
    g_toolbarPositionInitialized = true; // 绝不访问已安装用户的设置。
    g_toolbar_hide_deadline = 0;
    g_presentedToolbar = {};
    coral::composition::ToolbarRenderSnapshot first;
    first.visible = true;
    first.context = {101, 1, 1};
    first.version = 1;
    first.theme = "light";
    refresh_toolbar(first, 10, 1000, 1000, false, 1000);
    bool passed = ::IsWindowVisible(g_toolbarWindow) && toolbar_paints > 0;
    const auto diagnose = [&](const char* stage) {
        if (!passed) std::fprintf(stderr, "%s: visible=%d paints=%d hides=%d owner=%llu deadline=%llu\n",
            stage, !!::IsWindowVisible(g_toolbarWindow), toolbar_paints, toolbar_hides,
            static_cast<unsigned long long>(g_presentedToolbarClient),
            static_cast<unsigned long long>(g_toolbar_hide_deadline));
    };
    diagnose("initial");
    const int initial_paints = toolbar_paints;
    const int initial_hides = toolbar_hides;

    // 前台事件、过期的失焦发布与清除可能先于下一任持有者的快照到达。
    // 它们都不能隐藏或重绘被保留的图像。
    refresh_toolbar(first, 10, 1000, 2000, false, 1010);
    auto hidden = first;
    hidden.visible = false;
    refresh_toolbar(hidden, 0, 0, 2000, false, 1020);
    passed = passed && ::IsWindowVisible(g_toolbarWindow) &&
        g_presentedToolbarClient == 0 && g_toolbar_hide_deadline == 1190;
    diagnose("loss");
    g_state.actions.clear();
    RequestToolbarAction(coral::composition::RendererActionType::ToggleAscii, 1000);
    passed = passed && g_state.actions.empty();
    auto second = first;
    second.context = {202, 2, 1};
    second.version = 7;
    refresh_toolbar(second, 20, 2000, 2000, false, 1080);
    passed = passed && ::IsWindowVisible(g_toolbarWindow) &&
        toolbar_paints == initial_paints && toolbar_hides == initial_hides &&
        g_toolbar_hide_deadline == 0 && g_presentedToolbarClient == 20;
    diagnose("handoff");
    RequestToolbarAction(coral::composition::RendererActionType::ToggleAscii, 2000);
    passed = passed && g_state.actions[20].size() == 1 &&
        g_state.actions[20].front().context.serviceInstanceId == 202 &&
        g_state.actions[20].front().snapshotVersion == 7;
    second.ascii = true;
    refresh_toolbar(second, 20, 2000, 2000, false, 1090);
    passed = passed && toolbar_paints == initial_paints + 1 && toolbar_hides == initial_hides;
    diagnose("changed");
    // 同进程重复的焦点更新同样保持连续。
    refresh_toolbar(hidden, 0, 0, 2000, false, 1100);
    refresh_toolbar(second, 30, 2000, 2000, false, 1140);
    passed = passed && toolbar_paints == initial_paints + 1 && g_presentedToolbarClient == 30;
    // 不使用本输入法的窗口按原定期限隐藏，
    // 即使交接期内到达更多无关刷新也是如此。
    refresh_toolbar(second, 30, 2000, 3000, false, 1200);
    refresh_toolbar(second, 30, 2000, 3000, false, 1300);
    passed = passed && ::IsWindowVisible(g_toolbarWindow) && g_toolbar_hide_deadline == 1380;
    refresh_toolbar(second, 30, 2000, 3000, false, 1380);
    passed = passed && !::IsWindowVisible(g_toolbarWindow) && toolbar_hides == initial_hides + 1;
    refresh_toolbar(second, 30, 2000, 2000, false, 1400);
    refresh_toolbar(second, 30, 2000, 2000, true, 1401);
    passed = passed && !::IsWindowVisible(g_toolbarWindow) && g_toolbar_hide_deadline == 0;
    // 全屏计时器在全屏结束后可再次显示。
    refresh_toolbar(second, 30, 2000, 2000, false, 1410);
    passed = passed && ::IsWindowVisible(g_toolbarWindow);
    // 语音入口只在开关打开时扩展到第六栏，点击投递到隔离的测试宿主。
    const auto previous_namespace = std::getenv("CORAL_TEST_NAMESPACE");
    const std::string saved_namespace = previous_namespace ? previous_namespace : "";
    _putenv_s("CORAL_TEST_NAMESPACE", ("speech-toolbar-" + std::to_string(GetCurrentProcessId())).c_str());
    g_toolbar_preferences = std::make_unique<SharedMemoryConfig>();
    passed = passed && g_toolbar_preferences->initializeAndSetConfigs({{"speechEnabled", "false"}});
    refresh_toolbar(second, 30, 2000, 2000, false, 1420);
    RECT before{}; GetWindowRect(g_toolbarWindow, &before);
    const UINT dpi = WindowDpi(g_toolbarWindow);
    const POINT microphone_point{Scale(kToolbarPaddingX + 5 * (kToolbarButtonSize + kToolbarSpacing) + 13, dpi), Scale(15, dpi)};
    passed = passed && HitToolbarButton(g_toolbarWindow, microphone_point) == -1;
    g_toolbar_preferences->setConfig("speechEnabled", "true");
    refresh_toolbar(second, 30, 2000, 2000, false, 1430);
    RECT after{}; GetWindowRect(g_toolbarWindow, &after);
    if (coral::speech::win32::kSpeechFeatureEnabled) {
        passed = passed && after.right-after.left > before.right-before.left &&
            HitToolbarButton(g_toolbarWindow, microphone_point) == 5;
        if (!passed) std::fprintf(stderr,"speech geometry: before=%ld after=%ld count=%d hit=%d\n",before.right-before.left,after.right-after.left,toolbar_button_count(),HitToolbarButton(g_toolbarWindow,microphone_point));
        WNDCLASSW speech_class{}; speech_class.hInstance=cls.hInstance;
        speech_class.lpszClassName=coral::speech::win32::kSpeechHostClass; speech_class.lpfnWndProc=SpeechTestHostProc;
        RegisterClassW(&speech_class);
        HWND speech_host=CreateWindowExW(0,speech_class.lpszClassName,coral::speech::win32::speech_host_title().c_str(),0,
            0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
        g_presentedToolbarProcessId=ForegroundProcessId();
        SendMessageW(g_toolbarWindow,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(microphone_point.x,microphone_point.y));
        SendMessageW(g_toolbarWindow,WM_LBUTTONUP,0,MAKELPARAM(microphone_point.x,microphone_point.y));
        if (!g_presentedToolbarProcessId) {
            // 无交互前台的构建会话仍验证同一生产路由及其所有权拒绝。
            g_presentedToolbarProcessId=2000;
            passed = passed && !request_toolbar_speech(3000) && request_toolbar_speech(2000);
        }
        MSG speech_message{};
        while (PeekMessageW(&speech_message,speech_host,0,0,PM_REMOVE)) DispatchMessageW(&speech_message);
        passed = passed && speech_open_requests == 1;
        if (speech_open_requests != 1) std::fprintf(stderr,"speech click: requests=%d foreground=%lu host=%p presented=%lu\n",speech_open_requests,ForegroundProcessId(),speech_host,g_presentedToolbarProcessId);
        DestroyWindow(speech_host); UnregisterClassW(speech_class.lpszClassName,cls.hInstance);
    } else {
        // 临时屏蔽：即便配置打开也不增加第六栏，也不投递打开消息。
        passed = passed && after.right-after.left == before.right-before.left &&
            toolbar_button_count() == 5 && HitToolbarButton(g_toolbarWindow, microphone_point) == -1 &&
            !request_toolbar_speech(2000) && speech_open_requests == 0;
        if (!passed) std::fprintf(stderr,"speech disabled geometry: before=%ld after=%ld count=%d hit=%d\n",before.right-before.left,after.right-after.left,toolbar_button_count(),HitToolbarButton(g_toolbarWindow,microphone_point));
    }
    g_toolbar_preferences->setConfig("speechEnabled", "false");
    refresh_toolbar(second, 30, 2000, 2000, false, 1440);
    passed = passed && toolbar_button_count() == 5 && HitToolbarButton(g_toolbarWindow,microphone_point) == -1;
    g_toolbar_preferences.reset(); g_speech_toolbar_enabled=false;
    _putenv_s("CORAL_TEST_NAMESPACE",saved_namespace.c_str());
    // 也投递真实的 WM_TIMER 消息：从不发布替换内容的应用
    // 不能让旧工具栏无限期留在屏幕上。
    g_state.toolbar = {};
    g_state.toolbarClient = 0;
    g_state.toolbarProcessId = 0;
    const ULONGLONG started = ::GetTickCount64();
    refresh_toolbar({}, 0, 0, 3000, false, started);
    while (::IsWindowVisible(g_toolbarWindow) && ::GetTickCount64() - started < 1000)
    {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (::IsWindowVisible(g_toolbarWindow))
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    passed = passed && !::IsWindowVisible(g_toolbarWindow) &&
        ::GetTickCount64() - started >= kToolbarHandoffDelayMs;
    ::KillTimer(g_toolbarWindow, kToolbarHandoffTimer);
    ::DestroyWindow(g_toolbarWindow);
    g_toolbarWindow = nullptr;
    ::UnregisterClassW(cls.lpszClassName, cls.hInstance);
    if (!passed) std::fprintf(stderr, "toolbar handoff regression failed\n");
    return passed;
}

struct TestRegistryKey
{
    std::wstring path = L"Software\\CoralPinyin.ToolbarTest." + std::to_wstring(::GetCurrentProcessId()) +
        L"." + std::to_wstring(::GetTickCount64());
    ~TestRegistryKey() { ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str()); }
};

int test_toolbar_persistence()
{
    TestRegistryKey key;
    POINT restored{12, 34};
    CHECK(!load_toolbar_position(HKEY_CURRENT_USER, key.path.c_str(), restored));
    CHECK(restored.x == 12 && restored.y == 34);
    CHECK(save_toolbar_position(HKEY_CURRENT_USER, key.path.c_str(), {-720, 144}));
    // 独立验证存储的字节布局，并在新进程中恢复。
    std::uint64_t packed = 0;
    DWORD bytes = sizeof(packed);
    CHECK(::RegGetValueW(HKEY_CURRENT_USER, key.path.c_str(), L"ToolbarPosition", RRF_RT_REG_QWORD,
        nullptr, &packed, &bytes) == ERROR_SUCCESS);
    CHECK(packed == 0x00000090FFFFFD30ULL);
    wchar_t executable[MAX_PATH]{};
    CHECK(::GetModuleFileNameW(nullptr, executable, MAX_PATH));
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --read-toolbar-position " + key.path;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    CHECK(::CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
    const DWORD waited = ::WaitForSingleObject(process.hProcess, 10000);
    DWORD code = 1;
    const bool exited = ::GetExitCodeProcess(process.hProcess, &code) != FALSE;
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    CHECK(waited == WAIT_OBJECT_0 && exited && code == 0);
    CHECK(save_toolbar_position(HKEY_CURRENT_USER, key.path.c_str(), {640, -480}));
    CHECK(load_toolbar_position(HKEY_CURRENT_USER, key.path.c_str(), restored));
    CHECK(restored.x == 640 && restored.y == -480);
    HKEY opened = nullptr;
    CHECK(::RegOpenKeyExW(HKEY_CURRENT_USER, key.path.c_str(), 0, KEY_SET_VALUE, &opened) == ERROR_SUCCESS);
    const DWORD invalid = 123;
    const auto write_result = ::RegSetValueExW(opened, L"ToolbarPosition", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&invalid), sizeof(invalid));
    ::RegCloseKey(opened);
    CHECK(write_result == ERROR_SUCCESS);
    CHECK(!load_toolbar_position(HKEY_CURRENT_USER, key.path.c_str(), restored));
    CHECK(restored.x == 640 && restored.y == -480);
    const POINT clamped = ClampToolbarPosition({LONG_MAX, LONG_MIN}, 190, 34);
    CHECK(clamped.x != LONG_MAX && clamped.y != LONG_MIN);
    return 0;
}

LRESULT CALLBACK MenuTestProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_ENTERMENULOOP) menuEntered = true;
	return CandidateProc(window, message, wParam, lParam);
}

void CALLBACK HideDuringMenu(HWND window, UINT, UINT_PTR timer, DWORD)
{
	::KillTimer(window, timer);
	menuHideDelivered = true;
	if (hideScenario == 0) {
		::ShowWindow(window, SW_HIDE);
		return;
	}
	if (hideScenario == 4) {
		::SendMessageW(window, WM_CANCELMODE, 0, 0);
		return;
	}
	coral::renderer_ipc::Message update;
	update.clientId = 42;
	update.context = { 101, 102, 103 };
	update.version = 2;
	if (hideScenario == 1) update.type = coral::renderer_ipc::SessionOperation::HideContext;
	else if (hideScenario == 2) update.type = coral::renderer_ipc::SessionOperation::ClearClient;
	else {
		update.type = coral::renderer_ipc::SessionOperation::CandidateSnapshot;
		update.candidate = g_state.candidate;
		update.candidate.context.contextGeneration++;
	}
	ApplyMessage(update, 1000, 1000);
	::PostMessageW(window, kRefreshMessage, 0, 0);
}

void CALLBACK MenuFallback(HWND window, UINT, UINT_PTR timer, DWORD)
{
	::KillTimer(window, timer);
	menuFallbackUsed = true;
	::EndMenu();
}

bool test_candidate_backdrop_collapse()
{
    // 未转发 WINDOWPOS 消息的宿主复现分层窗口缩放：它在最终 SetWindowPos
    // 之前就更新了后备位图。
    HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        L"STATIC", L"", WS_POPUP, 10, 10, 0, 0, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) return false;
    g_candidateWindow = window;
    g_state = {};
    g_state.candidateClient = 42;
    auto& snapshot = g_state.candidate;
    snapshot.visible = snapshot.expanded = true;
    snapshot.context = {101, 102, 103};
    snapshot.version = 1;
    snapshot.fontSize = 16;
    snapshot.skin = "k:s|s:5|o:65";
    snapshot.theme = "light";
    for (std::uint64_t i = 0; i < 60; ++i)
        snapshot.candidates.push_back({i + 1, u8"啤酒", "pijiu", true});
    RefreshWindows();
    refresh_candidate_backdrop();
    HWND glass = g_candidateBackdrop.window();
    const bool active = glass && IsWindowVisible(glass);
    RECT expanded{};
    if (active) GetWindowRect(glass, &expanded);
    snapshot.expanded = false;
    ++snapshot.version;
    RefreshWindows();
    bool passed = true;
    if (active) {
        RECT expected = g_candidateView.panelRect(), actual{};
        MapWindowPoints(window, nullptr, reinterpret_cast<POINT*>(&expected), 2);
        GetWindowRect(glass, &actual);
        passed = expected.bottom - expected.top < expanded.bottom - expanded.top &&
            EqualRect(&expected, &actual);
        if (!passed) std::fprintf(stderr,
            "collapsed blur bounds: expected=(%ld,%ld,%ld,%ld), actual=(%ld,%ld,%ld,%ld)\n",
            expected.left, expected.top, expected.right, expected.bottom,
            actual.left, actual.top, actual.right, actual.bottom);
        snapshot.visible = false;
        RefreshWindows();
        passed = !IsWindowVisible(glass) && passed;
    }
    g_candidateBackdrop.reset();
    g_candidateView.clear();
    DestroyWindow(window);
    g_candidateWindow = nullptr;
    return passed;
}

bool TestNativeMenuCancellation()
{
	// 在私有桌面上演练真实的 TrackPopupMenu 消息循环；
	// 使回归测试的窗口与菜单不出现在用户可见桌面上。
	const std::wstring name = L"CoralMenuTest-" + std::to_wstring(::GetCurrentProcessId());
	HDESK desktop = ::CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
	if (!desktop) { std::fprintf(stderr, "CreateDesktop failed: %lu\n", ::GetLastError()); return false; }
	bool passed = true;
	std::thread worker([&] {
		if (!::SetThreadDesktop(desktop)) { std::fprintf(stderr, "SetThreadDesktop failed: %lu\n", ::GetLastError()); passed = false; return; }
		WNDCLASSW cls{};
		cls.hInstance = ::GetModuleHandleW(nullptr);
		cls.lpszClassName = L"Coral.MenuLifecycle.Test";
		cls.lpfnWndProc = MenuTestProc;
		if (!::RegisterClassW(&cls)) { passed = false; return; }
		for (hideScenario = 0; hideScenario < 5; ++hideScenario) {
			HWND window = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
				cls.lpszClassName, L"", WS_POPUP, 10, 10, 100, 50, nullptr, nullptr, cls.hInstance, nullptr);
			if (!window) { passed = false; break; }
			g_candidateWindow = window;
			g_state = {};
			g_state.candidateClient = 42;
			g_state.candidate.context = { 101, 102, 103 };
			g_state.candidate.version = 1;
			g_state.candidate.visible = true;
			g_state.candidate.candidates = {{7, u8"词语", "ciyu", true}};
			g_presentedCandidate = g_state.candidate;
			g_presentedCandidateClient = 42;
			::ShowWindow(window, SW_SHOWNOACTIVATE);
			HMENU menu = CreateCandidateMenu(g_state.candidate.candidates[0]);
			if (!menu) { ::DestroyWindow(window); passed = false; break; }
			menuEntered = menuHideDelivered = menuFallbackUsed = false;
			::SetTimer(window, kMenuHideTimer, 25, HideDuringMenu);
			::SetTimer(window, kMenuFallbackTimer, 1000, MenuFallback);
			const UINT command = TrackCandidateMenu(window, menu, {20, 20},
				{42, {101, 102, 103}, 7});
			const bool closed = menuEntered && menuHideDelivered && !menuFallbackUsed &&
				command == 0 && g_candidateMenuCancelled && !g_candidateMenuActive;
			if (!closed) std::fprintf(stderr,
				"menu scenario %d: entered=%d hide=%d fallback=%d command=%u cancelled=%d active=%d error=%lu\n",
				hideScenario, menuEntered, menuHideDelivered, menuFallbackUsed, command,
				g_candidateMenuCancelled, g_candidateMenuActive, ::GetLastError());
			passed = passed && closed;
			::KillTimer(window, kMenuHideTimer);
			::KillTimer(window, kMenuFallbackTimer);
			::DestroyMenu(menu);
			::DestroyWindow(window);
			g_candidateWindow = nullptr;
		}
		passed = test_candidate_backdrop_collapse() && passed;
		passed = test_toolbar_handoff() && passed;
		::UnregisterClassW(cls.lpszClassName, cls.hInstance);
	});
	worker.join();
	::CloseDesktop(desktop);
	return passed;
}
}

int main(int argc, char** argv)
{
	if (argc == 3 && std::strcmp(argv[1], "--read-toolbar-position") == 0)
	{
		POINT position{};
		return load_toolbar_position(HKEY_CURRENT_USER, Utf8ToWide(argv[2]).c_str(), position) &&
			position.x == -720 && position.y == 144 ? 0 : 1;
	}
	CHECK(test_toolbar_persistence() == 0);
	using namespace coral;
	renderer_ipc::Message desktop;
	desktop.type = renderer_ipc::SessionOperation::ToolbarSnapshot;
	desktop.clientId = 10;
	desktop.toolbar.context = { 100, 0, 0 }; // 无可编辑文档的资源管理器。
	desktop.toolbar.version = 1;
	desktop.toolbar.visible = true;
	ApplyMessage(desktop, 1000, 1000);
	CHECK(g_state.toolbarClient == 10 && g_state.toolbarProcessId == 1000);
	CHECK(ToolbarBelongsToForeground(g_state.toolbarProcessId, 1000));
	CHECK(!ToolbarBelongsToForeground(g_state.toolbarProcessId, 2000));
	CHECK(!ToolbarBelongsToForeground(0, 0));

	// 切换到其他应用时隐藏旧渲染器，即使其 TSF 隐藏缺失。
	renderer_ipc::Message app = desktop;
	app.clientId = 20;
	app.toolbar.context = { 200, 1, 1 };
	ApplyMessage(app, 2000, 2000);
	CHECK(g_state.toolbarClient == 20);
	ApplyMessage(desktop, 1000, 2000); // 迟到的桌面显示不能窃取所有权。
	CHECK(g_state.toolbarClient == 20 && g_state.toolbarProcessId == 2000);
	desktop.toolbar.visible = false;
	ApplyMessage(desktop, 1000, 2000); // 迟到的后台隐藏不能隐藏应用。
	CHECK(g_state.toolbar.visible && g_state.toolbarClient == 20);

	app.toolbar.version = 2;
	ApplyMessage(app, 2000, 2000);
	app.toolbar.version = 1;
	app.toolbar.visible = false;
	ApplyMessage(app, 2000, 2000); // 来自当前客户端的过期隐藏。
	CHECK(g_state.toolbar.visible && g_state.toolbar.version == 2);

	// 返回桌面会发布服务令牌并保留菜单动作。
	desktop.toolbar.visible = true;
	desktop.toolbar.version = 3;
	ApplyMessage(desktop, 1000, 1000);
	g_presentedToolbar = g_state.toolbar;
	g_presentedToolbarClient = g_state.toolbarClient;
	g_presentedToolbarProcessId = g_state.toolbarProcessId;
	RequestToolbarAction(composition::RendererActionType::OpenSettingsMenu, 2000);
	CHECK(g_state.actions.count(10) == 0); // 过期的可见工具栏不能路由点击。
	RequestToolbarAction(composition::RendererActionType::OpenSettingsMenu, 1000);
	CHECK(g_state.actions[10].size() == 1);
	const auto& action = g_state.actions[10].front();
	CHECK(action.context.serviceInstanceId == 100 && action.context.contextId == 0);
	CHECK(action.type == composition::RendererActionType::OpenSettingsMenu && action.snapshotVersion == 3);

	std::vector<std::uint8_t> frame;
	std::string error;
	renderer_ipc::Message decoded;
	CHECK(renderer_ipc::EncodeMessage(desktop, frame, error));
	CHECK(renderer_ipc::DecodeMessage(frame.data(), frame.size(), decoded, error));
	CHECK(decoded.toolbar.context.serviceInstanceId == 100 && decoded.toolbar.context.contextId == 0);

	renderer_ipc::Message clear;
	clear.type = renderer_ipc::SessionOperation::ClearClient;
	clear.clientId = 20;
	ApplyMessage(clear, 2000, 1000);
	CHECK(g_state.toolbar.visible);
	clear.clientId = 10;
	ApplyMessage(clear, 1000, 1000);
	CHECK(!g_state.toolbar.visible && g_state.toolbarProcessId == 0);
	CHECK(g_state.actions.count(10) == 0);

	// 单字符保留置顶/添加动作但没有删除动作，
	// 即使其显示标签带有读音标注。
	composition::RenderCandidate character{1, u8"中(zhōng)", "zhong", false};
	HMENU menu = CreateCandidateMenu(character);
	CHECK(menu != nullptr);
	CHECK(::GetMenuState(menu, kDeleteCandidateCommand, MF_BYCOMMAND) == static_cast<UINT>(-1));
	CHECK(::GetMenuState(menu, kPinCandidateCommand, MF_BYCOMMAND) != static_cast<UINT>(-1));
	::DestroyMenu(menu);
	character.canDelete = true;
	menu = CreateCandidateMenu(character);
	CHECK(::GetMenuState(menu, kDeleteCandidateCommand, MF_BYCOMMAND) != static_cast<UINT>(-1));
	::DestroyMenu(menu);

	composition::CandidateRenderSnapshot candidate;
	candidate.visible = true;
	candidate.context = {1, 2, 3};
	candidate.candidates = {{7, u8"词语", "ciyu", true}};
	const CandidateMenuTarget target{42, candidate.context, 7};
	CHECK(CandidateMenuTargetIsCurrent(target, 42, candidate));
	candidate.version++;
	CHECK(CandidateMenuTargetIsCurrent(target, 42, candidate)); // 仅锚点刷新。
	CHECK(!CandidateMenuTargetIsCurrent(target, 43, candidate));
	candidate.context.contextGeneration++;
	CHECK(!CandidateMenuTargetIsCurrent(target, 42, candidate));
	candidate.context = target.context;
	candidate.candidates.clear();
	CHECK(!CandidateMenuTargetIsCurrent(target, 42, candidate));
	CHECK(TestNativeMenuCancellation());
	return 0;
}

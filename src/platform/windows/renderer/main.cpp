#include "coral/product/product_identity.h"
#include "renderer_protocol.h"
#include "candidate_view.h"
#include "candidate_backdrop.h"
#include "../common/search_package_sid.h"
#include "resource.h"
#include "shared_preferences.h"
#include "speech_runtime.h"

#include <windows.h>
#include <windowsx.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
constexpr UINT kRefreshMessage = WM_APP + 0x411;
constexpr UINT_PTR kToolbarFullscreenTimer = 1;
constexpr UINT_PTR kToolbarHandoffTimer = 2;
constexpr UINT kToolbarHandoffDelayMs = 180;
constexpr int kToolbarButtonSize = 26;
constexpr int kToolbarPaddingX = 10;
constexpr int kToolbarPaddingY = 4;
constexpr int kToolbarSpacing = 10;
constexpr int kToolbarRadius = 5;
constexpr int kToolbarButtonCount = 5;
bool g_speech_toolbar_enabled = false;
std::unique_ptr<SharedMemoryConfig> g_toolbar_preferences;
int toolbar_button_count() { return kToolbarButtonCount + (g_speech_toolbar_enabled ? 1 : 0); }
constexpr UINT kPinCandidateCommand = 1;
constexpr UINT kDeleteCandidateCommand = 2;
constexpr UINT kAddPhraseCommand = 3;
std::atomic<bool> g_running{ true };
std::uint64_t g_generation = 0;
HWND g_candidateWindow = nullptr;
HWND g_toolbarWindow = nullptr;
coral::win32::CandidateView g_candidateView;
coral::win32::CandidateBackdrop g_candidateBackdrop;
int g_toolbarHover = -1;
bool g_toolbarDragging = false;
bool g_toolbarTracking = false;
int g_toolbarPressed = -1;
POINT g_toolbarDragStart{};
POINT g_toolbarDragOffset{};
POINT g_toolbarPosition{};
bool g_toolbarPositionInitialized = false;
ULONGLONG g_toolbar_hide_deadline = 0;
bool g_toolbar_presented_dark = false;

bool g_scrollDragging = false;
int g_scrollDragOffset = 0;
std::uint64_t g_layoutQueuedVersion = 0;
coral::composition::RenderContextToken g_layoutQueuedContext{};
int g_layoutQueuedFontSize = 0;

struct Palette
{
	COLORREF background;
	COLORREF border;
	COLORREF text;
	COLORREF secondaryText;
	COLORREF selectedBackground;
	COLORREF selectedText;
	COLORREF hoverBackground;
};

struct RenderState
{
	coral::composition::CandidateRenderSnapshot candidate;
	coral::composition::ToolbarRenderSnapshot toolbar;
	std::uint64_t candidateClient = 0;
	std::uint64_t toolbarClient = 0;
	DWORD toolbarProcessId = 0;
	std::unordered_map<std::uint64_t, std::deque<coral::composition::RendererAction>> actions;
};
std::mutex g_stateMutex;
RenderState g_state;
// 管道线程可能在 UI 线程重建单元格之前发布更新的快照。
// 绘制与命中测试必须使用生成当前单元格几何的快照，
// 绝不能使用 g_state 中最新的待处理快照。
coral::composition::CandidateRenderSnapshot g_presentedCandidate;
std::uint64_t g_presentedCandidateClient = 0;
coral::composition::ToolbarRenderSnapshot g_presentedToolbar;
std::uint64_t g_presentedToolbarClient = 0;
DWORD g_presentedToolbarProcessId = 0;

bool SameContext(const coral::composition::RenderContextToken& lhs,
	const coral::composition::RenderContextToken& rhs);

struct CandidateMenuTarget
{
	std::uint64_t clientId = 0;
	coral::composition::RenderContextToken context;
	coral::composition::CandidateId candidateId = 0;
};
CandidateMenuTarget g_candidateMenuTarget;
bool g_candidateMenuActive = false;
bool g_candidateMenuCancelled = false;

bool CandidateMenuTargetIsCurrent(const CandidateMenuTarget& target,
	std::uint64_t clientId, const coral::composition::CandidateRenderSnapshot& snapshot)
{
	return target.clientId == clientId && snapshot.visible &&
		!snapshot.letterSelection && !snapshot.listLayout &&
		SameContext(target.context, snapshot.context) &&
		std::any_of(snapshot.candidates.begin(), snapshot.candidates.end(),
			[&](const auto& item) { return item.id == target.candidateId; });
}

void CancelCandidateMenu()
{
	if (!g_candidateMenuActive) return;
	g_candidateMenuCancelled = true;
	::EndMenu();
}

HMENU CreateCandidateMenu(const coral::composition::RenderCandidate& candidate)
{
	HMENU menu = ::CreatePopupMenu();
	if (!menu) return nullptr;
	::AppendMenuW(menu, MF_STRING, kPinCandidateCommand, L"固定到首位");
	if (candidate.canDelete)
		::AppendMenuW(menu, MF_STRING, kDeleteCandidateCommand, L"删除候选词");
	::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
	::AppendMenuW(menu, MF_STRING, kAddPhraseCommand, L"添加到常用语");
	return menu;
}

UINT TrackCandidateMenu(HWND window, HMENU menu, POINT point,
	const CandidateMenuTarget& target)
{
	if (g_candidateMenuActive) return 0;
	g_candidateMenuTarget = target;
	g_candidateMenuCancelled = false;
	g_candidateMenuActive = true;
	const UINT command = ::TrackPopupMenu(menu,
		TPM_RETURNCMD | TPM_RIGHTBUTTON,
		point.x, point.y, 0, window, nullptr);
	g_candidateMenuActive = false;
	if (g_candidateMenuCancelled || !::IsWindowVisible(window)) return 0;
	std::lock_guard<std::mutex> lock(g_stateMutex);
	return CandidateMenuTargetIsCurrent(target, g_state.candidateClient, g_state.candidate)
		? command : 0;
}

struct LocalMemory
{
	HLOCAL value = nullptr;
	~LocalMemory() { if (value) ::LocalFree(value); }
};

std::wstring Utf8ToWide(const std::string& value)
{
	if (value.empty()) return {};
	const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	std::wstring result(std::max(0, length), L'\0');
	if (length > 0)
		::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
			result.data(), length);
	return result;
}

int Scale(int value, UINT dpi)
{
	return ::MulDiv(value, static_cast<int>(dpi), 96);
}

int ScaleCandidate(int value, UINT dpi, int fontSize)
{
	return std::max(1, ::MulDiv(Scale(value, dpi), std::clamp(fontSize, 12, 28), 16));
}

int ScalePresentedCandidate(int value, UINT dpi)
{
	return ScaleCandidate(value, dpi, g_presentedCandidate.fontSize);
}

UINT WindowDpi(HWND window)
{
	using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
	static auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
		::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	return getDpi ? getDpi(window) : 96;
}

DWORD ForegroundProcessId()
{
	HWND foreground = ::GetForegroundWindow();
	DWORD processId = 0;
	const DWORD threadId = ::GetWindowThreadProcessId(foreground, &processId);
	// UWP 输入可能属于由 ApplicationFrameHost 托管的子窗口。
	GUITHREADINFO gui{ sizeof(gui) };
	if (threadId && ::GetGUIThreadInfo(threadId, &gui) && gui.hwndFocus &&
		::GetAncestor(gui.hwndFocus, GA_ROOT) == ::GetAncestor(foreground, GA_ROOT))
		::GetWindowThreadProcessId(gui.hwndFocus, &processId);
	return processId;
}

bool ToolbarBelongsToForeground(DWORD sourceProcess, DWORD foregroundProcess)
{
	return sourceProcess != 0 && sourceProcess == foregroundProcess;
}

bool request_toolbar_speech(DWORD foreground_process) {
    return g_speech_toolbar_enabled && ToolbarBelongsToForeground(g_presentedToolbarProcessId, foreground_process) &&
        coral::speech::win32::request_speech_window();
}

bool ForegroundIsFullscreen()
{
	HWND foreground = ::GetForegroundWindow();
	if (!foreground) return false;
	HWND root = ::GetAncestor(foreground, GA_ROOT);
	if (!root || root == ::GetShellWindow() || !::IsWindowVisible(root) ||
		::IsIconic(root)) return false;
	wchar_t className[64]{};
	::GetClassNameW(root, className, static_cast<int>(std::size(className)));
	if (::wcscmp(className, L"Progman") == 0 ||
		::wcscmp(className, L"WorkerW") == 0 ||
		::wcscmp(className, L"Shell_TrayWnd") == 0) return false;
	DWORD processId = 0;
	::GetWindowThreadProcessId(root, &processId);
	if (processId == ::GetCurrentProcessId()) return false;

	MONITORINFO monitor{ sizeof(monitor) };
	if (!::GetMonitorInfoW(::MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST),
		&monitor)) return false;
	RECT client{};
	if (!::GetClientRect(root, &client)) return false;
	::MapWindowPoints(root, nullptr, reinterpret_cast<POINT*>(&client), 2);
	const RECT& screen = monitor.rcMonitor;
	constexpr int tolerance = 2;
	return client.left <= screen.left + tolerance &&
		client.top <= screen.top + tolerance &&
		client.right >= screen.right - tolerance &&
		client.bottom >= screen.bottom - tolerance;
}

bool SystemUsesDarkTheme()
{
	DWORD light = 1;
	DWORD bytes = sizeof(light);
	::RegGetValueW(HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
	return light == 0;
}

bool IsDarkTheme(const std::string& theme)
{
	return theme == "dark" || ((theme == "system" || theme.empty()) && SystemUsesDarkTheme());
}

Palette ToolbarPalette(bool dark)
{
	if (dark)
		return { RGB(46, 46, 50), RGB(80, 80, 86), RGB(238, 238, 242),
			RGB(238, 238, 242), RGB(66, 66, 72), RGB(238, 238, 242), RGB(66, 66, 72) };
	return { RGB(249, 249, 250), RGB(225, 225, 228), RGB(32, 33, 39),
		RGB(32, 33, 39), RGB(235, 235, 237), RGB(32, 33, 39), RGB(235, 235, 237) };
}

HFONT CreateUiFont(int logicalPixels, UINT dpi, const std::string& configuredName = {})
{
	std::wstring name = Utf8ToWide(configuredName);
	if (name.empty()) name = L"Microsoft YaHei";
	return ::CreateFontW(-Scale(std::clamp(logicalPixels, 10, 48), dpi), 0, 0, 0,
		FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name.c_str());
}

std::uint64_t CandidateListSignature(const coral::composition::CandidateRenderSnapshot& snapshot)
{
	std::uint64_t hash = 1469598103934665603ULL;
	for (const auto& candidate : snapshot.candidates)
	{
		hash ^= candidate.id;
		hash *= 1099511628211ULL;
	}
	return hash ? hash : 1;
}

bool BuildPipeSecurity(SECURITY_ATTRIBUTES& attributes, LocalMemory& descriptor)
{
	HANDLE token = nullptr;
	if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
	DWORD bytes = 0;
	::GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
	std::vector<std::uint8_t> buffer(bytes);
	const bool tokenOk = ::GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes) != FALSE;
	::CloseHandle(token);
	if (!tokenOk) return false;
	LPWSTR sid = nullptr;
	if (!::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
		return false;
	LocalMemory sidOwner;
	sidOwner.value = sid;
	// 绝不允许中/低完整性进程驱动提权的候选窗口。
	// 常规渲染器仍对 AppContainer 客户端可用。
	std::wstring sddl = coral::renderer_ipc::UsesHighIntegrityRenderer()
		? L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) + L")S:(ML;;NW;;;HI)"
		: L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid) +
			L")(A;;GRGW;;;AC)";
	if (!coral::renderer_ipc::UsesHighIntegrityRenderer())
	{
		for (const auto& packageSid : coral::win32::SearchPackageSids())
			sddl += L"(A;;GRGW;;;" + packageSid + L")";
		sddl += L"S:(ML;;NW;;;LW)";
	}
	if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
		reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value), nullptr))
		return false;
	attributes = { sizeof(attributes), descriptor.value, FALSE };
	return true;
}

void QueueAction(std::uint64_t clientId, coral::composition::RendererAction action)
{
	if (!clientId) return;
	std::lock_guard<std::mutex> lock(g_stateMutex);
	auto& queue = g_state.actions[clientId];
	if (action.type == coral::composition::RendererActionType::RequestPrefetch)
	{
		const auto duplicate = std::find_if(queue.begin(), queue.end(), [&action](const auto& queued) {
			return queued.type == action.type && queued.snapshotVersion == action.snapshotVersion &&
				queued.context.contextId == action.context.contextId &&
				queued.context.contextGeneration == action.context.contextGeneration;
		});
		if (duplicate != queue.end()) return;
	}
	if (action.type == coral::composition::RendererActionType::CandidateLayout)
	{
		queue.erase(std::remove_if(queue.begin(), queue.end(), [&action](const auto& queued) {
			return queued.type == action.type &&
				queued.context.contextId == action.context.contextId &&
				queued.context.contextGeneration == action.context.contextGeneration;
			}), queue.end());
	}
	const bool itemAction = action.type == coral::composition::RendererActionType::select_candidate ||
		action.type == coral::composition::RendererActionType::PinCandidate ||
		action.type == coral::composition::RendererActionType::DeleteCandidate ||
		action.type == coral::composition::RendererActionType::AddPhrase;
	if (itemAction)
	{
		// 用户直接操作不得排在布局/预取记账之后。
		if (queue.size() >= 32) queue.pop_back();
		queue.push_front(std::move(action));
	}
	else if (action.type == coral::composition::RendererActionType::CandidateLayout)
		queue.push_front(std::move(action));
	else if (queue.size() < 32)
		queue.push_back(std::move(action));
}

void RequestCandidateAction(coral::composition::RendererActionType type)
{
	coral::composition::RendererAction action;
	action.type = type;
	action.context = g_presentedCandidate.context;
	action.snapshotVersion = g_presentedCandidate.version;
	QueueAction(g_presentedCandidateClient, std::move(action));
}

int HitToolbarButton(HWND window, POINT point)
{
	const UINT dpi = WindowDpi(window);
	const int top = Scale(kToolbarPaddingY, dpi);
	const int size = Scale(kToolbarButtonSize, dpi);
	if (point.y < top || point.y >= top + size) return -1;
	const int x = point.x - Scale(kToolbarPaddingX, dpi);
	const int stride = Scale(kToolbarButtonSize + kToolbarSpacing, dpi);
	if (x < 0 || x / stride >= toolbar_button_count() || x % stride >= size) return -1;
	return x / stride;
}

void RequestToolbarAction(coral::composition::RendererActionType type, DWORD foregroundProcessId)
{
	if (!g_presentedToolbarClient || !g_presentedToolbar.visible) return;
	if (!ToolbarBelongsToForeground(g_presentedToolbarProcessId, foregroundProcessId)) return;
	coral::composition::RendererAction action;
	action.type = type;
	action.context = g_presentedToolbar.context;
	action.snapshotVersion = g_presentedToolbar.version;
	QueueAction(g_presentedToolbarClient, std::move(action));
}

POINT ClampToolbarPosition(POINT position, int width, int height)
{
    const auto bounded_sum = [](LONG coordinate, int extent) {
        return static_cast<LONG>(std::clamp(static_cast<std::int64_t>(coordinate) + extent,
            static_cast<std::int64_t>(LONG_MIN), static_cast<std::int64_t>(LONG_MAX)));
    };
	RECT bounds{ position.x, position.y, bounded_sum(position.x, width), bounded_sum(position.y, height) };
	HMONITOR monitor = ::MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info{ sizeof(info) };
	RECT work{};
	if (::GetMonitorInfoW(monitor, &info)) work = info.rcWork;
	else ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	position.x = std::clamp(static_cast<int>(position.x), static_cast<int>(work.left),
		std::max(static_cast<int>(work.left), static_cast<int>(work.right) - width));
	position.y = std::clamp(static_cast<int>(position.y), static_cast<int>(work.top),
		std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height));
	return position;
}

std::wstring toolbar_settings_key()
{
    return std::wstring(coral::product::kSettingsRegistryWide) + L"\\Renderer";
}

bool load_toolbar_position(HKEY root, const wchar_t* key_path, POINT& position)
{
    // 用一个值把两个带符号坐标保存在一起，包括位于主屏幕
    // 左侧/上方的显示器。不要持久化原生结构体布局。
    std::uint64_t packed = 0;
    DWORD bytes = sizeof(packed);
    if (::RegGetValueW(root, key_path, L"ToolbarPosition", RRF_RT_REG_QWORD,
        nullptr, &packed, &bytes) != ERROR_SUCCESS || bytes != sizeof(packed)) return false;
    const auto signed_coordinate = [](std::uint32_t value) -> LONG {
        return static_cast<LONG>(value <= INT32_MAX ? static_cast<std::int64_t>(value) :
            static_cast<std::int64_t>(value) - (std::int64_t{1} << 32));
    };
    position = { signed_coordinate(static_cast<std::uint32_t>(packed)),
        signed_coordinate(static_cast<std::uint32_t>(packed >> 32)) };
    return true;
}

bool save_toolbar_position(HKEY root, const wchar_t* key_path, POINT position)
{
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root, key_path, 0, nullptr, 0, KEY_SET_VALUE,
        nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    const std::uint64_t packed = static_cast<std::uint32_t>(position.x) |
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.y)) << 32);
    const LSTATUS result = ::RegSetValueExW(key, L"ToolbarPosition", 0, REG_QWORD,
        reinterpret_cast<const BYTE*>(&packed), sizeof(packed));
    ::RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

void persist_toolbar_position()
{
    if (!save_toolbar_position(HKEY_CURRENT_USER, toolbar_settings_key().c_str(), g_toolbarPosition))
        ::OutputDebugStringW(L"CoralPinyin: failed to save toolbar position.\n");
}

bool toolbar_transition_visible(bool requested, bool fullscreen, bool visible, ULONGLONG now)
{
    if (requested || fullscreen || !visible)
    {
        g_toolbar_hide_deadline = 0;
        return requested && !fullscreen;
    }
    // 焦点丢失与下一次 TSF 发布是分开到达的。在有限的交接期内保留
    // 现有像素；重复隐藏不会延长该期限。
    if (!g_toolbar_hide_deadline) g_toolbar_hide_deadline = now + kToolbarHandoffDelayMs;
    return now < g_toolbar_hide_deadline;
}

bool same_toolbar_appearance(const coral::composition::ToolbarRenderSnapshot& lhs,
    const coral::composition::ToolbarRenderSnapshot& rhs)
{
    return lhs.ascii == rhs.ascii && lhs.capsLock == rhs.capsLock &&
        lhs.englishPunctuation == rhs.englishPunctuation &&
        lhs.full_width == rhs.full_width && lhs.traditional == rhs.traditional;
}

void refresh_toolbar(const coral::composition::ToolbarRenderSnapshot& toolbar,
    std::uint64_t client_id, DWORD process_id, DWORD foreground_id, bool fullscreen, ULONGLONG now)
{
    if (!g_toolbarWindow) return;
    const bool was_visible = ::IsWindowVisible(g_toolbarWindow) != FALSE;
    const bool requested = toolbar.visible && ToolbarBelongsToForeground(process_id, foreground_id);
    const bool show = toolbar_transition_visible(requested, fullscreen, was_visible, now);
    if (g_toolbar_hide_deadline && show)
        ::SetTimer(g_toolbarWindow, kToolbarHandoffTimer,
            static_cast<UINT>(g_toolbar_hide_deadline - now), nullptr);
    else ::KillTimer(g_toolbarWindow, kToolbarHandoffTimer);

    if (!requested || fullscreen)
    {
        // 保留的图像暂时处于惰性状态。绝不向前一个应用程序派发点击，
        // 即使在它的多个线程之间切换也是如此。
        g_presentedToolbarClient = 0;
        g_presentedToolbarProcessId = 0;
        if (!show && was_visible) ::ShowWindow(g_toolbarWindow, SW_HIDE);
        return;
    }

    const UINT dpi = WindowDpi(g_toolbarWindow);
    if (g_toolbar_preferences)
        g_speech_toolbar_enabled = coral::speech::win32::kSpeechFeatureEnabled &&
            g_toolbar_preferences->getConfig("speechEnabled") == "true";
    if (g_toolbarHover >= toolbar_button_count()) g_toolbarHover = -1;
    const int width = Scale(kToolbarPaddingX * 2 + kToolbarButtonSize * toolbar_button_count() +
        kToolbarSpacing * (toolbar_button_count() - 1), dpi);
    const int height = Scale(kToolbarPaddingY * 2 + kToolbarButtonSize, dpi);
    const bool dark = IsDarkTheme(toolbar.theme);
    RECT bounds{};
    ::GetWindowRect(g_toolbarWindow, &bounds);
    const bool resized = bounds.right - bounds.left != width || bounds.bottom - bounds.top != height;
    const bool repaint = !was_visible || resized || dark != g_toolbar_presented_dark ||
        !same_toolbar_appearance(g_presentedToolbar, toolbar);
    // 即使像素未变化，所有权/版本也必须递增。
    g_presentedToolbar = toolbar;
    g_presentedToolbarClient = client_id;
    g_presentedToolbarProcessId = process_id;
    g_toolbar_presented_dark = dark;
    if (!g_toolbarPositionInitialized)
    {
        g_toolbarPositionInitialized = load_toolbar_position(HKEY_CURRENT_USER,
            toolbar_settings_key().c_str(), g_toolbarPosition);
        if (!g_toolbarPositionInitialized)
        {
            RECT work{};
            ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            g_toolbarPosition = { work.right - width - Scale(8, dpi),
                work.bottom - height - Scale(8, dpi) };
            g_toolbarPositionInitialized = true;
        }
    }
    g_toolbarPosition = ClampToolbarPosition(g_toolbarPosition, width, height);
    if (!was_visible || resized || bounds.left != g_toolbarPosition.x || bounds.top != g_toolbarPosition.y)
        ::SetWindowPos(g_toolbarWindow, HWND_TOPMOST, g_toolbarPosition.x,
            g_toolbarPosition.y, width, height, SWP_NOACTIVATE);
    // 在首次显示之前先构建分层图像，且没有擦除阶段。
    if (repaint)
    {
        ::InvalidateRect(g_toolbarWindow, nullptr, FALSE);
        // UpdateWindow/RedrawWindow 会跳过隐藏窗口的绘制。需显式发送我们的
        // 绘制处理，使 alpha 位图在显示前就绪。
        ::SendMessageW(g_toolbarWindow, WM_PAINT, 0, 0);
    }
    if (!was_visible) ::ShowWindow(g_toolbarWindow, SW_SHOWNOACTIVATE);
}

void refresh_pending_toolbar()
{
    coral::composition::ToolbarRenderSnapshot toolbar;
    std::uint64_t client_id = 0;
    DWORD process_id = 0;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        toolbar = g_state.toolbar;
        client_id = g_state.toolbarClient;
        process_id = g_state.toolbarProcessId;
    }
    refresh_toolbar(toolbar, client_id, process_id, ForegroundProcessId(),
        ForegroundIsFullscreen(), ::GetTickCount64());
}

bool SearchPanelBounds(RECT& bounds)
{
	HWND foreground = ::GetForegroundWindow();
	if (!foreground) return false;
	HWND root = ::GetAncestor(foreground, GA_ROOT);
	if (!root) return false;
	DWORD processId = 0;
	::GetWindowThreadProcessId(root, &processId);
	if (!processId) return false;
	HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
	if (!process) return false;
	wchar_t path[MAX_PATH]{};
	DWORD length = MAX_PATH;
	const bool found = ::QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
	::CloseHandle(process);
	if (!found) return false;
	const wchar_t* name = std::wcsrchr(path, L'\\');
	name = name ? name + 1 : path;
	return ::_wcsicmp(name, L"SearchHost.exe") == 0 &&
		::GetWindowRect(root, &bounds) != FALSE;
}

void KeepCandidateOutsideSearchPanel(int& x, int& y, int width, int height,
	const RECT& work, int gap)
{
	RECT panel{};
	if (!SearchPanelBounds(panel)) return;
	const RECT candidate{ x, y, x + width, y + height };
	if (candidate.right <= panel.left || candidate.left >= panel.right ||
		candidate.bottom <= panel.top || candidate.top >= panel.bottom)
		return;

	// 搜索界面使用位于渲染器常规桌面窗口栈之外的 CoreWindow，
	// 置顶候选窗口仍可能被该面板遮挡，因此当常规插入符位置与其重叠时，
	// 应将候选窗口放到面板旁边。
	const POINT positions[]{
		{ panel.left - width - gap, y },
		{ panel.right + gap, y },
		{ x, panel.top - height - gap },
		{ x, panel.bottom + gap }
	};
	const int originalX = x;
	const int originalY = y;
	int bestDistance = INT_MAX;
	for (const POINT position : positions)
	{
		if (position.x < work.left || position.y < work.top ||
			position.x + width > work.right || position.y + height > work.bottom)
			continue;
		const int distance = std::abs(position.x - originalX) +
			std::abs(position.y - originalY);
		if (distance < bestDistance)
		{
			bestDistance = distance;
			x = position.x;
			y = position.y;
		}
	}
}

void refresh_candidate_backdrop();

void RefreshWindows()
{
	coral::composition::CandidateRenderSnapshot candidate;
	coral::composition::ToolbarRenderSnapshot toolbar;
	std::uint64_t candidateClient = 0;
	std::uint64_t toolbarClient = 0;
	DWORD toolbarProcessId = 0;
	{
		std::lock_guard<std::mutex> lock(g_stateMutex);
		candidate = g_state.candidate;
		toolbar = g_state.toolbar;
		candidateClient = g_state.candidateClient;
		toolbarClient = g_state.toolbarClient;
		toolbarProcessId = g_state.toolbarProcessId;
	}
	if (g_candidateMenuActive &&
		!CandidateMenuTargetIsCurrent(g_candidateMenuTarget, candidateClient, candidate))
		CancelCandidateMenu();
	if (g_candidateWindow)
	{
		if (candidate.visible && (!candidate.candidates.empty() || !candidate.hint.empty()))
		{
			g_candidateView.update(g_candidateWindow, candidate);
			g_presentedCandidate = candidate;
			g_presentedCandidateClient = candidateClient;
			g_candidateView.setHover(-1);
			const bool newLayout = g_layoutQueuedVersion != candidate.version ||
				g_layoutQueuedFontSize != candidate.fontSize ||
				g_layoutQueuedContext.serviceInstanceId != candidate.context.serviceInstanceId ||
				g_layoutQueuedContext.contextId != candidate.context.contextId ||
				g_layoutQueuedContext.contextGeneration != candidate.context.contextGeneration;
			if (newLayout)
			{
				coral::composition::RendererAction layout;
				layout.type = coral::composition::RendererActionType::CandidateLayout;
				layout.context = candidate.context;
				layout.snapshotVersion = candidate.version;
				layout.candidateId = CandidateListSignature(candidate);
				layout.rowStarts = g_candidateView.rowStarts();
				layout.itemCenters = g_candidateView.itemCenters();
				QueueAction(candidateClient, std::move(layout));
				if (candidate.expanded && candidate.hasMore &&
					g_candidateView.nearEnd())
				{
					coral::composition::RendererAction prefetch;
					prefetch.type = coral::composition::RendererActionType::RequestPrefetch;
					prefetch.context = candidate.context;
					prefetch.snapshotVersion = candidate.version;
					QueueAction(candidateClient, std::move(prefetch));
				}
				g_layoutQueuedVersion = candidate.version;
				g_layoutQueuedFontSize = candidate.fontSize;
				g_layoutQueuedContext = candidate.context;
			}
			const UINT dpi = WindowDpi(g_candidateWindow);
			RECT work{};
			const RECT anchor{ candidate.anchorLeft, candidate.anchorTop,
				candidate.anchorRight, candidate.anchorBottom };
			HMONITOR monitor = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
			MONITORINFO info{ sizeof(info) };
			if (::GetMonitorInfoW(monitor, &info)) work = info.rcWork;
			else ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
			const POINT position = g_candidateView.place(anchor, work, Scale(2, dpi));
			int x = position.x;
			int y = position.y;
			KeepCandidateOutsideSearchPanel(x, y, g_candidateView.width(), g_candidateView.height(),
				work, Scale(6, dpi));
			g_candidateView.present(g_candidateWindow, { x, y });
			::SetWindowPos(g_candidateWindow, HWND_TOPMOST, x, y,
				g_candidateView.width(), g_candidateView.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW);
			// UpdateLayeredWindow 可能在 SetWindowPos 之前消化掉尺寸变化；
			// 因此每次布局都要同步，即使没有 WINDOWPOS 通知。
			refresh_candidate_backdrop();
		}
		else
		{
			g_presentedCandidate = candidate;
			g_presentedCandidateClient = candidateClient;
			g_candidateView.clear();
			g_candidateBackdrop.hide();
			::ShowWindow(g_candidateWindow, SW_HIDE);
		}
	}
    refresh_toolbar(toolbar, toolbarClient, toolbarProcessId, ForegroundProcessId(),
        ForegroundIsFullscreen(), ::GetTickCount64());
}

void refresh_candidate_backdrop()
{
    RECT panel = g_candidateView.panelRect();
    MapWindowPoints(g_candidateWindow, nullptr, reinterpret_cast<POINT*>(&panel), 2);
    const auto& snapshot = g_candidateView.snapshot();
    const auto skin = coral::win32::CandidateThemeConfig::parse(snapshot.skin);
    g_candidateBackdrop.update(g_candidateWindow, panel, skin.opacity < 100,
        IsDarkTheme(snapshot.theme));
}

LRESULT CALLBACK CandidateProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if ((message == WM_SHOWWINDOW && !wParam) || message == WM_DESTROY)
        g_candidateBackdrop.hide();
    if (message == WM_THEMECHANGED || message == WM_SETTINGCHANGE ||
        message == 0x031E /*  WM_DWMCOMPOSITIONCHANGED（DWM 合成模式变化） */)
        g_candidateBackdrop.reset();
    if (message == WM_WINDOWPOSCHANGED || message == WM_THEMECHANGED ||
        message == WM_SETTINGCHANGE || message == 0x031E /*  WM_DWMCOMPOSITIONCHANGED（DWM 合成模式变化） */)
        refresh_candidate_backdrop();
	if ((message == WM_SHOWWINDOW && !wParam) ||
		message == WM_CANCELMODE || message == WM_DESTROY)
		CancelCandidateMenu();
	if (message == kRefreshMessage) { RefreshWindows(); return 0; }
	if (message == WM_ENTERIDLE && g_candidateMenuActive)
	{
		bool current = false;
		{
			std::lock_guard<std::mutex> lock(g_stateMutex);
			current = CandidateMenuTargetIsCurrent(g_candidateMenuTarget,
				g_state.candidateClient, g_state.candidate);
		}
		if (!current) CancelCandidateMenu();
	}
	if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
	if (message == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
	{
		POINT point{};
		::GetCursorPos(&point);
		::ScreenToClient(window, &point);
		::SetCursor(::LoadCursorW(nullptr,
			g_candidateView.clickableAt(point) ? IDC_HAND : IDC_ARROW));
		return TRUE;
	}
	if (message == WM_TIMER && wParam == coral::win32::CandidateView::kAnimationTimer)
	{
		g_candidateView.onTimer(window);
		return 0;
	}
	if (message == WM_MOUSEMOVE)
	{
		if (g_scrollDragging)
		{
			RECT track = g_candidateView.scrollTrack();
			RECT thumb = g_candidateView.scrollThumb();
			const int rows = static_cast<int>(g_candidateView.rowStarts().size());
			const int maxTop = std::max(0, rows - g_candidateView.visibleRows());
			const int travel = (track.bottom - track.top) - (thumb.bottom - thumb.top);
			if (maxTop > 0 && travel > 0)
				g_candidateView.setTopRow(
					static_cast<int>(GET_Y_LPARAM(lParam) - g_scrollDragOffset - track.top) * maxTop / travel);
			g_candidateView.redraw(window);
			if (g_candidateView.nearEnd())
				RequestCandidateAction(coral::composition::RendererActionType::RequestPrefetch);
			return 0;
		}
		if (g_candidateView.setPointer({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			g_candidateView.redraw(window);
		TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
		::TrackMouseEvent(&track);
		return 0;
	}
	if (message == WM_MOUSELEAVE)
	{
		g_candidateView.clearPointer();
		g_candidateView.redraw(window);
		return 0;
	}
	if (message == WM_CAPTURECHANGED)
	{
		g_scrollDragging = false;
		g_candidateView.setScrollDragging(false);
		g_candidateView.redraw(window);
		return 0;
	}
	if (message == WM_LBUTTONDOWN)
	{
		POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT thumb = g_candidateView.scrollThumb();
		if (!::IsRectEmpty(&thumb) && ::PtInRect(&thumb, point))
		{
			g_scrollDragging = true;
			g_scrollDragOffset = point.y - thumb.top;
			g_candidateView.setScrollDragging(true);
			::SetCapture(window);
			g_candidateView.redraw(window);
			return 0;
		}
		RECT track = g_candidateView.scrollTrack();
		if (!::IsRectEmpty(&thumb) && ::PtInRect(&track, point))
		{
			const int rows = static_cast<int>(g_candidateView.rowStarts().size());
			const int maxTop = std::max(0, rows - g_candidateView.visibleRows());
			const int usable = std::max(1, static_cast<int>(track.bottom - track.top));
			g_candidateView.setTopRow(
				static_cast<int>(point.y - track.top) * maxTop / usable);
			g_candidateView.redraw(window);
			if (g_candidateView.nearEnd())
				RequestCandidateAction(coral::composition::RendererActionType::RequestPrefetch);
			return 0;
		}
	}
	if (message == WM_LBUTTONUP)
	{
		if (g_scrollDragging)
		{
			g_scrollDragging = false;
			::ReleaseCapture();
			return 0;
		}
		POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		RECT expand = g_candidateView.expandRect();
		if (::PtInRect(&expand, point))
		{
			RequestCandidateAction(coral::composition::RendererActionType::ToggleExpansion);
			return 0;
		}
		const int index = g_candidateView.hit({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		const auto& candidate = g_presentedCandidate;
		const std::uint64_t clientId = g_presentedCandidateClient;
		if (index >= 0 && static_cast<std::size_t>(index) < candidate.candidates.size())
		{
			coral::composition::RendererAction action;
			action.type = coral::composition::RendererActionType::select_candidate;
			action.context = candidate.context;
			action.snapshotVersion = candidate.version;
			action.candidateIndex = static_cast<std::uint32_t>(index);
			action.candidateId = candidate.candidates[index].id;
			QueueAction(clientId, std::move(action));
		}
		return 0;
	}
	if (message == WM_MOUSEWHEEL)
	{
		const auto& candidate = g_presentedCandidate;
		if (!candidate.expanded) return 0;
		const int rows = static_cast<int>(g_candidateView.rowStarts().size());
		const int maxTop = std::max(0, rows - g_candidateView.visibleRows());
		const int delta = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1;
		g_candidateView.scrollRows(delta);
		g_candidateView.redraw(window);
		if (g_candidateView.nearEnd())
			RequestCandidateAction(coral::composition::RendererActionType::RequestPrefetch);
		return 0;
	}
	if (message == WM_RBUTTONUP)
	{
		const int index = g_candidateView.hit({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		// TrackPopupMenu 会派发可能替换此快照的嵌套消息。
		const auto candidate = g_presentedCandidate;
		const std::uint64_t clientId = g_presentedCandidateClient;
		if (index < 0 || static_cast<std::size_t>(index) >= candidate.candidates.size())
			return 0;
		// V 模式（含剪贴板入口）与剪贴板列表的候选不能固定或删除，右键与左键一样选中。
		if (candidate.letterSelection || candidate.listLayout)
		{
			coral::composition::RendererAction action;
			action.type = coral::composition::RendererActionType::select_candidate;
			action.context = candidate.context;
			action.snapshotVersion = candidate.version;
			action.candidateIndex = static_cast<std::uint32_t>(index);
			action.candidateId = candidate.candidates[index].id;
			QueueAction(clientId, std::move(action));
			return 0;
		}

		HMENU menu = CreateCandidateMenu(candidate.candidates[index]);
		if (!menu)
			return 0;
		POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		::ClientToScreen(window, &point);
		const CandidateMenuTarget target{ clientId, candidate.context, candidate.candidates[index].id };
		const UINT command = TrackCandidateMenu(window, menu, point, target);
		::DestroyMenu(menu);

		coral::composition::RendererActionType actionType = coral::composition::RendererActionType::None;
		if (command == kPinCandidateCommand)
			actionType = coral::composition::RendererActionType::PinCandidate;
		else if (command == kDeleteCandidateCommand)
			actionType = coral::composition::RendererActionType::DeleteCandidate;
		else if (command == kAddPhraseCommand)
			actionType = coral::composition::RendererActionType::AddPhrase;
		if (actionType != coral::composition::RendererActionType::None)
		{
			coral::composition::RendererAction action;
			action.type = actionType;
			action.context = candidate.context;
			action.snapshotVersion = candidate.version;
			action.candidateIndex = static_cast<std::uint32_t>(index);
			action.candidateId = candidate.candidates[index].id;
			QueueAction(clientId, std::move(action));
		}
		return 0;
	}
	if (message == WM_PAINT)
	{
		PAINTSTRUCT paint{};
		::BeginPaint(window, &paint);
		::EndPaint(window, &paint);
		g_candidateView.redraw(window);
		return 0;
	}
	return ::DefWindowProcW(window, message, wParam, lParam);
}

namespace
{
// 浮动工具栏的逐像素 alpha 表面。圆角区域以带符号距离场光栅化，
// 使边缘保持平滑；这一点与旧的 COLORKEY + SetWindowRgn 方案不同，
// 该方案的边缘天生只有 1 位。
struct ArgbCanvas
{
	int width = 0;
	int height = 0;
	std::vector<std::uint32_t> pixels;  // 预乘 ARGB，自顶向下

	void reset(int w, int h)
	{
		width = w;
		height = h;
		pixels.assign(static_cast<std::size_t>(w) * h, 0u);
	}
};

float RoundedRectSdf(float px, float py, float left, float top, float right, float bottom,
	float radius)
{
	const float halfW = (right - left) * 0.5f;
	const float halfH = (bottom - top) * 0.5f;
	const float cx = left + halfW;
	const float cy = top + halfH;
	const float qx = std::abs(px - cx) - (halfW - radius);
	const float qy = std::abs(py - cy) - (halfH - radius);
	const float outsideX = std::max(qx, 0.0f);
	const float outsideY = std::max(qy, 0.0f);
	const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);
	const float inside = std::min(std::max(qx, qy), 0.0f);
	return outside + inside - radius;
}

void BlendRoundRect(ArgbCanvas& canvas, float left, float top, float right, float bottom,
	float radius, COLORREF color, int alpha)
{
	const int x0 = std::max(0, static_cast<int>(std::floor(left)) - 1);
	const int y0 = std::max(0, static_cast<int>(std::floor(top)) - 1);
	const int x1 = std::min(canvas.width - 1, static_cast<int>(std::ceil(right)) + 1);
	const int y1 = std::min(canvas.height - 1, static_cast<int>(std::ceil(bottom)) + 1);
	const float srcA = static_cast<float>(alpha) / 255.0f;
	const float srcR = static_cast<float>(GetRValue(color)) / 255.0f;
	const float srcG = static_cast<float>(GetGValue(color)) / 255.0f;
	const float srcB = static_cast<float>(GetBValue(color)) / 255.0f;
	for (int y = y0; y <= y1; ++y)
	{
		for (int x = x0; x <= x1; ++x)
		{
			const float d = RoundedRectSdf(x + 0.5f, y + 0.5f, left, top, right, bottom, radius);
			const float coverage = std::clamp(0.5f - d, 0.0f, 1.0f) * srcA;
			if (coverage <= 0.0f) continue;
			std::uint32_t& pixel = canvas.pixels[static_cast<std::size_t>(y) * canvas.width + x];
			const float dstA = static_cast<float>(pixel >> 24) / 255.0f;
			const float dstB = static_cast<float>(pixel & 0xFF) / 255.0f;
			const float dstG = static_cast<float>((pixel >> 8) & 0xFF) / 255.0f;
			const float dstR = static_cast<float>((pixel >> 16) & 0xFF) / 255.0f;
			const float outA = coverage + dstA * (1.0f - coverage);
			const float outR = srcR * coverage + dstR * (1.0f - coverage);
			const float outG = srcG * coverage + dstG * (1.0f - coverage);
			const float outB = srcB * coverage + dstB * (1.0f - coverage);
			pixel = (static_cast<std::uint32_t>(std::clamp(outA, 0.0f, 1.0f) * 255.0f + 0.5f) << 24) |
				(static_cast<std::uint32_t>(std::clamp(outB, 0.0f, 1.0f) * 255.0f + 0.5f)) |
				(static_cast<std::uint32_t>(std::clamp(outG, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
				(static_cast<std::uint32_t>(std::clamp(outR, 0.0f, 1.0f) * 255.0f + 0.5f) << 16);
		}
	}
}

void PresentToolbar(HWND window)
{
	RECT client{};
	if (!::GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0) return;
	const int width = client.right;
	const int height = client.bottom;
	const UINT dpi = WindowDpi(window);
	const Palette palette = ToolbarPalette(IsDarkTheme(g_presentedToolbar.theme));

	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = width;
	info.bmiHeader.biHeight = -height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;
	HDC screen = ::GetDC(nullptr);
	HDC dc = ::CreateCompatibleDC(screen);
	void* bits = nullptr;
	HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!dc || !bitmap || !bits)
	{
		if (bitmap) ::DeleteObject(bitmap);
		if (dc) ::DeleteDC(dc);
		::ReleaseDC(nullptr, screen);
		return;
	}

	// 软 alpha 绘制：先画带 1px 边框环的面板，再画悬停按钮的胶囊形。
	ArgbCanvas canvas;
	canvas.reset(width, height);
	const float radius = static_cast<float>(Scale(kToolbarRadius, dpi));
	const float borderW = static_cast<float>(std::max(1, Scale(1, dpi)));
	BlendRoundRect(canvas, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
		radius, palette.border, 255);
	BlendRoundRect(canvas, borderW, borderW, static_cast<float>(width) - borderW,
		static_cast<float>(height) - borderW, std::max(1.0f, radius - borderW),
		palette.background, 255);
	const float pillRadius = static_cast<float>(Scale(3, dpi));
	const float pillTop = static_cast<float>(Scale(kToolbarPaddingY, dpi));
	const float pillBottom = pillTop + static_cast<float>(Scale(kToolbarButtonSize, dpi));
	if (g_toolbarHover >= 0)
	{
		const float left = static_cast<float>(Scale(
			kToolbarPaddingX + g_toolbarHover * (kToolbarButtonSize + kToolbarSpacing), dpi));
		const float right = left + static_cast<float>(Scale(kToolbarButtonSize, dpi));
		BlendRoundRect(canvas, left, pillTop, right, pillBottom, pillRadius,
			palette.hoverBackground, 255);
	}

	// GDI 绘制会清掉它触碰到的每个像素的 alpha 字节，因此要先备份
	// alpha 通道，待文字/图标绘制完成后再恢复。
	const std::size_t count = canvas.pixels.size();
	std::vector<std::uint8_t> alpha(count);
	for (std::size_t i = 0; i < count; ++i) alpha[i] = static_cast<std::uint8_t>(canvas.pixels[i] >> 24);
	std::memcpy(bits, canvas.pixels.data(), count * sizeof(std::uint32_t));
	HGDIOBJ oldBitmap = ::SelectObject(dc, bitmap);

	const auto& toolbar = g_presentedToolbar;
	const wchar_t* labels[] = {
		toolbar.capsLock ? L"\u5927" : (toolbar.ascii ? L"\u82f1" : L"\u4e2d"),
		toolbar.englishPunctuation ? L".," : L"\u3002\uff0c",
		toolbar.full_width ? L"\u5168" : L"\u534a",
		toolbar.traditional ? L"\u7e41" : L"\u7b80" };
	HFONT font = CreateUiFont(14, dpi);
	HGDIOBJ oldFont = ::SelectObject(dc, font);
	::SetBkMode(dc, TRANSPARENT);
	::SetTextColor(dc, palette.text);
	for (int i = 0; i < toolbar_button_count(); ++i)
	{
		const int left = Scale(kToolbarPaddingX + i * (kToolbarButtonSize + kToolbarSpacing), dpi);
		RECT item{ left, Scale(kToolbarPaddingY, dpi),
			left + Scale(kToolbarButtonSize, dpi), Scale(kToolbarPaddingY + kToolbarButtonSize, dpi) };
		if (i == 0)
		{
			const int iconSize = Scale(20, dpi);
			HICON icon = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr),
				MAKEINTRESOURCEW(IDI_TOOLBAR_LOGO), IMAGE_ICON, iconSize, iconSize, LR_SHARED));
			if (icon)
				::DrawIconEx(dc, item.left + (item.right - item.left - iconSize) / 2,
					item.top + (item.bottom - item.top - iconSize) / 2,
					icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
		}
		else if (i == kToolbarButtonCount)
        {
            const int x = (item.left + item.right) / 2, y = (item.top + item.bottom) / 2;
            HPEN pen = CreatePen(PS_SOLID, Scale(2, dpi), palette.text);
            auto old_pen = SelectObject(dc, pen);
            auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, x - Scale(3, dpi), y - Scale(9, dpi), x + Scale(3, dpi), y + Scale(3, dpi), Scale(5, dpi), Scale(5, dpi));
            Arc(dc, x - Scale(7, dpi), y - Scale(5, dpi), x + Scale(7, dpi), y + Scale(8, dpi),
                x - Scale(7, dpi), y + Scale(1, dpi), x + Scale(7, dpi), y + Scale(1, dpi));
            MoveToEx(dc, x, y + Scale(8, dpi), nullptr); LineTo(dc, x, y + Scale(11, dpi));
            MoveToEx(dc, x - Scale(4, dpi), y + Scale(11, dpi), nullptr); LineTo(dc, x + Scale(4, dpi), y + Scale(11, dpi));
            SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(pen);
        }
        else
			::DrawTextW(dc, labels[i - 1], -1, &item, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	::GdiFlush();
	::SelectObject(dc, oldFont);
	::DeleteObject(font);

	// 恢复被 GDI 破坏的 alpha 通道。
	std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
	for (std::size_t i = 0; i < count; ++i)
		pixels[i] = (pixels[i] & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha[i]) << 24);

	SIZE size{ width, height };
	POINT source{ 0, 0 };
	BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	::UpdateLayeredWindow(window, screen, nullptr, &size, dc, &source, 0, &blend, ULW_ALPHA);
	::SelectObject(dc, oldBitmap);
	::DeleteObject(bitmap);
	::DeleteDC(dc);
	::ReleaseDC(nullptr, screen);
}
}  // namespace

LRESULT CALLBACK ToolbarProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == kRefreshMessage) { RefreshWindows(); return 0; }
	if (message == WM_TIMER && (wParam == kToolbarFullscreenTimer || wParam == kToolbarHandoffTimer))
	{
		refresh_pending_toolbar();
		return 0;
	}
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
	if (message == WM_MOUSEMOVE)
	{
		if (g_toolbarTracking && !g_toolbarDragging && (wParam & MK_LBUTTON))
		{
			POINT cursor{};
			::GetCursorPos(&cursor);
			if (std::abs(cursor.x - g_toolbarDragStart.x) >= ::GetSystemMetrics(SM_CXDRAG) ||
				std::abs(cursor.y - g_toolbarDragStart.y) >= ::GetSystemMetrics(SM_CYDRAG))
				g_toolbarDragging = true;
		}
		if (g_toolbarDragging)
		{
			POINT cursor{};
			::GetCursorPos(&cursor);
			RECT windowRect{};
			::GetWindowRect(window, &windowRect);
			POINT position{ cursor.x - g_toolbarDragOffset.x, cursor.y - g_toolbarDragOffset.y };
			position = ClampToolbarPosition(position,
				windowRect.right - windowRect.left, windowRect.bottom - windowRect.top);
			g_toolbarPosition = position;
			g_toolbarPositionInitialized = true;
			::SetWindowPos(window, nullptr, position.x, position.y, 0, 0,
				SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			::SetCursor(::LoadCursorW(nullptr, IDC_SIZEALL));
			return 0;
		}
		const int hover = HitToolbarButton(window,
			{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		if (hover != g_toolbarHover)
		{
			g_toolbarHover = hover;
			::InvalidateRect(window, nullptr, FALSE);
		}
		TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
		::TrackMouseEvent(&track);
		return 0;
	}
	if (message == WM_LBUTTONDOWN)
	{
		POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		g_toolbarPressed = HitToolbarButton(window, point);
		::GetCursorPos(&g_toolbarDragStart);
		RECT bounds{};
		::GetWindowRect(window, &bounds);
		g_toolbarDragOffset = { g_toolbarDragStart.x - bounds.left,
			g_toolbarDragStart.y - bounds.top };
		g_toolbarTracking = true;
		g_toolbarDragging = false;
		::SetCapture(window);
		return 0;
	}
	if (message == WM_CAPTURECHANGED)
	{
		if (g_toolbarDragging) persist_toolbar_position();
		g_toolbarDragging = false;
		g_toolbarTracking = false;
		g_toolbarPressed = -1;
		return 0;
	}
	if (message == WM_MOUSELEAVE)
	{
		g_toolbarHover = -1;
		::InvalidateRect(window, nullptr, FALSE);
		return 0;
	}
	if (message == WM_LBUTTONUP)
	{
		if (g_toolbarDragging)
		{
			persist_toolbar_position();
			g_toolbarDragging = false;
			g_toolbarTracking = false;
			g_toolbarPressed = -1;
			::ReleaseCapture();
			return 0;
		}
		const int index = HitToolbarButton(window,
			{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		const int pressed = g_toolbarPressed;
		g_toolbarTracking = false;
		g_toolbarPressed = -1;
		::ReleaseCapture();
		if (index < 0 || index != pressed) return 0;
        if (index == kToolbarButtonCount) {
            request_toolbar_speech(ForegroundProcessId());
            return 0;
        }
		const coral::composition::RendererActionType types[] = {
			coral::composition::RendererActionType::OpenSettingsMenu,
			coral::composition::RendererActionType::ToggleAscii,
			coral::composition::RendererActionType::TogglePunctuation,
			coral::composition::RendererActionType::ToggleFullWidth,
			coral::composition::RendererActionType::ToggleCharacterSet };
		RequestToolbarAction(types[index], ForegroundProcessId());
		return 0;
	}
	if (message == WM_RBUTTONUP)
	{
		RequestToolbarAction(coral::composition::RendererActionType::OpenSettingsMenu, ForegroundProcessId());
		return 0;
	}
	if (message == WM_PAINT)
	{
		PAINTSTRUCT paint{};
		::BeginPaint(window, &paint);
		::EndPaint(window, &paint);
		PresentToolbar(window);
		return 0;
	}
	return ::DefWindowProcW(window, message, wParam, lParam);
}

bool SameContext(const coral::composition::RenderContextToken& lhs,
	const coral::composition::RenderContextToken& rhs)
{
	return lhs.serviceInstanceId == rhs.serviceInstanceId &&
		lhs.contextId == rhs.contextId &&
		lhs.contextGeneration == rhs.contextGeneration;
}

void ApplyMessage(const coral::renderer_ipc::Message& request,
	DWORD clientProcessId, DWORD foregroundProcessId)
{
	std::lock_guard<std::mutex> lock(g_stateMutex);
	switch (request.type)
	{
	case coral::renderer_ipc::SessionOperation::CandidateSnapshot:
		// 只隐藏由该客户端与该上下文显示的候选列表。
		if (!request.candidate.visible &&
			(g_state.candidateClient != request.clientId ||
			!SameContext(g_state.candidate.context, request.candidate.context) ||
			request.candidate.version < g_state.candidate.version))
			break;
		g_state.candidate = request.candidate;
		g_state.candidateClient = request.candidate.visible ? request.clientId : 0;
		break;
	case coral::renderer_ipc::SessionOperation::ToolbarSnapshot:
		// 使用管道认证过的 PID，绝不信任快照中携带的值。
		// 常规与高完整性渲染器可以共存，但只有前台应用程序的渲染器
		// 可以显示工具栏。稍后到达的后台发布
		// 不得替换当前前台应用程序的状态。
		if (request.toolbar.visible &&
			!ToolbarBelongsToForeground(clientProcessId, foregroundProcessId)) break;
		if (!request.toolbar.visible && g_state.toolbarClient != request.clientId) break;
		if (g_state.toolbarClient == request.clientId &&
			request.toolbar.version < g_state.toolbar.version) break;
		g_state.toolbar = request.toolbar;
		g_state.toolbarClient = request.toolbar.visible ? request.clientId : 0;
		g_state.toolbarProcessId = request.toolbar.visible ? clientProcessId : 0;
		break;
	case coral::renderer_ipc::SessionOperation::HideContext:
		if (g_state.candidateClient == request.clientId &&
			SameContext(g_state.candidate.context, request.context) &&
			request.version >= g_state.candidate.version)
		{
			g_state.candidate.visible = false;
			g_state.candidateClient = 0;
		}
		break;
	case coral::renderer_ipc::SessionOperation::ClearClient:
		if (g_state.candidateClient == request.clientId)
		{
			g_state.candidate.visible = false;
			g_state.candidateClient = 0;
		}
		if (g_state.toolbarClient == request.clientId)
		{
			g_state.toolbar.visible = false;
			g_state.toolbarClient = 0;
			g_state.toolbarProcessId = 0;
		}
		g_state.actions.erase(request.clientId);
		break;
	default:
		break;
	}
}

void Serve(HANDLE pipe)
{
	std::vector<std::uint8_t> input(sizeof(coral::renderer_ipc::FrameHeader) +
		coral::renderer_ipc::kMaxPayloadBytes);
	DWORD read = 0;
	if (!::ReadFile(pipe, input.data(), static_cast<DWORD>(input.size()), &read, nullptr)) return;
	coral::renderer_ipc::Message request;
	std::string error;
	if (!coral::renderer_ipc::DecodeMessage(input.data(), read, request, error)) return;
	coral::renderer_ipc::Message response;
	response.type = request.type;
	response.clientId = request.clientId;
	response.rendererGeneration = g_generation;
	if (request.type != coral::renderer_ipc::SessionOperation::Hello &&
		request.rendererGeneration != g_generation)
		return;
	if (request.type == coral::renderer_ipc::SessionOperation::PollAction)
	{
		std::lock_guard<std::mutex> lock(g_stateMutex);
		auto& queue = g_state.actions[request.clientId];
		if (!queue.empty())
		{
			response.action = queue.front();
			queue.pop_front();
		}
	}
	else if (request.type != coral::renderer_ipc::SessionOperation::Hello)
	{
		ULONG clientProcessId = 0;
		if (!::GetNamedPipeClientProcessId(pipe, &clientProcessId)) return;
		ApplyMessage(request, clientProcessId, ForegroundProcessId());
		if (g_toolbarWindow) ::PostMessageW(g_toolbarWindow, kRefreshMessage, 0, 0);
	}
	std::vector<std::uint8_t> output;
	if (!coral::renderer_ipc::EncodeMessage(response, output, error)) return;
	DWORD written = 0;
	::WriteFile(pipe, output.data(), static_cast<DWORD>(output.size()), &written, nullptr);
}

void CALLBACK ForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
	if (g_candidateWindow) ::PostMessageW(g_candidateWindow, WM_CANCELMODE, 0, 0);
	if (g_toolbarWindow) ::PostMessageW(g_toolbarWindow, kRefreshMessage, 0, 0);
}

void PipeLoop()
{
	SECURITY_ATTRIBUTES security{};
	LocalMemory descriptor;
	if (!BuildPipeSecurity(security, descriptor)) return;
	const std::wstring pipeName = coral::renderer_ipc::DefaultRendererPipeName();
	while (g_running.load(std::memory_order_relaxed))
	{
		HANDLE pipe = ::CreateNamedPipeW(pipeName.c_str(), PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
			PIPE_UNLIMITED_INSTANCES, coral::renderer_ipc::kMaxPayloadBytes + 64,
			coral::renderer_ipc::kMaxPayloadBytes + 64, 0, &security);
		if (pipe == INVALID_HANDLE_VALUE) return;
		const BOOL connected = ::ConnectNamedPipe(pipe, nullptr) ? TRUE :
			(::GetLastError() == ERROR_PIPE_CONNECTED);
		if (connected) Serve(pipe);
		::FlushFileBuffers(pipe);
		::DisconnectNamedPipe(pipe);
		::CloseHandle(pipe);
	}
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	using SetDpiAwarenessFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
	if (auto setDpiAwareness = reinterpret_cast<SetDpiAwarenessFn>(
		::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
		setDpiAwareness(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	DWORD session_id = 0;
	::ProcessIdToSessionId(::GetCurrentProcessId(), &session_id);
	const std::wstring mutexName = coral::product::kRendererMutexPrefixWide + std::to_wstring(session_id) +
		(coral::renderer_ipc::UsesHighIntegrityRenderer() ? L".High" : L"");
	HANDLE singleton = ::CreateMutexW(nullptr, TRUE, mutexName.c_str());
	if (!singleton || ::GetLastError() == ERROR_ALREADY_EXISTS)
	{
		if (singleton) ::CloseHandle(singleton);
		return 0;
	}
	g_generation = (::GetTickCount64() << 16) ^ ::GetCurrentProcessId();
    g_toolbar_preferences = std::make_unique<SharedMemoryConfig>();
    g_toolbar_preferences->initialize();

	WNDCLASSW candidateClass{};
	candidateClass.lpfnWndProc = CandidateProc;
	candidateClass.hInstance = instance;
	candidateClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
	candidateClass.style = CS_HREDRAW | CS_VREDRAW;
	candidateClass.lpszClassName = L"CoralPinyin.RemoteCandidate.v3";
	::RegisterClassW(&candidateClass);
	WNDCLASSW toolbarClass = candidateClass;
	toolbarClass.lpfnWndProc = ToolbarProc;
	toolbarClass.lpszClassName = L"CoralPinyin.RemoteToolbar.v3";
	::RegisterClassW(&toolbarClass);
	const DWORD style = WS_POPUP;
	const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED;
	g_candidateWindow = ::CreateWindowExW(exStyle, candidateClass.lpszClassName, L"", style,
		0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
	g_toolbarWindow = ::CreateWindowExW(exStyle, toolbarClass.lpszClassName, L"", style,
		0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
	if (g_toolbarWindow) ::SetTimer(g_toolbarWindow, kToolbarFullscreenTimer, 300, nullptr);
	HWINEVENTHOOK foregroundHook = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND,
		EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);
	std::thread pipeThread(PipeLoop);
	MSG message{};
	while (::GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		::TranslateMessage(&message);
		::DispatchMessageW(&message);
	}
	if (foregroundHook) ::UnhookWinEvent(foregroundHook);
	g_candidateBackdrop.reset();
	if (g_toolbarWindow) ::KillTimer(g_toolbarWindow, kToolbarFullscreenTimer);
	if (g_toolbarWindow) ::KillTimer(g_toolbarWindow, kToolbarHandoffTimer);
	g_running.store(false, std::memory_order_relaxed);
	// 唤醒阻塞中的 ConnectNamedPipe 再汇合线程。
	HANDLE wake = ::CreateFileW(coral::renderer_ipc::DefaultRendererPipeName().c_str(),
		GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (wake != INVALID_HANDLE_VALUE) ::CloseHandle(wake);
	if (pipeThread.joinable()) pipeThread.join();
	::ReleaseMutex(singleton);
	::CloseHandle(singleton);
	return 0;
}

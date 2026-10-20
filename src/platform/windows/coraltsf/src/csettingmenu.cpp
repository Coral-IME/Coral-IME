#include "pch.h"
#include "csettingmenu.h"
#include "textservice.h"
#include "coreapi.h"

#include <windowsx.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace
{
using namespace coral::win32::soft;

constexpr wchar_t kClassName[] = L"CoralPinyin.SettingMenu";
constexpr int kItemCount = 5;
constexpr int kFooterIndex = 4;
constexpr std::array<const wchar_t*, kItemCount> kTitles = {
	L"显示工具栏", L"自定义工具栏", L"设置", L"关于", L"帮助与反馈" };

// 24-unit line icons from UI/右键菜单图标; circles are written as two arcs.
constexpr const char* kEyeIcon[] = {
	"M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z",
	"M9 12a3 3 0 1 0 6 0a3 3 0 1 0-6 0" };
constexpr const char* kStarIcon[] = {
	"M12 2l2.4 4.86L19.8 7.7l-3.9 3.8.92 5.36L12 14.34 7.18 16.86l.92-5.36-3.9-3.8 5.4-.84z",
	"M19 19l-2-2", "M5 5l2 2" };
constexpr const char* kGearIcon[] = {
	"M9 12a3 3 0 1 0 6 0a3 3 0 1 0-6 0",
	"M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06"
	"a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09"
	"A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83"
	"l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09"
	"A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0"
	"l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09"
	"a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83"
	"l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2"
	"h-.09a1.65 1.65 0 0 0-1.51 1z" };
constexpr const char* kInfoIcon[] = {
	"M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0", "M12 11v6", "M12 7.5h.01" };
constexpr const char* kHelpIcon[] = {
	"M2 12a10 10 0 1 0 20 0a10 10 0 1 0-20 0",
	"M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3", "M12 17h.01" };

struct MenuIcon
{
	const char* const* paths;
	int count;
};
constexpr std::array<MenuIcon, kItemCount> kIcons = { {
	{ kEyeIcon, 2 }, { kStarIcon, 3 }, { kGearIcon, 2 }, { kInfoIcon, 3 }, { kHelpIcon, 3 } } };

std::uint32_t Fade(std::uint32_t color, float factor)
{
	const auto alpha = static_cast<std::uint32_t>(((color >> 24) & 0xFF) *
		(std::clamp)(factor, 0.0f, 1.0f) + 0.5f);
	return (alpha << 24) | (color & 0x00FFFFFFu);
}

float EaseOut(float t)
{
	const float inverse = 1.0f - t;
	return 1.0f - inverse * inverse * inverse;
}
}

SettingMenuSkin SettingMenuSkin::light()
{
	return {};
}

SettingMenuSkin SettingMenuSkin::dark()
{
	SettingMenuSkin skin;
	skin.panel = 0xFF2B2B2E;
	skin.border = 0x26FFFFFF;
	skin.text = 0xFFF2F2F5;
	skin.icon = 0xFFC9CACF;
	skin.hoverFill = 0x12FFFFFF;
	skin.pressedFill = 0x1FFFFFFF;
	skin.separator = 0x1AFFFFFF;
	skin.switchOn = 0xFF0A84FF;
	skin.switchOff = 0xFF5F5F64;
	skin.shadows[0].color = 0x33000000;
	skin.shadows[1].color = 0x52000000;
	return skin;
}

CSettingMenu* CSettingMenu::s_hookMenu = nullptr;

CSettingMenu::~CSettingMenu()
{
	hide();
	if (m_window) ::DestroyWindow(m_window);
}

int CSettingMenu::scale(int value) const
{
	return ::MulDiv(value, m_dpi, 96);
}

float CSettingMenu::chrome(float value) const
{
	return (std::max)(1.0f, std::round(value * static_cast<float>(m_dpi) / 96.0f));
}

bool CSettingMenu::darkTheme() const
{
	return DarkTheme(sConfig->getTheme());
}

RECT CSettingMenu::itemRect(int index) const
{
	const int separator = index >= kFooterIndex ?
		2 * scale(m_skin.separatorGap) + static_cast<int>(chrome(1.0f)) : 0;
	const int top = m_panel.top + scale(m_skin.paddingY) +
		index * scale(m_skin.itemHeight) + separator;
	return { m_panel.left + scale(m_skin.paddingX), top,
		m_panel.right - scale(m_skin.paddingX), top + scale(m_skin.itemHeight) };
}

int CSettingMenu::hitTest(POINT point) const
{
	for (int i = 0; i < kItemCount; ++i)
	{
		const RECT bounds = itemRect(i);
		if (::PtInRect(&bounds, point)) return i;
	}
	return -1;
}

float CSettingMenu::switchProgress(Clock::time_point now) const
{
	const int duration = (std::max)(1, m_skin.switchAnimationMs);
	const float elapsed = std::chrono::duration<float, std::milli>(now - m_switchStart).count();
	if (elapsed >= static_cast<float>(duration)) return m_switchTo;
	const float t = (std::max)(0.0f, elapsed / static_cast<float>(duration));
	return m_switchFrom + (m_switchTo - m_switchFrom) * EaseOut(t);
}

void CSettingMenu::toggle(const RECT& anchor)
{
	if (m_window && ::IsWindowVisible(m_window)) { hide(); return; }
	if (!m_window)
	{
		WNDCLASSW cls{};
		cls.lpfnWndProc = WindowProc;
		cls.hInstance = g_hInst;
		cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
		cls.lpszClassName = kClassName;
		::RegisterClassW(&cls);
		m_window = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
			WS_EX_NOACTIVATE | WS_EX_LAYERED, kClassName, L"", WS_POPUP,
			0, 0, 0, 0, nullptr, nullptr, g_hInst, this);
		if (!m_window) return;
	}
	HMONITOR monitor = ::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info{ sizeof(info) };
	RECT work{};
	if (::GetMonitorInfoW(monitor, &info)) work = info.rcWork;
	else ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
	::SetWindowPos(m_window, nullptr, anchor.left, anchor.top, 0, 0,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	m_dpi = g_fGetDpiForWindow ? static_cast<int>(g_fGetDpiForWindow(m_window)) : 96;
	m_skin = m_skinOverride ? *m_skinOverride :
		darkTheme() ? SettingMenuSkin::dark() : SettingMenuSkin::light();

	const float dpiScale = static_cast<float>(m_dpi) / 96.0f;
	float marginX = 0.0f, marginTop = 0.0f, marginBottom = 0.0f;
	for (const Shadow& shadow : m_skin.shadows)
	{
		const float reach = ShadowReach(shadow, dpiScale);
		const float offset = shadow.offsetY * dpiScale;
		marginX = (std::max)(marginX, reach);
		marginTop = (std::max)(marginTop, reach - offset);
		marginBottom = (std::max)(marginBottom, reach + offset);
	}
	const int left = static_cast<int>(std::ceil(marginX));
	const int top = static_cast<int>(std::ceil(marginTop));
	const int panelWidth = scale(m_skin.panelWidth);
	const int panelHeight = 2 * scale(m_skin.paddingY) + kItemCount * scale(m_skin.itemHeight) +
		2 * scale(m_skin.separatorGap) + static_cast<int>(chrome(1.0f));
	m_panel = { left, top, left + panelWidth, top + panelHeight };
	m_width = panelWidth + 2 * left;
	m_height = panelHeight + top + static_cast<int>(std::ceil(marginBottom));

	const int gap = scale(m_skin.anchorGap);
	const int centerX = anchor.left + (anchor.right - anchor.left) / 2;
	const int x = std::clamp(centerX - panelWidth / 2, static_cast<int>(work.left),
		std::max(static_cast<int>(work.left), static_cast<int>(work.right) - panelWidth));
	int y = anchor.top - gap - panelHeight;
	if (y < work.top) y = anchor.bottom + gap;
	y = std::clamp(y, static_cast<int>(work.top),
		std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - panelHeight));

	m_hover = -1;
	m_pressed = -1;
	m_switchFrom = m_switchTo = sConfig->getIsFloatingToolbarOpen() ? 1.0f : 0.0f;
	const POINT position{ x - m_panel.left, y - m_panel.top };
	render(&position);
	::SetWindowPos(m_window, HWND_TOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
	::SetCapture(m_window);
	s_hookMenu = this;
	m_keyboardHook = ::SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, g_hInst, 0);
}

void CSettingMenu::hide()
{
	if (m_hiding) return;
	m_hiding = true;
	if (m_window) ::ShowWindow(m_window, SW_HIDE);
	if (m_window && ::GetCapture() == m_window) ::ReleaseCapture();
	if (m_keyboardHook) { ::UnhookWindowsHookEx(m_keyboardHook); m_keyboardHook = nullptr; }
	if (s_hookMenu == this) s_hookMenu = nullptr;
	if (m_window && m_animating) ::KillTimer(m_window, kAnimationTimer);
	m_animating = false;
	m_hover = -1;
	m_pressed = -1;
	m_hiding = false;
}

LRESULT CALLBACK CSettingMenu::KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code >= 0 && s_hookMenu && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
	{
		const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
		if (key->vkCode == VK_ESCAPE) { s_hookMenu->hide(); return 1; }
		if (key->vkCode == VK_LWIN || key->vkCode == VK_RWIN ||
			(key->vkCode == VK_TAB && (::GetAsyncKeyState(VK_MENU) & 0x8000)))
			s_hookMenu->hide();
	}
	return ::CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK CSettingMenu::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_NCCREATE)
	{
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		auto* menu = static_cast<CSettingMenu*>(create->lpCreateParams);
		menu->m_window = window;
		::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(menu));
	}
	auto* self = reinterpret_cast<CSettingMenu*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
	return self ? self->handleMessage(message, wParam, lParam) :
		::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CSettingMenu::handleMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_ERASEBKGND: return 1;
	case WM_PAINT:
		::ValidateRect(m_window, nullptr);
		render();
		return 0;
	case WM_TIMER:
		if (wParam == kAnimationTimer) render();
		return 0;
	case WM_MOUSEMOVE:
	{
		const int hover = hitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		if (hover != m_hover) { m_hover = hover; render(); }
		return 0;
	}
	case WM_LBUTTONDOWN:
		m_pressed = hitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		if (m_pressed >= 0) render();
		return 0;
	case WM_LBUTTONUP:
	{
		const int index = hitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		m_pressed = -1;
		if (index < 0) hide();
		else select(index);
		return 0;
	}
	case WM_RBUTTONUP:
		if (hitTest({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }) < 0) hide();
		return 0;
	case WM_CAPTURECHANGED:
		if (!m_hiding) hide();
		return 0;
	case WM_DESTROY:
		m_window = nullptr;
		return 0;
	}
	return ::DefWindowProcW(m_window, message, wParam, lParam);
}

void CSettingMenu::select(int index)
{
	if (!m_owner) { hide(); return; }
	if (index == 0)
	{
		const bool enabled = !sConfig->getIsFloatingToolbarOpen();
		sConfig->setIsFloatingToolbarOpen(enabled);
		m_owner->refreshFloatingToolbarPreference();
		const auto now = Clock::now();
		m_switchFrom = switchProgress(now);
		m_switchTo = enabled ? 1.0f : 0.0f;
		m_switchStart = now;
		render();
		if (::GetCapture() != m_window) ::SetCapture(m_window);
		return;
	}
	hide();
	switch (index)
	{
	case 2:
		m_owner->hideCandidateDlg(TRUE);
		Coreapi::showSettingMenu();
		break;
	case 3: // 关于：打开设置窗口并跳转到“关于”页
		m_owner->hideCandidateDlg(TRUE);
		Coreapi::showAboutPage();
		break;
	case 4: Coreapi::sendFeedback(); break;
	default: break; // “定制”仍是原来的占位。
	}
}

void CSettingMenu::render(const POINT* position)
{
	if (!m_window || m_width <= 0 || m_height <= 0) return;
	Canvas canvas(m_width, m_height);
	if (!canvas.valid()) return;
	Surface surface = canvas.surface();
	const SettingMenuSkin& skin = m_skin;
	const float dpiScale = static_cast<float>(m_dpi) / 96.0f;
	const float hairline = chrome(1.0f);
	const RectF panel = ToRectF(m_panel);
	const float radius = static_cast<float>(scale(skin.panelRadius));
	for (const Shadow& shadow : skin.shadows)
		DrawShadow(surface, panel, radius, shadow, dpiScale);
	FillRoundRect(surface, panel, radius, skin.panel);
	StrokeRoundRect(surface, panel, radius, chrome(skin.borderWidth), skin.border);

	const float itemRadius = static_cast<float>(scale(skin.itemRadius));
	if (m_hover >= 0)
		FillRoundRect(surface, ToRectF(itemRect(m_hover)), itemRadius,
			m_pressed == m_hover ? skin.pressedFill : skin.hoverFill);
	const float separatorY = static_cast<float>(itemRect(kFooterIndex - 1).bottom +
		scale(skin.separatorGap));
	FillRoundRect(surface, { panel.left + hairline, separatorY, panel.right - hairline,
		separatorY + hairline }, 0.0f, skin.separator);

	const int iconSize = scale(skin.iconSize);
	for (int i = 0; i < kItemCount; ++i)
	{
		const RECT item = itemRect(i);
		const float iconTop = static_cast<float>(item.top + (item.bottom - item.top - iconSize) / 2);
		StrokeSvgIcon(surface, kIcons[i].paths, kIcons[i].count,
			static_cast<float>(item.left + scale(skin.iconLeft)), iconTop,
			static_cast<float>(iconSize), 24.0f, skin.iconStroke * dpiScale, skin.icon);
	}

	const auto now = Clock::now();
	const float progress = switchProgress(now);
	const RECT first = itemRect(0);
	const float switchWidth = static_cast<float>(scale(skin.switchWidth));
	const float switchHeight = static_cast<float>(scale(skin.switchHeight));
	const float switchRight = static_cast<float>(first.right - scale(skin.switchRight));
	const float switchTop = static_cast<float>(first.top) +
		std::round((static_cast<float>(first.bottom - first.top) - switchHeight) * 0.5f);
	const RectF track{ switchRight - switchWidth, switchTop, switchRight, switchTop + switchHeight };
	FillRoundRect(surface, track, switchHeight * 0.5f, skin.switchOff);
	FillRoundRect(surface, track, switchHeight * 0.5f, Fade(skin.switchOn, progress));
	const float inset = static_cast<float>(scale(skin.switchInset));
	const float knob = switchHeight - 2.0f * inset;
	const float knobLeft = track.left + inset + (switchWidth - 2.0f * inset - knob) * progress;
	const RectF knobRect{ knobLeft, track.top + inset, knobLeft + knob, track.top + inset + knob };
	DrawShadow(surface, knobRect, knob * 0.5f, skin.knobShadow, dpiScale);
	FillRoundRect(surface, knobRect, knob * 0.5f, skin.switchKnob);

	canvas.beginText();
	ScopedFont font(skin.fontName, scale(skin.fontSize), skin.fontWeight);
	HGDIOBJ oldFont = ::SelectObject(canvas.dc(), font.get());
	::SetTextColor(canvas.dc(), ToColorRef(skin.text));
	for (int i = 0; i < kItemCount; ++i)
	{
		const RECT item = itemRect(i);
		RECT text{ item.left + scale(skin.textLeft), item.top,
			i == 0 ? static_cast<LONG>(track.left) - scale(6) : item.right - scale(8), item.bottom };
		::DrawTextW(canvas.dc(), kTitles[i], -1, &text,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	}
	::SelectObject(canvas.dc(), oldFont);
	canvas.endText();
	canvas.present(m_window, position);

	const bool moving = progress != m_switchTo;
	if (moving && !m_animating)
		::SetTimer(m_window, kAnimationTimer, USER_TIMER_MINIMUM, nullptr);
	else if (!moving && m_animating)
		::KillTimer(m_window, kAnimationTimer);
	m_animating = moving;
}

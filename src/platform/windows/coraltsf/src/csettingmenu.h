#pragma once

#include <windows.h>

#include "soft_canvas.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

class CTextService;

// 快捷菜单的全部视觉参数。尺寸为 96 DPI 下的逻辑像素，
// 颜色为直通 0xAARRGGBB。面板色必须保持不透明：文字以
// ClearType 叠加在其上渲染。
struct SettingMenuSkin
{
	std::uint32_t panel = 0xFFFFFFFF;
	std::uint32_t border = 0x1A000000;
	std::uint32_t text = 0xFF1D1D1F;
	std::uint32_t icon = 0xFF3A3A3C;
	std::uint32_t hoverFill = 0x0B000000;
	std::uint32_t pressedFill = 0x16000000;
	std::uint32_t separator = 0x12000000;
	std::uint32_t switchOn = 0xFF007AFF;
	std::uint32_t switchOff = 0xFFB0B0B0;
	std::uint32_t switchKnob = 0xFFFFFFFF;
	coral::win32::soft::Shadow knobShadow{ 0.5f, 2.0f, 0.0f, 0x4D000000 };
	coral::win32::soft::Shadow shadows[2] = {
		{ 0.0f, 2.0f, 0.0f, 0x12000000 },
		{ 4.0f, 16.0f, -2.0f, 0x1F000000 } };

	int panelWidth = 200;
	int panelRadius = 8;
	float borderWidth = 1.0f;
	int paddingX = 4;
	int paddingY = 4;
	int itemHeight = 32;
	int itemRadius = 5;
	int iconLeft = 10;
	int iconSize = 16;
	float iconStroke = 1.35f;
	int textLeft = 36;
	int fontSize = 13;
	int fontWeight = FW_NORMAL;
	std::wstring fontName = L"Microsoft YaHei UI";
	int separatorGap = 4;
	int switchWidth = 28;
	int switchHeight = 16;
	int switchInset = 2;
	int switchRight = 8;
	int switchAnimationMs = 140;
	int anchorGap = 4;

	static SettingMenuSkin light();
	static SettingMenuSkin dark();
};

// 从悬浮工具栏打开的五行快捷菜单，以逐像素 alpha
// 分层窗口绘制，带抗锯齿边框。
class CSettingMenu final
{
public:
	explicit CSettingMenu(CTextService* owner) : m_owner(owner) {}
	~CSettingMenu();
	CSettingMenu(const CSettingMenu&) = delete;
	CSettingMenu& operator=(const CSettingMenu&) = delete;

	void toggle(const RECT& anchor);
	void hide();
	// 固定皮肤；std::nullopt 恢复跟随配置的浅色/深色主题。
	void setSkin(std::optional<SettingMenuSkin> skin) { m_skinOverride = std::move(skin); }

private:
	using Clock = std::chrono::steady_clock;
	static constexpr UINT_PTR kAnimationTimer = 0xC0A2;

	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);
	LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
	void render(const POINT* position = nullptr);
	void select(int index);
	int hitTest(POINT point) const;
	RECT itemRect(int index) const;
	int scale(int value) const;
	float chrome(float value) const;
	bool darkTheme() const;
	float switchProgress(Clock::time_point now) const;

	CTextService* m_owner = nullptr;
	HWND m_window = nullptr;
	HHOOK m_keyboardHook = nullptr;
	SettingMenuSkin m_skin;
	std::optional<SettingMenuSkin> m_skinOverride;
	RECT m_panel{};
	int m_dpi = 96;
	int m_width = 0;
	int m_height = 0;
	int m_hover = -1;
	int m_pressed = -1;
	float m_switchFrom = 0.0f;
	float m_switchTo = 0.0f;
	Clock::time_point m_switchStart{};
	bool m_animating = false;
	bool m_hiding = false;
	static CSettingMenu* s_hookMenu;
};

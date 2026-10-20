#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGSWINDOW_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGSWINDOW_H_INCLUDED

#include "pch.h"

#include <cstdint>

/*
 * 设置窗口的全部外观参数。颜色为非预乘 0xAARRGGBB，尺寸为 96 DPI 逻辑像素。
 * window / surface 必须不透明：文字以 ClearType 绘制在其上。
 */
struct SettingsSkin
{
	std::uint32_t window = 0xFFFFF9F6;
	std::uint32_t sidebar = 0xFFFFF3DD;
	std::uint32_t surface = 0xFFFFFFFF;
	std::uint32_t hover = 0xFFFFF3EB;
	std::uint32_t pressed = 0xFFFFE6D8;
	std::uint32_t line = 0x1A704B3A;
	std::uint32_t lineStrong = 0x33704B3A;
	std::uint32_t lineHover = 0x4D704B3A;
	std::uint32_t text = 0xFF30241F;
	std::uint32_t sub = 0xFF776259;
	std::uint32_t muted = 0xFF927C71;
	std::uint32_t accent = 0xFFE8B18A;
	std::uint32_t accentHover = 0xFFDDA17A;
	std::uint32_t accentPressed = 0xFFD3936C;
	std::uint32_t onAccent = 0xFF76533F;
	std::uint32_t choiceMark = 0xFFFFFFFF;
	std::uint32_t link = 0xFF0F6CBD;
	std::uint32_t linkHover = 0xFF115EA3;
	std::uint32_t linkPressed = 0xFF0C5A9E;
	std::uint32_t danger = 0xFFC42B1C;
	std::uint32_t navHover = 0x9EFFFFFF;
	std::uint32_t navActive = 0xFFFFFFFF;
	std::uint32_t navShadow = 0x0A111827;
	std::uint32_t cardShadow = 0x0A111827;
	std::uint32_t switchBorder = 0xFFB8A99B;
	std::uint32_t switchKnob = 0xFFAAA093;
	std::uint32_t sliderTrack = 0xFFEAD9CE;
	std::uint32_t kbdFill = 0xFFFFF8F3;
	std::uint32_t scrollThumb = 0xFFD8BFB0;
	std::uint32_t scrollThumbHover = 0xFFC4A18D;
	std::uint32_t heroFrom = 0xFFFFFFFF;
	std::uint32_t heroTo = 0xFFFFEBDD;
	bool darkCaption = false;

	std::wstring fontName = L"Microsoft YaHei UI";
	int workspacePadding = 18;
	int sidebarWidth = 228;
	int navHeight = 42;
	int navGap = 4;
	int navRadius = 8;
	int navIconSize = 19;
	float navIconStroke = 1.5f;
	int contentMaxWidth = 690;
	int scrollGutter = 18;
	int scrollbarWidth = 6;
	int cardRadius = 10;
	int heroRadius = 14;
	int rowMinHeight = 58;
	int rowPaddingLeft = 16;
	int rowPaddingRight = 14;
	int rowPaddingY = 11;
	int stackPaddingY = 14;
	int controlGap = 16;
	int controlHeight = 34;
	int controlRadius = 6;
	int switchWidth = 40;
	int switchHeight = 22;
	int switchAnimationMs = 140;
	int choiceSize = 18;

	static SettingsSkin light();
	static SettingsSkin dark();
};

/*
 * Win32 设置主窗口：左侧自绘导航（常规/按键/外观/词库/常用语和剪贴板/高级/语音输入/关于），右侧为可滚动的卡片式页面。
 * 开关、单选、复选、按钮、滑块为自绘控件，但仍响应 BM_GETCHECK/BM_SETCHECK/TBM_* 并发送
 * BN_CLICKED/WM_HSCROLL，下拉框为重绘外观的原生 COMBOBOX。
 * 所有配置项修改即时写回 sConfig（共享内存+INI，TSF 侧实时生效），无确认按钮。
 * 单实例：showInstance() 已存在则前置激活。
 */
class SettingsWindow
{
public:
	static void showInstance();
	// 打开设置窗口并定位到“关于”页（右键菜单“关于”入口）。
	static void showAboutPage();
	static void bringToTopAndActivate();   // 创建或激活设置窗口
	static void closeInstance();
	// 更换皮肤；窗口已打开时会按新皮肤重建页面。
	static void setSkin(const SettingsSkin& skin);
	static const SettingsSkin& skin() { return s_skin; }

	static bool isInstanceAlive() { return s_hwnd != nullptr; }
	// 常用语/剪贴板数据变化后刷新对应列表；窗口未打开时忽略。
	static void refreshPhraseList();
	static void refreshClipboardList();

private:
	SettingsWindow() = default;

	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK viewportProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK pageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static void registerClass();
	static void createFonts();
	static void destroyFonts();
	static void createControls();      // 主窗口 WM_CREATE 时调用
	static void destroyPages();
	static void layoutPages();
	static void createRoutinePage();
	static void createKeyboardPage();
	static void createAppearancePage();
	static void createLexiconPage();
	static void createPhrasePage();
	static void createSeniorPage();
    static void createSpeechPage();
	static void createAboutPage();
	static void switchPage(int index); // 0..PAGE_COUNT-1（页面索引范围）
	static void scrollPage(int index, int offset);
	static void paintSidebar(HDC dc);
	static int navHitTest(POINT point);
	static RECT navRect(int index);
	static void loadAllPages();        // 从 sConfig 读回全部控件状态
	static void loadRoutinePage();
	static void loadKeyboardPage();
	static void loadAppearancePage();
	static void loadLexiconPage();
	static void loadPhrasePage();
	static void loadSeniorPage();
    static void loadSpeechPage();
	static void loadAboutPage();
	static void onCommand(WPARAM wParam, LPARAM lParam);
	static void onScroll(WPARAM wParam, LPARAM lParam);
	// 候选窗皮肤：写回配置并刷新图块、色块与预览。
	static void applyCandidateSkin();
	static void refreshSkinControls();
	static void switchSkinTab(int tab);
	static void rebuildPage(int index);
	static void switchPhraseTab(int tab);
	static void showListMenu(int listId, int row, POINT screen);
	static void clearClipboardHistory();
	static void selectSkinTile(int id);
	static void editSkinColor(int id);
	static void pickCandidateImage();
	static void refreshCandidateFonts();
	static void addCandidateFont();
	static void importCandidateImage(const std::wstring& source);
	static void exportLexicon();
	static void importLexicon();
	static void clear_lexicon();
	static std::wstring getVersionString();
	static std::vector<std::pair<std::string, std::wstring>> enumDoublepinyinSchemes(); // {schemeName, 显示名}

private:
	static inline HWND s_hwnd = nullptr;
	static inline SettingsSkin s_skin{};
	static constexpr int kPageCount = 8;
	static inline HWND s_viewports[kPageCount] = {};
	static inline HWND s_pages[kPageCount] = {};   // 控件父窗口（可滚动内容），GetDlgItem 以此为准
	static inline int s_scroll[kPageCount] = {};
	static inline int s_currentPage = 0;
	static inline int s_navHover = -1;
	static inline std::vector<std::string> s_schemeNames; // 双拼方案名（与 combo 索引对应）
};

#endif // SETTINGSWINDOW_H

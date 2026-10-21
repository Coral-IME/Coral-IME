#include "coral/product/product_identity.h"
#include "coral/composition/assist_hints.h"
#include "coral/translation/languages.h"
#include "coral/translation/shortcut.h"
#include "coral/speech/shortcut.h"
#include "speech_runtime.h"
#include "microphone_capture.h"
#include "pch.h"
#include "settingswindow.h"
#include "settings_dpi.h"
#include "candidatefontlist.h"
#include "settingsenum.h"
#include "fuzzypinyindialog.h"
#include "lexiconimportdialog.h"
#include "confirmdialog.h"
#include "addphrasedialog.h"
#include "skincropdialog.h"
#include "update_coordinator.h"
#include "utils/helper.h"
#include "utf8conv.h"
#include "layout_metadata.h"
#include "package_version.h"
#include "pinyin.h"
#include "core_operations.h"
#include "soft_canvas.h"
#include "candidate_view.h"
#include "clipboardhistory.h"

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <chrono>
#include <cmath>
#include <initializer_list>
#include <map>

using namespace coral::runtime;
using namespace coral::win32::soft;

namespace
{
	using coral::settings_ui::DpiCanvas;
	using coral::settings_ui::DpiScale;
	using coral::settings_ui::DpiContext;
	DpiScale g_dpi;

	void logical_client_rect(HWND window, RECT* rect)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		::GetClientRect(window, rect);
		*rect = g_dpi.logical(*rect);
	}

	BOOL position_window(HWND window, HWND after, int x, int y, int width, int height, UINT flags)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		return ::SetWindowPos(window, after, g_dpi.pixels(x), g_dpi.pixels(y),
			g_dpi.pixels(width), g_dpi.pixels(height), flags);
	}

	BOOL move_window(HWND window, int x, int y, int width, int height, BOOL repaint)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		return ::MoveWindow(window, g_dpi.pixels(x), g_dpi.pixels(y),
			g_dpi.pixels(width), g_dpi.pixels(height), repaint);
	}

	HWND create_scaled_window(DWORD extended, LPCWSTR cls, LPCWSTR caption, DWORD style,
		int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID data)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		return ::CreateWindowExW(extended, cls, caption, style, g_dpi.pixels(x), g_dpi.pixels(y),
			g_dpi.pixels(width), g_dpi.pixels(height), parent, menu, instance, data);
	}

	void invalidate_logical_rect(HWND window, const RECT* rect, BOOL erase)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		if (!rect) { ::InvalidateRect(window, nullptr, erase); return; }
		const RECT pixels = g_dpi.pixels(*rect);
		::InvalidateRect(window, &pixels, erase);
	}

	void screen_to_logical_client(HWND window, POINT* point)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		::ScreenToClient(window, point);
		*point = g_dpi.logical(*point);
	}

	void logical_client_to_screen(HWND window, POINT* point)
	{
		DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		point->x = g_dpi.pixels(point->x);
		point->y = g_dpi.pixels(point->y);
		::ClientToScreen(window, point);
	}

	// 主窗口
	constexpr int WINDOW_W = 1000;
	constexpr int WINDOW_H = 700;
	constexpr int kCandidateSizeSteps[] = { 12, 13, 14, 16, 18, 21, 24 };
	constexpr int kWheelStep = 64;

	// 控件 ID
	enum CtrlId : int {
		// 常规页
		ID_R_QUAN = 0x1100, ID_R_SHUANG, ID_R_SCHEME_LABEL, ID_R_SCHEME,
		ID_R_DEFZH, ID_R_DEFEN,
		ID_R_JIAN, ID_R_FAN,
		ID_R_BANJIAO, ID_R_QUANJIAO,
		ID_R_ENPUNCT, ID_R_CORRECT, ID_R_FUZZY,
		// 按键页
		ID_K_CS = 0x1200, ID_K_SHIFT, ID_K_CTRL,
		ID_K_WIDTHON, ID_K_WIDTHOFF,
		ID_K_PUNCTON, ID_K_PUNCTOFF,
		ID_K_SIMTRA, ID_K_SIMTRAKEY,
		ID_K_FLIPPAGE, ID_K_FLIPBRACE, ID_K_FLIPCOMMA,
		ID_K_SEM, ID_K_WORD2CHAR, ID_K_FLIPMINUS,
		// 外观页
		ID_A_FONT = 0x1300, ID_A_SIZE, ID_A_THEME, ID_A_PREVIEW, ID_A_OPACITY, ID_A_ADDFONT, ID_A_IMAGEOPACITY,
		// 主题图块：纯色 / 渐变（末位自定义）/ 图片（末位自定义）
		ID_A_SOLID = 0x1310,
		ID_A_GRADIENT = 0x1320,
		ID_A_IMAGE = 0x1330,
		ID_A_COLOR_TEXT = 0x1340, ID_A_COLOR_SELTEXT, ID_A_COLOR_BG, ID_A_COLOR_SELBG,
		ID_A_COLOR_FROM, ID_A_COLOR_TO,
		// 主题 tab：纯色 / 渐变 / 图片
		ID_A_TAB = 0x1350, ID_A_TAB_LAST = ID_A_TAB + 2,
		// 词库页
		ID_L_EXPORT = 0x1400, ID_L_IMPORT, ID_L_CLEAR,
		ID_L_AUTOSYMBOL, ID_L_DIGITSYMBOL, ID_L_LETTERSYMBOL,
		// 常用语和剪贴板页
		ID_P_TAB = 0x1600, ID_P_TAB_LAST = ID_P_TAB + 1,
		ID_P_SEGMENT, ID_P_ILLUSTRATION, ID_P_ADD, ID_P_PHRASES,
		ID_P_CLIPSWITCH, ID_P_CLIPS, ID_P_CLEARCLIPS,
		// 高级页
		ID_S_UMODE = 0x1500, ID_S_VMODE, ID_S_UHELP, ID_S_VHELP,
        ID_S_TRANSLATION, ID_S_KEYID, ID_S_SECRET, ID_S_SAVEKEY, ID_S_TARGET, ID_S_TRANSLATION_HELP, ID_S_PREDICTION, ID_S_SHORTCUT,
		// 关于页
        ID_V_ENABLED = 0x1800, ID_V_KEYID, ID_V_SECRET, ID_V_SAVEKEY, ID_V_SHORTCUT, ID_V_MIC, ID_V_PUSHTOTALK,
		ID_AB_CHECKUPDATE = 0x1700, ID_AB_EULA, ID_AB_PRIVACY, ID_AB_AUTOCHECK,
	};

	constexpr int PAGE_COUNT = 8;
	const wchar_t* NAV_TITLES[PAGE_COUNT] = {
		L"常规", L"按键", L"外观", L"词库和自学习", L"常用语和剪贴板", L"高级", L"语音输入", L"关于"
	};
	const wchar_t* PAGE_DESCRIPTIONS[PAGE_COUNT] = {
		L"设置默认输入方式、字符形式与标点行为，只影响程序首次打开时的输入法状态",
		L"调整切换、翻页和候选词快捷键。",
		L"调整候选窗口的大小、字体与皮肤。",
		L"管理个人词库与输入习惯。",
		L"用简短的输入码输入常用内容，或从剪贴板记录中快速粘贴。",
		L"配置可选的输入扩展模式。",
        L"说完再转成文字；开启“按下发言”后可按住 Ctrl + Win 直接说话。",
		L"版本信息、更新与相关协议。"
	};

	// 24 单位线性图标（UI/设置页面.html），圆与圆角矩形写成弧线路径。
	constexpr const char* kNavGeneral[] = { "M4 7h10M18 7h2M4 17h2M10 17h10M14 4v6M6 14v6" };
	constexpr const char* kNavKeys[] = {
		"M5 5h14a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2z",
		"M7 9h.01M11 9h.01M15 9h.01M7 13h.01M11 13h6M7 16h10" };
	constexpr const char* kNavAppearance[] = {
		"M12 3a9 9 0 1 0 0 18h1.3a1.7 1.7 0 0 0 0-3.4h-.7a1.6 1.6 0 0 1 0-3.2H15a6 6 0 0 0 0-12h-3Z",
		"M7.5 10.5h.01M9.5 7.5h.01M13 6.5h.01" };
	constexpr const char* kNavDictionary[] = {
		"M4 5.5A2.5 2.5 0 0 1 6.5 3H11v16H6.5A2.5 2.5 0 0 0 4 21.5v-16Z"
		"M20 5.5A2.5 2.5 0 0 0 17.5 3H13v16h4.5a2.5 2.5 0 0 1 2.5 2.5v-16Z" };
	constexpr const char* kNavPhrase[] = {
		"M9 3.5h6a1 1 0 0 1 1 1v1.5a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1V4.5a1 1 0 0 1 1-1Z",
		"M16 5h1.5A1.5 1.5 0 0 1 19 6.5v13a1.5 1.5 0 0 1-1.5 1.5h-11A1.5 1.5 0 0 1 5 19.5v-13A1.5 1.5 0 0 1 6.5 5H8",
		"M9 12h6M9 16h4" };
	constexpr const char* kNavAdvanced[] = {
		"M9 12a3 3 0 1 0 6 0a3 3 0 1 0-6 0",
		"M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6 7 7M17 17l1.4 1.4M18.4 5.6 17 7M7 17l-1.4 1.4" };
	constexpr const char* kNavAbout[] = {
		"M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0", "M12 11v6M12 7.5h.01" };
	struct NavIcon { const char* const* paths; int count; };
    constexpr const char* kNavSpeech[] = {
        "M9 6a3 3 0 0 1 6 0v6a3 3 0 0 1-6 0V6Z",
        "M6 11v1a6 6 0 0 0 12 0v-1M12 18v3M9 21h6" };
	constexpr NavIcon kNavIcons[PAGE_COUNT] = {
		{ kNavGeneral, 1 }, { kNavKeys, 2 }, { kNavAppearance, 2 },
		{ kNavDictionary, 1 }, { kNavPhrase, 3 }, { kNavAdvanced, 2 }, { kNavSpeech, 2 }, { kNavAbout, 2 } };

	constexpr wchar_t kWindowClass[] = L"CoralPinyinSettingsWnd";
	constexpr wchar_t kViewportClass[] = L"CoralPinyinSettingsViewport";
	constexpr wchar_t kPageClass[] = L"CoralPinyinSettingsPage";
	constexpr wchar_t kControlClass[] = L"CoralPinyinSettingsControl";
	constexpr wchar_t kSliderClass[] = L"CoralPinyinSettingsSlider";
	constexpr wchar_t kPreviewClass[] = L"CoralPinyinSettingsSkinPreview";
	constexpr wchar_t kSegmentClass[] = L"CoralPinyinSettingsSegment";
	constexpr wchar_t kIllustrationClass[] = L"CoralPinyinSettingsIllustration";
	constexpr wchar_t kListClass[] = L"CoralPinyinSettingsList";

	enum FontId {
		F_PAGE_TITLE, F_SIDEBAR_TITLE, F_NAV, F_NAV_ACTIVE, F_LABEL,
		F_CONTROL, F_SECTION, F_DESC, F_SMALL, F_LINK, F_COUNT
	};
	HFONT g_fonts[F_COUNT] = {};
	HBRUSH g_surfaceBrush = nullptr;
	std::wstring g_version;
	std::vector<coral::settings::CandidateFont> g_candidateFonts;

	using SkinColor = std::uint32_t SettingsSkin::*;
	struct PaintText
	{
		RECT rect;
		std::wstring text;
		int font;
		SkinColor color;
		UINT format;
	};
	struct PageLayout
	{
		struct Card { RECT rect; bool hero; };
		std::vector<Card> cards;
		std::vector<RECT> lines;
		std::vector<PaintText> texts;
		RECT logo{};
		int height = 0;
		std::unique_ptr<DpiCanvas> cache;
	};
	PageLayout g_layouts[PAGE_COUNT];

	struct ScrollDrag { int page = -1; int grab = 0; bool hover = false; };
	ScrollDrag g_scrollDrag;

	const SettingsSkin& Skin() { return SettingsWindow::skin(); }

	std::uint32_t Fade(std::uint32_t color, float factor)
	{
		const auto alpha = static_cast<std::uint32_t>(((color >> 24) & 0xFF) *
			(std::clamp)(factor, 0.0f, 1.0f) + 0.5f);
		return (alpha << 24) | (color & 0x00FFFFFFu);
	}

	// a 必须不透明。
	std::uint32_t Mix(std::uint32_t a, std::uint32_t b, float t)
	{
		return Over(Fade(b, t), a);
	}

	float EaseOut(float t)
	{
		const float inverse = 1.0f - (std::clamp)(t, 0.0f, 1.0f);
		return 1.0f - inverse * inverse * inverse;
	}

	SIZE MeasureText(HDC dc, int font, const std::wstring& text)
	{
		HGDIOBJ old = SelectObject(dc, g_fonts[font]);
		SIZE size{};
		GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
		SelectObject(dc, old);
		size.cx = static_cast<LONG>(std::ceil(size.cx / g_dpi.factor()));
		size.cy = static_cast<LONG>(std::ceil(size.cy / g_dpi.factor()));
		return size;
	}

	void DrawTextIn(HDC dc, const std::wstring& text, RECT rect, int font, std::uint32_t color, UINT format)
	{
		HGDIOBJ old = SelectObject(dc, g_fonts[font]);
		SetTextColor(dc, ToColorRef(color));
		rect = g_dpi.pixels(rect);
		DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
			format | DT_SINGLELINE | DT_NOPREFIX);
		SelectObject(dc, old);
	}

	// 横向渐变的圆角矩形（逐列绘制）。
	void FillGradientRoundRect(Surface& surface, const RectF& rect, float radius,
		std::uint32_t from, std::uint32_t to)
	{
		const float width = (std::max)(1.0f, rect.right - rect.left);
		for (float x = std::floor(rect.left); x < rect.right; x += 1.0f)
		{
			const std::uint32_t color = Mix(from, to, (x + 0.5f - rect.left) / width);
			surface.paint({ x, rect.top, x + 1.0f, rect.bottom }, color, [&](float px, float py) {
				return Coverage(RoundRectDistance(px, py, rect, radius));
			});
		}
	}

	void BuildPageCache(int index, HWND page)
	{
		PageLayout& layout = g_layouts[index];
		RECT client{};
		logical_client_rect(page, &client);
		layout.cache = std::make_unique<DpiCanvas>(client.right, client.bottom, g_dpi);
		DpiCanvas& canvas = *layout.cache;
		if (!canvas.valid()) return;
		const SettingsSkin& s = Skin();
		Surface surface = canvas.surface();
		surface.fill(s.window);
		for (const PageLayout::Card& card : layout.cards)
		{
			const RectF rect = ToRectF(card.rect);
			const float radius = static_cast<float>(card.hero ? s.heroRadius : s.cardRadius);
			DrawShadow(surface, rect, radius, { 1.0f, 2.0f, 0.0f, s.cardShadow }, 1.0f);
			if (card.hero) FillGradientRoundRect(surface, rect, radius, s.heroFrom, s.heroTo);
			else FillRoundRect(surface, rect, radius, s.surface);
			StrokeRoundRect(surface, rect, radius, 1.0f, s.line);
		}
		for (const RECT& line : layout.lines)
			FillRoundRect(surface, ToRectF(line), 0.0f, s.line);

		canvas.beginText();
		HDC dc = canvas.dc();
		for (const PaintText& text : layout.texts)
			DrawTextIn(dc, text.text, text.rect, text.font, s.*text.color, text.format);
		if (!IsRectEmpty(&layout.logo))
		{
			RECT rect = g_dpi.pixels(layout.logo);
			HICON icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
				MAKEINTRESOURCEW(IDI_PINYIN), IMAGE_ICON, rect.right - rect.left,
				rect.bottom - rect.top, 0));
			if (icon)
			{
				DrawIconEx(dc, rect.left, rect.top, icon, rect.right - rect.left,
					rect.bottom - rect.top, 0, nullptr, DI_NORMAL);
				DestroyIcon(icon);
			}
		}
		canvas.endText();
	}

	DpiCanvas* PageCache(HWND page)
	{
		const int index = static_cast<int>(GetWindowLongPtrW(page, GWLP_USERDATA));
		if (index < 0 || index >= PAGE_COUNT) return nullptr;
		if (!g_layouts[index].cache) BuildPageCache(index, page);
		return g_layouts[index].cache.get();
	}

	// 子控件先铺上页面缓存中对应位置的背景，圆角与卡片/渐变无缝衔接。
	void PaintBackdrop(HWND control, DpiCanvas& canvas)
	{
		HWND page = GetParent(control);
		DpiCanvas* cache = PageCache(page);
		if (!cache || !cache->valid())
		{
			Surface surface = canvas.surface();
			surface.fill(Skin().surface);
			return;
		}
		POINT origin{ 0, 0 };
		MapWindowPoints(control, page, &origin, 1);
		BitBlt(canvas.dc(), 0, 0, canvas.width(), canvas.height(), cache->dc(), origin.x, origin.y, SRCCOPY);
		GdiFlush();
	}

	// ---------------------------------------------------------------------
	// 候选窗皮肤：编辑中的配置、主题图块、调色板色块与实时预览
	// ---------------------------------------------------------------------
	using coral::win32::CandidateBackground;
	using coral::win32::CandidatePalette;
	using coral::win32::CandidateThemeConfig;

	CandidateThemeConfig g_candidateSkin;
	// 外观页当前打开的主题 tab，与 CandidateBackground 同序；-1 表示按配置初始化。
	int g_skinTab = -1;
	std::wstring g_droppedImage;
	// 拖入图片后以此通知码经 WM_COMMAND 转给主窗口。
	constexpr WORD kImageDropped = 0x7001;
	// 重建外观页会销毁发出通知的控件，因此投递到主窗口稍后执行。
	constexpr UINT kRebuildAppearance = WM_APP + 1;
	constexpr UINT kRebuildPhrase = WM_APP + 2;
	// 列表行右键：wParam = MAKEWPARAM(列表 ID, 行号)，lParam = 屏幕坐标。
	constexpr UINT kListContext = WM_APP + 3;
	constexpr int kTileColumns = 4;
	constexpr int kTileHeight = 44;
	constexpr int kTileChip = 22;
	constexpr int kWrapGap = 12;
	constexpr int kTabHeight = 40;
	constexpr int kTabGap = 28;
	constexpr int kPreviewHeight = 262;
	constexpr int kCustomImageTile = ID_A_IMAGE + coral::win32::kCandidateImagePresetCount;

	bool CandidateDark() { return DarkTheme(sConfig->getTheme()); }

	CandidateBackground TabKind() { return static_cast<CandidateBackground>((std::clamp)(g_skinTab, 0, 2)); }
	std::wstring InstallDir() { return hlp::getInstallPath().wstring(); }
	std::wstring UserDir() { return hlp::getLocalLowCoralPinyinPath().wstring(); }

	struct TileRef
	{
		CandidateBackground kind = CandidateBackground::Solid;
		int index = 0;
		bool custom = false;
	};

	bool TileOf(int id, TileRef& tile)
	{
		using namespace coral::win32;
		if (id >= ID_A_SOLID && id < ID_A_SOLID + kCandidateSolidPresetCount)
			tile = { CandidateBackground::Solid, id - ID_A_SOLID, false };
		else if (id >= ID_A_GRADIENT && id <= ID_A_GRADIENT + kCandidateGradientPresetCount)
			tile = { CandidateBackground::Gradient, id - ID_A_GRADIENT,
				id - ID_A_GRADIENT == kCandidateGradientPresetCount };
		else if (id >= ID_A_IMAGE && id <= kCustomImageTile)
			tile = { CandidateBackground::Image, id - ID_A_IMAGE, id == kCustomImageTile };
		else
			return false;
		return true;
	}

	bool TileSelected(const TileRef& tile)
	{
		const CandidateThemeConfig& c = g_candidateSkin;
		if (c.kind != tile.kind) return false;
		switch (tile.kind)
		{
		case CandidateBackground::Gradient: return c.gradient == tile.index;
		case CandidateBackground::Image: return c.image == tile.index;
		default: return c.solid == tile.index;
		}
	}

	// 假设选中该图块时的配置，用来绘制图块缩略图。
	CandidateThemeConfig TileConfig(const TileRef& tile)
	{
		CandidateThemeConfig config = g_candidateSkin;
		config.kind = tile.kind;
		config.customPalette = false;
		if (tile.kind == CandidateBackground::Gradient) config.gradient = tile.index;
		else if (tile.kind == CandidateBackground::Image) config.image = tile.index;
		else config.solid = tile.index;
		return config;
	}

	// 当前 tab 对应的配置：tab 与生效主题同类时就是生效配置，否则是该类中记住的那一项。
	CandidateThemeConfig TabConfig()
	{
		if (g_candidateSkin.kind == TabKind()) return g_candidateSkin;
		CandidateThemeConfig config = g_candidateSkin;
		config.kind = TabKind();
		config.customPalette = false;
		return config;
	}

	std::wstring TileCaption(const TileRef& tile)
	{
		if (tile.kind == CandidateBackground::Image && tile.custom)
			return g_candidateSkin.imageExtension.empty() ? L"上传图片" : L"自定义";
		if (tile.custom) return L"自定义";
		switch (tile.kind)
		{
		case CandidateBackground::Gradient: return coral::win32::CandidateGradientPresetAt(tile.index).name;
		case CandidateBackground::Image: return coral::win32::CandidateImagePresetAt(tile.index).name;
		default: return coral::win32::CandidateSolidPresetAt(tile.index).name;
		}
	}

	// 缩略图按路径缓存；LoadSkinImage 在文件被替换后会返回新对象。
	const std::vector<std::uint32_t>* TileImage(const std::wstring& path, int width, int height)
	{
		struct Entry
		{
			std::shared_ptr<const coral::win32::SkinImage> image;
			std::vector<std::uint32_t> pixels;
		};
		static std::map<std::wstring, Entry> cache;
		auto image = coral::win32::LoadSkinImage(path);
		if (!image) return nullptr;
		Entry& entry = cache[path];
		if (entry.image != image)
		{
			entry.image = image;
			entry.pixels = coral::win32::CoverSkinImage(*image, width, height);
		}
		return &entry.pixels;
	}

	void PaintTile(HWND hwnd, HDC target, bool hover)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		TileRef tile;
		TileOf(GetDlgCtrlID(hwnd), tile);
		const bool dark = CandidateDark();
		const CandidateThemeConfig config = TileConfig(tile);
		const CandidatePalette palette = config.themePalette(dark);
		const bool selected = TileSelected(tile);
		const float radius = 8.0f;
		const RectF card{ 0.5f, 0.5f, static_cast<float>(client.right) - 0.5f,
			static_cast<float>(client.bottom) - 0.5f };
		FillRoundRect(surface, card, radius, selected ? Mix(s.surface, s.accent, 0.07f) :
			hover ? s.hover : s.surface);
		StrokeRoundRect(surface, card, radius, selected ? 1.5f : 1.0f,
			selected ? s.accent : hover ? s.lineHover : s.lineStrong);

		const float chipTop = std::floor((client.bottom - kTileChip) / 2.0f);
		const RectF chip{ 12.0f, chipTop, 12.0f + kTileChip, chipTop + kTileChip };
		const float chipRadius = 5.0f;
		const bool emptyImage = tile.kind == CandidateBackground::Image && tile.custom &&
			config.imageExtension.empty();
		bool chipFilled = true;
		if (tile.kind == CandidateBackground::Gradient)
		{
			std::uint32_t from = 0, to = 0;
			config.gradientColors(dark, from, to);
			ShadeRoundRect(surface, chip, chipRadius, [&](int x, int y) {
				return Lerp(from, to, (static_cast<float>(x - chip.left) + static_cast<float>(y - chip.top)) /
					(2.0f * kTileChip));
			});
		}
		else if (tile.kind == CandidateBackground::Image && !emptyImage)
		{
			const std::vector<std::uint32_t>* pixels =
				TileImage(config.imagePath(InstallDir(), UserDir()), kTileChip, kTileChip);
			if (pixels)
				ShadeRoundRect(surface, chip, chipRadius, [&](int x, int y) {
					const int column = (std::clamp)(x - static_cast<int>(chip.left), 0, kTileChip - 1);
					const int row = (std::clamp)(y - static_cast<int>(chip.top), 0, kTileChip - 1);
					return (*pixels)[static_cast<size_t>(row) * kTileChip + column];
				});
			else
				FillRoundRect(surface, chip, chipRadius, palette.background);
		}
		else if (!emptyImage)
		{
			// 纯色：背景色上叠一块选中项颜色。
			FillRoundRect(surface, chip, chipRadius, palette.background);
			FillRoundRect(surface, { chip.left + 5.0f, chip.top + 5.0f, chip.right - 5.0f, chip.bottom - 5.0f },
				3.0f, palette.selectedBackground);
		}
		else
		{
			chipFilled = false;
			StrokeRoundRect(surface, chip, chipRadius, 1.0f, s.lineHover);
			const float cx = (chip.left + chip.right) / 2.0f;
			const float cy = (chip.top + chip.bottom) / 2.0f;
			const float horizontal[] = { cx - 5.0f, cy, cx + 5.0f, cy };
			const float vertical[] = { cx, cy - 5.0f, cx, cy + 5.0f };
			StrokePolyline(surface, horizontal, 2, 1.4f, s.sub);
			StrokePolyline(surface, vertical, 2, 1.4f, s.sub);
		}
		if (chipFilled) StrokeRoundRect(surface, chip, chipRadius, 1.0f, s.line);

		canvas.beginText();
		const RECT name{ static_cast<int>(chip.right) + 10, 0, client.right - 8, client.bottom };
		DrawTextIn(canvas.dc(), TileCaption(tile), name, F_CONTROL, selected ? s.accent : s.text,
			DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	// 主题 tab：选中项加粗并带短下划线，其余为灰色。
	void PaintTab(HWND hwnd, HDC target, bool hover)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		const SettingsSkin& s = Skin();
		const bool active = GetDlgCtrlID(hwnd) - ID_A_TAB == g_skinTab;
		if (active)
		{
			Surface surface = canvas.surface();
			const float center = client.right / 2.0f;
			FillRoundRect(surface, { center - 14.0f, client.bottom - 3.0f, center + 14.0f,
				static_cast<float>(client.bottom) }, 1.5f, s.text);
		}
		wchar_t caption[32] = {};
		GetWindowTextW(hwnd, caption, ARRAYSIZE(caption));
		canvas.beginText();
		DrawTextIn(canvas.dc(), caption, { 0, 0, client.right, client.bottom - 3 },
			active ? F_NAV_ACTIVE : F_NAV, active || hover ? s.text : s.sub, DT_CENTER | DT_VCENTER);
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	std::uint32_t SwatchColor(int id)
	{
		const bool dark = CandidateDark();
		const CandidateThemeConfig config = TabConfig();
		const CandidatePalette palette = config.effectivePalette(dark);
		std::uint32_t from = 0, to = 0;
		config.gradientColors(dark, from, to);
		switch (id)
		{
		case ID_A_COLOR_TEXT: return palette.text;
		case ID_A_COLOR_SELTEXT: return palette.selectedText;
		case ID_A_COLOR_BG: return palette.background;
		case ID_A_COLOR_SELBG: return palette.selectedBackground;
		case ID_A_COLOR_FROM: return from;
		case ID_A_COLOR_TO: return to;
		default: return 0xFF000000;
		}
	}

	std::wstring HexColor(std::uint32_t color)
	{
		wchar_t buffer[16] = {};
		swprintf_s(buffer, L"#%06X", color & 0x00FFFFFFu);
		return buffer;
	}

	// 候选窗已在绘制时应用背景透明度，按每像素 alpha 合成预览。
	void CompositeCandidate(DpiCanvas& target, const coral::win32::CandidateView& view, int x, int y)
	{
		Canvas layer(view.width(), view.height());
		if (!layer.valid()) return;
		view.draw(layer.dc(), layer.pixels());
		for (int row = 0; row < layer.height(); ++row)
		{
			const int ty = y + row;
			if (ty < 0 || ty >= target.height()) continue;
			for (int column = 0; column < layer.width(); ++column)
			{
				const int tx = x + column;
				if (tx < 0 || tx >= target.width()) continue;
				const std::uint32_t source = layer.pixels()[static_cast<size_t>(row) * layer.width() + column];
				std::uint32_t& destination = target.pixels()[static_cast<size_t>(ty) * target.width() + tx];
				const std::uint32_t alpha = source >> 24;
				if (!alpha) continue;
				const auto channel = [&](int shift) {
					const std::uint32_t s = (source >> shift) & 0xFF;
					const std::uint32_t d = (destination >> shift) & 0xFF;
					return (std::min)(255u, s + d * (255 - alpha) / 255) << shift;
				};
				destination = 0xFF000000u | channel(16) | channel(8) | channel(0);
			}
		}
	}

	void PaintPreview(HWND hwnd, HDC target)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		// 模拟桌面：渐变底与几行“文档”，用来体现透明度。
		const RectF frame{ 0.5f, 0.5f, static_cast<float>(client.right) - 0.5f,
			static_cast<float>(client.bottom) - 0.5f };
		FillGradientRoundRect(surface, frame, 10.0f, s.darkCaption ? 0xFF262A31 : 0xFFFFF0E5,
			s.darkCaption ? 0xFF1E2A37 : 0xFFF5DECE);
		const std::uint32_t ink = s.darkCaption ? 0x26FFFFFF : 0x246A4838;
		for (int i = 0; i < 9; ++i)
		{
			const float top = 18.0f + static_cast<float>(i) * 19.0f;
			const float width = static_cast<float>(client.right) * (0.45f + 0.07f * static_cast<float>((i * 3) % 7));
			FillRoundRect(surface, { 22.0f, top, (std::min)(width, frame.right - 22.0f), top + 7.0f }, 3.5f, ink);
		}
		StrokeRoundRect(surface, frame, 10.0f, 1.0f, s.line);

		const bool dark = CandidateDark();
		coral::win32::CandidateSkin skin = coral::win32::CandidateSkin::themed(dark, g_candidateSkin,
			g_candidateSkin.imagePath(InstallDir(), UserDir()));
		coral::composition::CandidateRenderSnapshot snapshot;
		snapshot.visible = true;
		snapshot.fontName = sConfig->getCandidateFontName();
		snapshot.fontSize = (std::min)(sConfig->getCandidateFontSize(), 16);
		static const char* const kWords[] = { "皮肤", "匹夫", "批复", "皮", "批", "匹", "辟",
			"屁", "劈", "披", "疲", "脾", "琵琶", "啤酒", "媲美", "譬如", "僻静", "痞子",
			"枇杷", "霹雳", "毗邻", "貔貅", "砒霜", "郫县", "皮革", "皮球", "批评", "批准",
			"匹配", "疲惫", "脾气", "啤", "琶", "罴", "鼙", "陴", "埤", "蚍", "裨", "邳",
			"皮蛋", "批发", "披风", "劈柴", "屁股", "譬喻", "僻", "睥", "仳", "圮" };
		std::uint64_t id = 1;
		for (const char* word : kWords)
		{
			coral::composition::RenderCandidate item;
			item.id = id++;
			item.text = word;
			snapshot.candidates.push_back(std::move(item));
		}
		coral::win32::CandidateView collapsed;
		collapsed.setSkin(skin);
		collapsed.update(hwnd, snapshot);
		coral::win32::CandidateView expanded;
		expanded.setSkin(skin);
		snapshot.expanded = true;
		snapshot.selection = 8;
		expanded.update(hwnd, snapshot);

		const RECT upper = collapsed.panelRect();
		const RECT lower = expanded.panelRect();
		constexpr int kGap = 14;
		const int total = (upper.bottom - upper.top) + g_dpi.pixels(kGap) + (lower.bottom - lower.top);
		const int top = (canvas.height() - total) / 2;
		const int left = (canvas.width() - (upper.right - upper.left)) / 2;
		CompositeCandidate(canvas, collapsed, left - upper.left, top - upper.top);
		CompositeCandidate(canvas, expanded, left - lower.left,
			top + (upper.bottom - upper.top) + g_dpi.pixels(kGap) - lower.top);
		canvas.blit(target, 0, 0);
	}

	void ForwardDroppedImage(HWND hwnd, HDROP drop)
	{
		wchar_t path[MAX_PATH * 2] = {};
		if (DragQueryFileW(drop, 0, path, ARRAYSIZE(path)))
		{
			g_droppedImage = path;
			SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(kCustomImageTile, kImageDropped),
				reinterpret_cast<LPARAM>(hwnd));
		}
		DragFinish(drop);
	}

	// 设置程序以管理员身份运行时，资源管理器的拖放消息会被 UIPI 拦截。
	void AcceptImageDrops(HWND hwnd)
	{
		DragAcceptFiles(hwnd, TRUE);
		ChangeWindowMessageFilterEx(hwnd, WM_DROPFILES, MSGFLT_ALLOW, nullptr);
		ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
		ChangeWindowMessageFilterEx(hwnd, 0x0049 /*  WM_COPYGLOBALDATA（跨进程复制全局数据消息） */, MSGFLT_ALLOW, nullptr);
	}

	LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintPreview(hwnd, dc);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintPreview(hwnd, reinterpret_cast<HDC>(wParam));
			return 0;
		case WM_DROPFILES:
			ForwardDroppedImage(hwnd, reinterpret_cast<HDROP>(wParam));
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------------------------------------------------------------
	// 常用语和剪贴板页：分段切换 / 用法插画 / 列表
	// ---------------------------------------------------------------------
	int g_phraseTab = 0; // 0 常用语，1 剪贴板
	std::vector<CustomPhraseEntry> g_phrases;      // 与常用语列表行一一对应
	std::vector<std::wstring> g_clipTexts;         // 与剪贴板列表行一一对应
	constexpr const wchar_t* kPhraseTabs[] = { L"常用语", L"剪贴板" };
	constexpr int kSegmentWidth = 120;
	constexpr int kSegmentHeight = 38;
	constexpr int kIllustrationHeight = 150;
	constexpr int kListRow = 48;
	constexpr int kListPadding = 12;

	void PaintSegment(HWND hwnd, HDC target, int hover)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		const float width = static_cast<float>(client.right);
		const float height = static_cast<float>(client.bottom);
		FillRoundRect(surface, { 0.0f, 0.0f, width, height }, 10.0f, Mix(s.window, s.text, 0.07f));
		const float half = width / 2.0f;
		const RectF active{ 3.0f + half * g_phraseTab, 3.0f, half * (g_phraseTab + 1) - 3.0f, height - 3.0f };
		DrawShadow(surface, active, 8.0f, { 1.0f, 2.0f, 0.0f, s.navShadow }, 1.0f);
		FillRoundRect(surface, active, 8.0f, s.surface);
		StrokeRoundRect(surface, active, 8.0f, 1.0f, s.line);
		canvas.beginText();
		for (int i = 0; i < 2; ++i)
		{
			const RECT rect{ static_cast<int>(half * i), 0, static_cast<int>(half * (i + 1)), client.bottom };
			const bool selected = i == g_phraseTab;
			DrawTextIn(canvas.dc(), kPhraseTabs[i], rect, selected ? F_NAV_ACTIVE : F_NAV,
				selected || i == hover ? s.text : s.sub, DT_CENTER | DT_VCENTER);
		}
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	LRESULT CALLBACK SegmentProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// GWLP_USERDATA 保存悬停的分段序号 + 1。
		const int hover = static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)) - 1;
		const auto segmentAt = [hwnd](LPARAM point) {
			RECT client{};
			logical_client_rect(hwnd, &client);
			return g_dpi.logical(GET_X_LPARAM(point)) < client.right / 2 ? 0 : 1;
		};
		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintSegment(hwnd, dc, hover);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintSegment(hwnd, reinterpret_cast<HDC>(wParam), hover);
			return 0;
		case WM_SETCURSOR:
			SetCursor(LoadCursorW(nullptr, IDC_HAND));
			return TRUE;
		case WM_MOUSEMOVE:
		{
			const int segment = segmentAt(lParam);
			if (segment != hover)
			{
				SetWindowLongPtrW(hwnd, GWLP_USERDATA, segment + 1);
				TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&track);
				invalidate_logical_rect(hwnd, nullptr, FALSE);
			}
			return 0;
		}
		case WM_MOUSELEAVE:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONUP:
		{
			const int segment = segmentAt(lParam);
			if (segment != g_phraseTab)
				SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_P_TAB + segment, BN_CLICKED),
					reinterpret_cast<LPARAM>(hwnd));
			return 0;
		}
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// 用法示意：输入码 + 候选条，常用语排在首位。
	void PaintIllustration(HWND hwnd, HDC target)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		const RectF frame{ 0.0f, 0.0f, static_cast<float>(client.right), static_cast<float>(client.bottom) };
		FillGradientRoundRect(surface, frame, 10.0f, Mix(s.surface, s.accent, 0.16f), Mix(s.surface, s.accent, 0.05f));

		const std::wstring code = L"sxs";
		const std::wstring first = L"山西省太原市尖草坪区上兰街道学院路3号 中北大学";
		const std::wstring second = L"山西省";
		ScopedFont codeFont(s.fontName, g_dpi.pixels(18), FW_BOLD);
		ScopedFont wordFont(s.fontName, g_dpi.pixels(16), FW_SEMIBOLD);
		HDC dc = canvas.dc();
		const auto measure = [dc](HFONT font, const std::wstring& text) {
			HGDIOBJ old = SelectObject(dc, font);
			SIZE size{};
			GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
			SelectObject(dc, old);
			return static_cast<int>(std::ceil(size.cx / g_dpi.factor()));
		};
		const int left = 48;
		const int codeTop = 26;
		const int codeWidth = measure(codeFont.get(), code);
		FillRoundRect(surface, { static_cast<float>(left + codeWidth + 3), codeTop + 4.0f,
			static_cast<float>(left + codeWidth + 5), codeTop + 24.0f }, 1.0f, s.accent);

		const int barTop = codeTop + 38;
		const int pillWidth = measure(wordFont.get(), L"1" + first) + 22;
		const int secondLeft = left + 6 + pillWidth + 14;
		const int secondWidth = measure(wordFont.get(), L"2" + second);
		const int chevron = secondLeft + secondWidth + 22;
		const RectF bar{ static_cast<float>(left), static_cast<float>(barTop),
			static_cast<float>(chevron + 20), static_cast<float>(barTop + 42) };
		DrawShadow(surface, bar, 8.0f, { 2.0f, 10.0f, 0.0f, s.cardShadow }, 1.0f);
		FillRoundRect(surface, bar, 8.0f, s.surface);
		const RectF pill{ bar.left + 6.0f, bar.top + 6.0f, bar.left + 6.0f + pillWidth, bar.bottom - 6.0f };
		FillRoundRect(surface, pill, 6.0f, s.accent);
		DrawChevron(surface, static_cast<float>(chevron), (bar.top + bar.bottom) * 0.5f, 12.0f, 1.4f, false, s.sub);

		canvas.beginText();
		const auto draw = [dc](HFONT font, const std::wstring& text, RECT rect, std::uint32_t color) {
			HGDIOBJ old = SelectObject(dc, font);
			SetTextColor(dc, ToColorRef(color));
			rect = g_dpi.pixels(rect);
			DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect,
				DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			SelectObject(dc, old);
		};
		SetBkMode(dc, TRANSPARENT);
		draw(codeFont.get(), code, { left, codeTop, left + codeWidth + 2, codeTop + 28 }, s.text);
		const RECT pillRect{ static_cast<int>(pill.left) + 11, static_cast<int>(pill.top),
			static_cast<int>(pill.right), static_cast<int>(pill.bottom) };
		draw(wordFont.get(), L"1" + first, pillRect, s.onAccent);
		const int digitWidth = measure(wordFont.get(), L"2");
		const RECT secondRect{ secondLeft, static_cast<int>(bar.top), secondLeft + secondWidth, static_cast<int>(bar.bottom) };
		draw(wordFont.get(), L"2", secondRect, s.muted);
		draw(wordFont.get(), second, { secondLeft + digitWidth, secondRect.top, secondRect.right + 2, secondRect.bottom }, s.text);
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	LRESULT CALLBACK IllustrationProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintIllustration(hwnd, dc);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintIllustration(hwnd, reinterpret_cast<HDC>(wParam));
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// 单行列表：左侧正文（超长省略），右侧可选注释；内部滚动，右键行向主窗口发 kListContext。
	struct ListState
	{
		std::vector<std::wstring> texts;
		std::vector<std::wstring> notes;
		std::wstring emptyText;
		std::wstring defaultEmptyText;
		int hover = -1;
		int scroll = 0;
	};

	ListState* ListOf(HWND hwnd) { return reinterpret_cast<ListState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

	int ListMaxScroll(HWND hwnd, const ListState& state)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		return (std::max)(0, static_cast<int>(state.texts.size()) * kListRow - static_cast<int>(client.bottom));
	}

	int ListRowAt(HWND hwnd, const ListState& state, int y)
	{
		const int row = (y + state.scroll) / kListRow;
		return y >= 0 && row >= 0 && row < static_cast<int>(state.texts.size()) ? row : -1;
	}

	void SetListItems(HWND hwnd, std::vector<std::wstring> texts, std::vector<std::wstring> notes,
		const std::wstring& emptyText = {})
	{
		ListState* state = hwnd ? ListOf(hwnd) : nullptr;
		if (!state) return;
		state->texts = std::move(texts);
		state->notes = std::move(notes);
		state->notes.resize(state->texts.size());
		state->emptyText = emptyText.empty() ? state->defaultEmptyText : emptyText;
		state->hover = -1;
		state->scroll = (std::clamp)(state->scroll, 0, ListMaxScroll(hwnd, *state));
		invalidate_logical_rect(hwnd, nullptr, FALSE);
	}

	void PaintList(HWND hwnd, HDC target, const ListState& state)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		const int count = static_cast<int>(state.texts.size());
		const int first = state.scroll / kListRow;
		const int last = (std::min)(count, static_cast<int>((state.scroll + client.bottom) / kListRow + 1));
		const float right = static_cast<float>(client.right);
		for (int i = first; i < last; ++i)
		{
			const float top = static_cast<float>(i * kListRow - state.scroll);
			if (i == state.hover)
				FillRoundRect(surface, { 4.0f, top + 3.0f, right - 4.0f, top + kListRow - 3.0f }, 6.0f, s.hover);
			if (i + 1 < count)
				FillRoundRect(surface, { static_cast<float>(kListPadding), top + kListRow - 1.0f,
					right - kListPadding, top + kListRow }, 0.0f, s.line);
		}
		const int maxScroll = ListMaxScroll(hwnd, state);
		if (maxScroll > 0)
		{
			const float track = static_cast<float>(client.bottom) - 8.0f;
			const float thumb = (std::max)(28.0f, track * client.bottom / (count * kListRow));
			const float thumbTop = 4.0f + (track - thumb) * state.scroll / maxScroll;
			FillRoundRect(surface, { right - 5.0f, thumbTop, right - 2.0f, thumbTop + thumb }, 1.5f, s.scrollThumb);
		}
		canvas.beginText();
		HDC dc = canvas.dc();
		if (count == 0)
			DrawTextIn(dc, state.emptyText, client, F_CONTROL, s.muted, DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS);
		for (int i = first; i < last; ++i)
		{
			const int top = i * kListRow - state.scroll;
			int textRight = client.right - kListPadding;
			if (!state.notes[i].empty())
			{
				const int noteWidth = (std::min)(static_cast<int>(MeasureText(dc, F_CONTROL, state.notes[i]).cx),
					static_cast<int>(client.right / 3));
				const RECT note{ textRight - noteWidth, top, textRight, top + kListRow };
				DrawTextIn(dc, state.notes[i], note, F_CONTROL, s.sub, DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
				textRight = note.left - 16;
			}
			DrawTextIn(dc, state.texts[i], { kListPadding, top, textRight, top + kListRow }, F_LABEL, s.text,
				DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
		}
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_NCCREATE)
		{
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		}
		ListState* state = ListOf(hwnd);
		if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);
		switch (msg)
		{
		case WM_NCDESTROY:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			delete state;
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintList(hwnd, dc, *state);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintList(hwnd, reinterpret_cast<HDC>(wParam), *state);
			return 0;
		case WM_MOUSEWHEEL:
		{
			const int maxScroll = ListMaxScroll(hwnd, *state);
			const int scroll = (std::clamp)(state->scroll - GET_WHEEL_DELTA_WPARAM(wParam) * kListRow / WHEEL_DELTA,
				0, maxScroll);
			// 列表到顶/到底后交给页面滚动。
			if (scroll == state->scroll)
				return SendMessageW(GetParent(hwnd), msg, wParam, lParam);
			state->scroll = scroll;
			POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			screen_to_logical_client(hwnd, &point);
			state->hover = ListRowAt(hwnd, *state, point.y);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
			const int row = ListRowAt(hwnd, *state, g_dpi.logical(GET_Y_LPARAM(lParam)));
			if (row != state->hover)
			{
				state->hover = row;
				TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&track);
				invalidate_logical_rect(hwnd, nullptr, FALSE);
			}
			return 0;
		}
		case WM_MOUSELEAVE:
			state->hover = -1;
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_RBUTTONUP:
		{
			const int row = ListRowAt(hwnd, *state, g_dpi.logical(GET_Y_LPARAM(lParam)));
			if (row < 0) return 0;
			POINT point{ g_dpi.logical(GET_X_LPARAM(lParam)), g_dpi.logical(GET_Y_LPARAM(lParam)) };
			logical_client_to_screen(hwnd, &point);
			SendMessageW(GetAncestor(hwnd, GA_ROOT), kListContext, MAKEWPARAM(GetDlgCtrlID(hwnd), row),
				MAKELPARAM(point.x, point.y));
			return 0;
		}
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------------------------------------------------------------
	// 自绘控件：开关 / 单选 / 复选（可带键帽）/ 按钮
	// ---------------------------------------------------------------------
	// Tile：主题选项卡片；Tab：主题分类标签；Swatch：调色板色块按钮（点击打开取色器）。
	enum class ControlKind { Switch, Radio, Check, Button, Tile, Tab, Swatch };
	enum class ButtonTone { Normal, Primary, Danger, Link };

	struct ControlState
	{
		ControlKind kind = ControlKind::Button;
		ButtonTone tone = ButtonTone::Normal;
		bool keycap = false;
		int group = 0;
		bool checked = false;
		bool hover = false;
		bool pressed = false;
		float from = 0.0f;
		float to = 0.0f;
		std::chrono::steady_clock::time_point start{};
	};
	constexpr UINT_PTR kSwitchTimer = 1;

	float SwitchProgress(const ControlState& state)
	{
		const float duration = static_cast<float>((std::max)(1, Skin().switchAnimationMs));
		const float elapsed = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - state.start).count();
		if (elapsed >= duration) return state.to;
		return state.from + (state.to - state.from) * EaseOut(elapsed / duration);
	}

	ControlState* StateOf(HWND hwnd)
	{
		return reinterpret_cast<ControlState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}

	void SetChecked(HWND hwnd, ControlState& state, bool checked, bool animate)
	{
		if (state.kind == ControlKind::Switch)
		{
			state.from = animate ? SwitchProgress(state) : (checked ? 1.0f : 0.0f);
			state.to = checked ? 1.0f : 0.0f;
			state.start = std::chrono::steady_clock::now();
			if (animate) SetTimer(hwnd, kSwitchTimer, USER_TIMER_MINIMUM, nullptr);
		}
		state.checked = checked;
		invalidate_logical_rect(hwnd, nullptr, FALSE);
	}

	void ActivateControl(HWND hwnd, ControlState& state)
	{
		switch (state.kind)
		{
		case ControlKind::Switch:
		case ControlKind::Check:
			SetChecked(hwnd, state, !state.checked, true);
			break;
		case ControlKind::Radio:
			for (HWND sibling = GetWindow(GetParent(hwnd), GW_CHILD); sibling;
				sibling = GetWindow(sibling, GW_HWNDNEXT))
			{
				wchar_t name[64] = {};
				GetClassNameW(sibling, name, ARRAYSIZE(name));
				ControlState* other = sibling != hwnd && wcscmp(name, kControlClass) == 0 ?
					StateOf(sibling) : nullptr;
				if (other && other->kind == ControlKind::Radio && other->group == state.group &&
					other->checked)
					SetChecked(sibling, *other, false, false);
			}
			SetChecked(hwnd, state, true, false);
			break;
		case ControlKind::Button:
		case ControlKind::Tile:
		case ControlKind::Tab:
		case ControlKind::Swatch:
			break;
		}
		SendMessageW(GetParent(hwnd), WM_COMMAND,
			MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
	}

	void PaintControl(HWND hwnd, HDC target, const ControlState& state)
	{
		if (state.kind == ControlKind::Tile)
		{
			PaintTile(hwnd, target, state.hover);
			return;
		}
		if (state.kind == ControlKind::Tab)
		{
			PaintTab(hwnd, target, state.hover);
			return;
		}
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		const bool enabled = IsWindowEnabled(hwnd) != FALSE;
		const std::uint32_t accent = !enabled ? s.muted : state.pressed ? s.accentPressed :
			state.hover ? s.accentHover : s.accent;
		const float width = static_cast<float>(client.right);
		const float height = static_cast<float>(client.bottom);
		const float cy = height * 0.5f;
		wchar_t buffer[128] = {};
		GetWindowTextW(hwnd, buffer, ARRAYSIZE(buffer));
		const std::wstring text = buffer;
		const float box = static_cast<float>(s.choiceSize);
		RECT textRect{ s.choiceSize + 8, 0, client.right, client.bottom };
		RECT keycap{};

		switch (state.kind)
		{
		case ControlKind::Switch:
		{
			const float p = SwitchProgress(state);
			const float trackHeight = static_cast<float>(s.switchHeight);
			const RectF track{ 0.0f, cy - trackHeight * 0.5f, static_cast<float>(s.switchWidth),
				cy + trackHeight * 0.5f };
			const float radius = trackHeight * 0.5f;
			FillRoundRect(surface, track, radius, state.hover ? s.hover : s.surface);
			StrokeRoundRect(surface, track, radius, 1.0f, enabled ? s.switchBorder : s.muted);
			FillRoundRect(surface, track, radius, Fade(accent, p));
			const float knob = (state.hover ? 7.0f : 6.0f);
			const float travel = track.right - track.left - 2.0f * (radius);
			const float cx = track.left + radius + travel * p;
			FillCircle(surface, cx, cy, knob, Mix(s.switchKnob, s.choiceMark, p));
			break;
		}
		case ControlKind::Radio:
		{
			const float cx = box * 0.5f;
			if (state.checked)
			{
				FillCircle(surface, cx, cy, box * 0.5f, accent);
				FillCircle(surface, cx, cy, state.hover ? 4.5f : 3.8f, s.choiceMark);
			}
			else
			{
				FillCircle(surface, cx, cy, box * 0.5f, state.hover ? s.hover : s.surface);
				StrokeCircle(surface, cx, cy, box * 0.5f, 1.2f, state.hover ? s.sub : s.switchBorder);
			}
			break;
		}
		case ControlKind::Check:
		{
			const RectF square{ 0.0f, cy - box * 0.5f, box, cy + box * 0.5f };
			if (state.checked)
			{
				FillRoundRect(surface, square, 4.0f, accent);
				const float points[] = { 4.6f, cy + 0.2f, 7.6f, cy + 3.2f, 13.4f, cy - 3.4f };
				StrokePolyline(surface, points, 3, 1.8f, s.choiceMark);
			}
			else
			{
				FillRoundRect(surface, square, 4.0f, state.hover ? s.hover : s.surface);
				StrokeRoundRect(surface, square, 4.0f, 1.2f, state.hover ? s.sub : s.switchBorder);
			}
			if (state.keycap)
			{
				const SIZE size = MeasureText(canvas.dc(), F_SMALL, text);
				const int top = (client.bottom - 25) / 2;
				keycap = { textRect.left, top, textRect.left + size.cx + 14, top + 25 };
				const RectF chip = ToRectF(keycap);
				FillRoundRect(surface, chip, 5.0f, s.lineStrong);
				FillRoundRect(surface, { chip.left + 1.0f, chip.top + 1.0f, chip.right - 1.0f,
					chip.bottom - 2.0f }, 4.0f, s.kbdFill);
			}
			break;
		}
		case ControlKind::Button:
		{
			if (state.tone == ButtonTone::Link) break;
			const RectF rect{ 0.0f, 0.0f, width, height };
			const float radius = static_cast<float>(s.controlRadius);
			if (state.tone == ButtonTone::Primary)
			{
				FillRoundRect(surface, rect, radius, accent);
			}
			else
			{
				FillRoundRect(surface, rect, radius,
					state.pressed ? s.pressed : state.hover ? s.hover : s.surface);
				StrokeRoundRect(surface, rect, radius, 1.0f, state.hover ? s.lineHover : s.lineStrong);
			}
			break;
		}
		case ControlKind::Swatch:
		{
			const RectF rect{ 0.0f, 0.0f, width, height };
			const float radius = static_cast<float>(s.controlRadius);
			FillRoundRect(surface, rect, radius,
				!enabled ? s.window : state.pressed ? s.pressed : state.hover ? s.hover : s.surface);
			StrokeRoundRect(surface, rect, radius, 1.0f, state.hover && enabled ? s.lineHover : s.lineStrong);
			const RectF chip{ 10.0f, cy - 9.0f, 28.0f, cy + 9.0f };
			FillRoundRect(surface, chip, 4.0f, Fade(SwatchColor(GetDlgCtrlID(hwnd)), enabled ? 1.0f : 0.35f));
			StrokeRoundRect(surface, chip, 4.0f, 1.0f, s.lineStrong);
			break;
		}
		case ControlKind::Tile:
		case ControlKind::Tab:
			break;
		}

		canvas.beginText();
		HDC dc = canvas.dc();
		if (state.kind == ControlKind::Swatch)
		{
			const RECT captionRect{ 36, 0, client.right - 10, client.bottom };
			DrawTextIn(dc, text, captionRect, F_CONTROL, enabled ? s.text : s.muted, DT_LEFT | DT_VCENTER);
			DrawTextIn(dc, HexColor(SwatchColor(GetDlgCtrlID(hwnd))), captionRect, F_CONTROL,
				enabled ? s.sub : s.muted, DT_RIGHT | DT_VCENTER);
		}
		else if (state.kind == ControlKind::Button)
		{
			if (state.tone == ButtonTone::Link)
			{
				DrawTextIn(dc, text, client, F_LINK, !enabled ? s.muted :
					state.pressed ? s.linkPressed : state.hover ? s.linkHover : s.link,
					DT_CENTER | DT_VCENTER);
			}
			else
			{
				const std::uint32_t color = state.tone == ButtonTone::Primary ? s.onAccent :
					!enabled ? s.muted : state.tone == ButtonTone::Danger ? s.danger : s.text;
				DrawTextIn(dc, text, client, F_CONTROL, color, DT_CENTER | DT_VCENTER);
			}
		}
		else if (state.keycap)
		{
			RECT rect = keycap;
			rect.bottom -= 1;
			DrawTextIn(dc, text, rect, F_SMALL, s.sub, DT_CENTER | DT_VCENTER);
		}
		else if (!text.empty())
		{
			DrawTextIn(dc, text, textRect, F_LABEL, enabled ? s.text : s.muted, DT_LEFT | DT_VCENTER);
		}
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	LRESULT CALLBACK ControlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == WM_NCCREATE)
		{
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		}
		ControlState* state = StateOf(hwnd);
		if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);
		switch (msg)
		{
		case WM_NCDESTROY:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			delete state;
			break;
		case BM_GETCHECK:
			return state->checked ? BST_CHECKED : BST_UNCHECKED;
		case BM_SETCHECK:
			SetChecked(hwnd, *state, wParam == BST_CHECKED, false);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintControl(hwnd, dc, *state);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintControl(hwnd, reinterpret_cast<HDC>(wParam), *state);
			return 0;
		case WM_TIMER:
			if (wParam == kSwitchTimer)
			{
				if (SwitchProgress(*state) == state->to) KillTimer(hwnd, kSwitchTimer);
				invalidate_logical_rect(hwnd, nullptr, FALSE);
			}
			return 0;
		case WM_SETCURSOR:
			SetCursor(LoadCursorW(nullptr, IDC_HAND));
			return TRUE;
		case WM_MOUSEMOVE:
			if (!state->hover)
			{
				state->hover = true;
				TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&track);
				invalidate_logical_rect(hwnd, nullptr, FALSE);
			}
			return 0;
		case WM_MOUSELEAVE:
			state->hover = false;
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONDOWN:
			state->pressed = true;
			SetCapture(hwnd);
			SetFocus(hwnd);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONUP:
			if (state->pressed)
			{
				state->pressed = false;
				ReleaseCapture();
				RECT client{};
				logical_client_rect(hwnd, &client);
				POINT point{ g_dpi.logical(GET_X_LPARAM(lParam)), g_dpi.logical(GET_Y_LPARAM(lParam)) };
				invalidate_logical_rect(hwnd, nullptr, FALSE);
				if (PtInRect(&client, point)) ActivateControl(hwnd, *state);
			}
			return 0;
		case WM_CAPTURECHANGED:
			if (state->pressed) { state->pressed = false; invalidate_logical_rect(hwnd, nullptr, FALSE); }
			return 0;
		case WM_KEYUP:
			if (wParam == VK_SPACE || wParam == VK_RETURN) ActivateControl(hwnd, *state);
			return 0;
		case WM_ENABLE:
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_DROPFILES:
			ForwardDroppedImage(hwnd, reinterpret_cast<HDROP>(wParam));
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------------------------------------------------------------
	// 自绘滑块：兼容 TBM_SETRANGE / TBM_SETPOS / TBM_GETPOS，拖动时向父窗口发 WM_HSCROLL
	// ---------------------------------------------------------------------
	struct SliderState
	{
		int minimum = 0;
		int maximum = 6;
		int position = 0;
		bool hover = false;
		bool dragging = false;
	};
	constexpr float kSliderThumb = 10.0f;

	void SliderSetFromPoint(HWND hwnd, SliderState& state, int x)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		const float left = kSliderThumb;
		const float right = static_cast<float>(client.right) - kSliderThumb;
		const float t = (std::clamp)((static_cast<float>(x) - left) / (std::max)(1.0f, right - left), 0.0f, 1.0f);
		const int position = state.minimum + static_cast<int>(std::lround(t * (state.maximum - state.minimum)));
		if (position == state.position) return;
		state.position = position;
		invalidate_logical_rect(hwnd, nullptr, FALSE);
		SendMessageW(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, position),
			reinterpret_cast<LPARAM>(hwnd));
	}

	void PaintSlider(HWND hwnd, HDC target, const SliderState& state)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		const float cy = static_cast<float>(client.bottom) * 0.5f;
		const float left = kSliderThumb;
		const float right = static_cast<float>(client.right) - kSliderThumb;
		const float t = state.maximum > state.minimum ?
			static_cast<float>(state.position - state.minimum) / (state.maximum - state.minimum) : 0.0f;
		const float x = left + (right - left) * t;
		FillRoundRect(surface, { left, cy - 2.0f, right, cy + 2.0f }, 2.0f, s.sliderTrack);
		FillRoundRect(surface, { left, cy - 2.0f, x, cy + 2.0f }, 2.0f, s.accent);
		const RectF thumb{ x - kSliderThumb, cy - kSliderThumb, x + kSliderThumb, cy + kSliderThumb };
		DrawShadow(surface, thumb, kSliderThumb, { 0.5f, 2.0f, 0.0f, 0x29000000 }, 1.0f);
		FillCircle(surface, x, cy, kSliderThumb, s.surface);
		StrokeCircle(surface, x, cy, kSliderThumb, 1.0f, s.lineStrong);
		FillCircle(surface, x, cy, state.dragging ? 5.0f : state.hover ? 7.0f : 6.0f,
			state.hover || state.dragging ? s.accentHover : s.accent);
		canvas.blit(target, 0, 0);
	}

	LRESULT CALLBACK SliderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		auto* state = reinterpret_cast<SliderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (msg == WM_NCCREATE)
		{
			state = new SliderState();
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
		}
		if (!state) return DefWindowProcW(hwnd, msg, wParam, lParam);
		switch (msg)
		{
		case WM_NCDESTROY:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			delete state;
			break;
		case TBM_SETRANGE:
			state->minimum = LOWORD(lParam);
			state->maximum = (std::max)(state->minimum, static_cast<int>(HIWORD(lParam)));
			state->position = (std::clamp)(state->position, state->minimum, state->maximum);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case TBM_SETPOS:
			state->position = (std::clamp)(static_cast<int>(lParam), state->minimum, state->maximum);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case TBM_GETPOS:
			return state->position;
		case TBM_SETTICFREQ:
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintSlider(hwnd, dc, *state);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintSlider(hwnd, reinterpret_cast<HDC>(wParam), *state);
			return 0;
		case WM_SETCURSOR:
			SetCursor(LoadCursorW(nullptr, IDC_HAND));
			return TRUE;
		case WM_MOUSEMOVE:
			if (!state->hover)
			{
				state->hover = true;
				TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&track);
				invalidate_logical_rect(hwnd, nullptr, FALSE);
			}
			if (state->dragging) SliderSetFromPoint(hwnd, *state, g_dpi.logical(GET_X_LPARAM(lParam)));
			return 0;
		case WM_MOUSELEAVE:
			state->hover = false;
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONDOWN:
			state->dragging = true;
			SetCapture(hwnd);
			SetFocus(hwnd);
			SliderSetFromPoint(hwnd, *state, g_dpi.logical(GET_X_LPARAM(lParam)));
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_LBUTTONUP:
			if (state->dragging) ReleaseCapture();
			return 0;
		case WM_CAPTURECHANGED:
			state->dragging = false;
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return 0;
		case WM_KEYDOWN:
			if (wParam == VK_LEFT || wParam == VK_RIGHT)
			{
				const int position = (std::clamp)(state->position + (wParam == VK_LEFT ? -1 : 1),
					state->minimum, state->maximum);
				if (position != state->position)
				{
					state->position = position;
					invalidate_logical_rect(hwnd, nullptr, FALSE);
					SendMessageW(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, position),
						reinterpret_cast<LPARAM>(hwnd));
				}
			}
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	// ---------------------------------------------------------------------
	// 原生下拉框：只重绘闭合态外观，下拉列表仍由系统绘制
	// ---------------------------------------------------------------------
	constexpr wchar_t kComboHotProp[] = L"CoralSettingsHot";

	void PaintCombo(HWND hwnd, HDC target)
	{
		RECT client{};
		logical_client_rect(hwnd, &client);
		DpiCanvas canvas(client.right, client.bottom, g_dpi);
		if (!canvas.valid()) return;
		PaintBackdrop(hwnd, canvas);
		Surface surface = canvas.surface();
		const SettingsSkin& s = Skin();
		const bool enabled = IsWindowEnabled(hwnd) != FALSE;
		const bool dropped = SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0) != FALSE;
		const bool hot = enabled && GetPropW(hwnd, kComboHotProp) != nullptr;
		const RectF rect = ToRectF(client);
		const float radius = static_cast<float>(s.controlRadius);
		FillRoundRect(surface, rect, radius, !enabled ? s.window : dropped ? s.pressed :
			hot ? s.hover : s.surface);
		StrokeRoundRect(surface, rect, radius, 1.0f, dropped ? s.accent : hot ? s.lineHover : s.lineStrong);
		DrawChevron(surface, rect.right - 16.0f, rect.bottom * 0.5f, 14.0f, 1.4f, dropped,
			enabled ? s.sub : s.muted);
		std::wstring text;
		const LRESULT selection = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
		if (selection != CB_ERR)
		{
			const LRESULT length = SendMessageW(hwnd, CB_GETLBTEXTLEN, selection, 0);
			if (length > 0)
			{
				text.resize(static_cast<size_t>(length) + 1);
				SendMessageW(hwnd, CB_GETLBTEXT, selection, reinterpret_cast<LPARAM>(text.data()));
				text.resize(static_cast<size_t>(length));
			}
		}
		canvas.beginText();
		RECT textRect{ 11, 0, client.right - 30, client.bottom };
		DrawTextIn(canvas.dc(), text, textRect, F_CONTROL, enabled ? s.text : s.muted,
			DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	LRESULT CALLBACK ComboProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR, DWORD_PTR)
	{
		switch (msg)
		{
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			PaintCombo(hwnd, dc);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			PaintCombo(hwnd, reinterpret_cast<HDC>(wParam));
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case WM_MOUSEMOVE:
			if (!GetPropW(hwnd, kComboHotProp))
			{
				SetPropW(hwnd, kComboHotProp, reinterpret_cast<HANDLE>(1));
				TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&track);
				invalidate_logical_rect(hwnd, nullptr, FALSE);
			}
			break;
		case WM_MOUSELEAVE:
			RemovePropW(hwnd, kComboHotProp);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			break;
		case WM_MOUSEWHEEL:
			// 闭合态滚轮交给页面滚动，避免误改选项。
			if (!SendMessageW(hwnd, CB_GETDROPPEDSTATE, 0, 0))
				return SendMessageW(GetParent(hwnd), msg, wParam, lParam);
			break;
		case WM_SETFOCUS:
		case WM_KILLFOCUS:
		case WM_ENABLE:
		case CB_SETCURSEL:
		{
			const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
			return result;
		}
		case WM_NCDESTROY:
			RemovePropW(hwnd, kComboHotProp);
			RemoveWindowSubclass(hwnd, ComboProc, 1);
			break;
		}
		return DefSubclassProc(hwnd, msg, wParam, lParam);
	}

	// ---------------------------------------------------------------------
	// 页面布局：分区标题 + 卡片 + 行（左侧名称/说明，右侧控件；stack 行控件另起一行）
	// ---------------------------------------------------------------------
    LRESULT CALLBACK PasswordProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
        UINT_PTR, DWORD_PTR)
    {
        const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
            HDC dc = msg == WM_PAINT ? GetDC(hwnd) : reinterpret_cast<HDC>(wParam);
            RECT bounds{};
            GetClientRect(hwnd, &bounds);
            HBRUSH border = CreateSolidBrush(ToColorRef(Skin().sub));
            FrameRect(dc, &bounds, border);
            DeleteObject(border);
            if (msg == WM_PAINT) ReleaseDC(hwnd, dc);
        } else if (msg == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, PasswordProc, 1);
        }
        return result;
    }

	class PageBuilder
	{
	public:
		PageBuilder(HWND page, PageLayout& layout, int width)
			: m_page(page), m_layout(layout), m_measure(CreateCompatibleDC(nullptr))
		{
			const SettingsSkin& s = Skin();
			const int inner = (std::min)(s.contentMaxWidth, width - 4);
			m_left = (width - inner) / 2;
			m_right = m_left + inner;
			m_y = s.workspacePadding + 2;
		}
		~PageBuilder() { DeleteDC(m_measure); }
		PageBuilder(const PageBuilder&) = delete;
		PageBuilder& operator=(const PageBuilder&) = delete;

		void header(const wchar_t* title, const wchar_t* description)
		{
			m_y += 7;
			text({ m_left + 2, m_y, m_right, m_y + 36 }, title, F_PAGE_TITLE, &SettingsSkin::text);
			m_y += 36 + 6;
			text({ m_left + 2, m_y, m_right, m_y + 20 }, description, F_LABEL, &SettingsSkin::sub);
			m_y += 20 + 2;
		}

		void section(const wchar_t* title)
		{
			closeCard();
			m_y += 18;
			text({ m_left + 3, m_y, m_right - 3, m_y + 19 }, title, F_SECTION, &SettingsSkin::sub);
			m_y += 19 + 8;
			m_cardTop = m_y;
			m_inCard = true;
			m_rowIndex = 0;
		}

		void row(const wchar_t* label, const wchar_t* description = nullptr, bool stack = false)
		{
			flushRow();
			m_label = label;
			m_description = description ? description : L"";
			m_stack = stack;
			m_hasRow = true;
		}

		HWND radio(int id, const wchar_t* caption, bool group = false)
		{
			if (group) ++m_group;
			auto* state = new ControlState();
			state->kind = ControlKind::Radio;
			state->group = m_group;
			const int width = Skin().choiceSize + 8 + MeasureText(m_measure, F_LABEL, caption).cx + 2;
			return add(control(id, caption, state), width, 26);
		}

		HWND check(int id, const wchar_t* caption, bool keycap)
		{
			auto* state = new ControlState();
			state->kind = ControlKind::Check;
			state->keycap = keycap;
			const int textWidth = MeasureText(m_measure, keycap ? F_SMALL : F_LABEL, caption).cx +
				(keycap ? 14 : 2);
			return add(control(id, caption, state), Skin().choiceSize + 8 + textWidth, 26);
		}

		HWND toggle(int id)
		{
			auto* state = new ControlState();
			state->kind = ControlKind::Switch;
			return add(control(id, L"", state), Skin().switchWidth, Skin().switchHeight + 4);
		}

		HWND button(int id, const wchar_t* caption, ButtonTone tone = ButtonTone::Normal)
		{
			auto* state = new ControlState();
			state->kind = ControlKind::Button;
			state->tone = tone;
			const int width = (std::max)(64, static_cast<int>(MeasureText(m_measure, F_CONTROL, caption).cx) + 28);
			return add(control(id, caption, state), width, Skin().controlHeight);
		}

		// 仅用于 stack 行：每行 kTileColumns 张，超出自动换行。
		HWND tile(int id, const std::wstring& caption)
		{
			const SettingsSkin& s = Skin();
			auto* state = new ControlState();
			state->kind = ControlKind::Tile;
			const int rowWidth = m_right - m_left - s.rowPaddingLeft - s.rowPaddingRight;
			const int width = (rowWidth - (kTileColumns - 1) * s.controlGap) / kTileColumns;
			return add(control(id, caption.c_str(), state), width, kTileHeight);
		}

		// 标签栏独占一行，底部一条细线；紧随其后的行不再画分隔线。
		void tabs(int firstId, std::initializer_list<const wchar_t*> captions)
		{
			flushRow();
			const SettingsSkin& s = Skin();
			if (m_rowIndex > 0)
				m_layout.lines.push_back({ m_left + 1, m_y, m_right - 1, m_y + 1 });
			const int top = m_y + 6;
			int x = m_left + s.rowPaddingLeft;
			int id = firstId;
			for (const wchar_t* caption : captions)
			{
				auto* state = new ControlState();
				state->kind = ControlKind::Tab;
				const int width = static_cast<int>(MeasureText(m_measure, F_NAV_ACTIVE, caption).cx) + 8;
				position_window(control(id++, caption, state), nullptr, x, top, width, kTabHeight,
					SWP_NOZORDER | SWP_NOACTIVATE);
				x += width + kTabGap;
			}
			m_y = top + kTabHeight;
			m_layout.lines.push_back({ m_left + s.rowPaddingLeft, m_y, m_right - s.rowPaddingRight, m_y + 1 });
			m_rowIndex = 0;
		}

		HWND swatch(int id, const wchar_t* caption)
		{
			auto* state = new ControlState();
			state->kind = ControlKind::Swatch;
			// 按最长的三字标题统一宽度，色块按钮上下对齐。
			const int captionWidth = (std::max)(MeasureText(m_measure, F_CONTROL, caption).cx,
				MeasureText(m_measure, F_CONTROL, L"选中项").cx);
			const int width = 36 + captionWidth + 14 + MeasureText(m_measure, F_CONTROL, L"#888888").cx + 10;
			return add(control(id, caption, state), width, Skin().controlHeight);
		}

		// 仅用于 stack 行：占满整行宽度。
		HWND preview(int id, int height)
		{
			HWND c = create_scaled_window(0, kPreviewClass, L"", WS_CHILD | WS_VISIBLE,
				0, 0, 10, height, m_page, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
			m_pending.push_back({ c, 0, height, height, true, {} });
			return c;
		}

		HWND combo(int id, int width, std::initializer_list<const wchar_t*> items)
		{
			HWND c = create_scaled_window(0, L"COMBOBOX", nullptr,
				WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
				0, 0, width, 300, m_page, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
			SendMessageW(c, WM_SETFONT, (WPARAM)g_fonts[F_CONTROL], FALSE);
			SendMessageW(c, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), g_dpi.pixels(Skin().controlHeight - 6));
			SendMessageW(c, CB_SETITEMHEIGHT, 0, g_dpi.pixels(28));
			for (const wchar_t* item : items)
				SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)item);
			SetWindowSubclass(c, ComboProc, 1, 0);
			RECT bounds{};
			GetWindowRect(c, &bounds);
			Pending& pending = m_pending.emplace_back();
			pending = { c, width, g_dpi.logical(bounds.bottom - bounds.top), 300, false, {} };
			return c;
		}

        HWND password(int id, int width)
        {
            HWND c = create_scaled_window(0, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                0, 0, width, Skin().controlHeight, m_page, (HMENU)(INT_PTR)id,
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)g_fonts[F_CONTROL], FALSE);
            SendMessageW(c, EM_SETLIMITTEXT, 4096, 0);
            SendMessageW(c, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                MAKELPARAM(g_dpi.pixels(6), g_dpi.pixels(6)));
            SetWindowSubclass(c, PasswordProc, 1, 0);
            return add(c, width, Skin().controlHeight);
        }

		HWND slider(int id, int minimum, int maximum, std::initializer_list<const wchar_t*> scale)
		{
			HWND c = create_scaled_window(0, kSliderClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
				0, 0, 100, 24, m_page, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
			SendMessageW(c, TBM_SETRANGE, TRUE, MAKELPARAM(minimum, maximum));
			Pending& pending = m_pending.emplace_back();
			pending = { c, 0, 24, 24, true, std::vector<std::wstring>(scale.begin(), scale.end()) };
			return c;
		}

		void hero(const wchar_t* title, const std::wstring& subtitle, int buttonId, const wchar_t* caption)
		{
			closeCard();
			m_y += 20;
			constexpr int kPadding = 22;
			constexpr int kLogo = 58;
			const int height = kPadding * 2 + kLogo;
			const RECT card{ m_left, m_y, m_right, m_y + height };
			m_layout.cards.push_back({ card, true });
			m_layout.logo = { m_left + kPadding, m_y + kPadding, m_left + kPadding + kLogo, m_y + kPadding + kLogo };
			const int textLeft = m_layout.logo.right + 18;
			const int textTop = m_y + (height - 52) / 2;
			text({ textLeft, textTop, m_right - 160, textTop + 32 }, title, F_SIDEBAR_TITLE, &SettingsSkin::text);
			text({ textLeft, textTop + 32, m_right - 160, textTop + 52 }, subtitle, F_CONTROL, &SettingsSkin::sub);
			auto* state = new ControlState();
			state->kind = ControlKind::Button;
			state->tone = ButtonTone::Primary;
			const int width = static_cast<int>(MeasureText(m_measure, F_CONTROL, caption).cx) + 28;
			HWND c = control(buttonId, caption, state);
			position_window(c, nullptr, m_right - kPadding - width, m_y + (height - Skin().controlHeight) / 2,
				width, Skin().controlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
			m_y += height;
		}

		// 居中的分段切换，独占一行且不在卡片内。
		HWND segment(int id)
		{
			closeCard();
			m_y += 18;
			const int width = kSegmentWidth * 2 + 6;
			HWND c = create_scaled_window(0, kSegmentClass, L"", WS_CHILD | WS_VISIBLE,
				(m_left + m_right - width) / 2, m_y, width, kSegmentHeight, m_page, (HMENU)(INT_PTR)id,
				GetModuleHandleW(nullptr), nullptr);
			m_y += kSegmentHeight;
			return c;
		}

		// 独立卡片：上方示意图，下方一行说明。
		HWND illustration(int id, int height, const wchar_t* caption)
		{
			closeCard();
			m_y += 16;
			constexpr int kPadding = 16;
			const int top = m_y;
			HWND c = create_scaled_window(0, kIllustrationClass, L"", WS_CHILD | WS_VISIBLE,
				m_left + kPadding, top + kPadding, m_right - m_left - 2 * kPadding, height, m_page,
				(HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
			const int captionTop = top + kPadding + height + 12;
			text({ m_left + kPadding + 4, captionTop, m_right - kPadding, captionTop + 22 }, caption, F_LABEL,
				&SettingsSkin::text);
			m_y = captionTop + 22 + 14;
			m_layout.cards.push_back({ RECT{ m_left, top, m_right, m_y }, false });
			return c;
		}

		// 卡片外右对齐的按钮。
		HWND trailingButton(int id, const wchar_t* caption, ButtonTone tone)
		{
			closeCard();
			m_y += 14;
			auto* state = new ControlState();
			state->kind = ControlKind::Button;
			state->tone = tone;
			const int width = (std::max)(80, static_cast<int>(MeasureText(m_measure, F_CONTROL, caption).cx) + 36);
			HWND c = control(id, caption, state);
			position_window(c, nullptr, m_right - width, m_y, width, Skin().controlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
			m_y += Skin().controlHeight;
			return c;
		}

		// 无标题卡片。
		void card()
		{
			closeCard();
			m_y += 16;
			m_cardTop = m_y;
			m_inCard = true;
			m_rowIndex = 0;
		}

		// 卡片内占满宽度的列表，可带底部操作按钮。
		HWND list(int id, int height, const wchar_t* emptyText, int footerId = 0, const wchar_t* footerCaption = nullptr)
		{
			flushRow();
			if (!m_inCard) card();
			if (m_rowIndex > 0)
				m_layout.lines.push_back({ m_left + 1, m_y, m_right - 1, m_y + 1 });
			auto* state = new ListState();
			state->defaultEmptyText = state->emptyText = emptyText;
			HWND c = create_scaled_window(0, kListClass, L"", WS_CHILD | WS_VISIBLE,
				m_left + 4, m_y + 4, m_right - m_left - 8, height, m_page, (HMENU)(INT_PTR)id,
				GetModuleHandleW(nullptr), state);
			m_y += height + 8;
			if (footerId)
			{
				const SettingsSkin& s = Skin();
				m_layout.lines.push_back({ m_left + 1, m_y, m_right - 1, m_y + 1 });
				auto* button = new ControlState();
				button->kind = ControlKind::Button;
				const int width = (std::max)(64, static_cast<int>(MeasureText(m_measure, F_CONTROL, footerCaption).cx) + 28);
				position_window(control(footerId, footerCaption, button), nullptr, m_right - s.rowPaddingRight - width,
					m_y + 12, width, s.controlHeight, SWP_NOZORDER | SWP_NOACTIVATE);
				m_y += 12 + s.controlHeight + 12;
			}
			++m_rowIndex;
			return c;
		}

		int finish()
		{
			closeCard();
			m_y += 24 + Skin().workspacePadding;
			m_layout.height = m_y;
			return m_y;
		}

	private:
		struct Pending
		{
			HWND hwnd;
			int width;
			int height;
			int windowHeight;
			bool fill;
			std::vector<std::wstring> scale;
		};

		HWND control(int id, const wchar_t* caption, ControlState* state)
		{
			return create_scaled_window(0, kControlClass, caption, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
				0, 0, 10, 10, m_page, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), state);
		}

		HWND add(HWND hwnd, int width, int height)
		{
			m_pending.push_back({ hwnd, width, height, height, false, {} });
			return hwnd;
		}

		void text(const RECT& rect, const std::wstring& value, int font, SkinColor color,
			UINT format = DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS)
		{
			m_layout.texts.push_back({ rect, value, font, color, format });
		}

		void place(const Pending& pending, int x, int y, int width)
		{
			position_window(pending.hwnd, nullptr, x, y, width, pending.windowHeight,
				SWP_NOZORDER | SWP_NOACTIVATE);
		}

		void flushRow()
		{
			if (!m_hasRow) return;
			m_hasRow = false;
			const SettingsSkin& s = Skin();
			const int top = m_y;
			const bool described = !m_description.empty();
			const int infoHeight = described ? 40 : 20;
			const int gap = s.controlGap;
			int controlsHeight = 0;
			for (const Pending& pending : m_pending)
				controlsHeight = (std::max)(controlsHeight,
					pending.height + (pending.scale.empty() ? 0 : 20));
			if (m_rowIndex > 0)
				m_layout.lines.push_back({ m_left + 1, top, m_right - 1, top + 1 });

			const int labelLeft = m_left + s.rowPaddingLeft;
			int labelRight = m_right - s.rowPaddingRight;
			int infoTop = 0;
			int height = 0;
			if (!m_stack)
			{
				height = (std::max)(s.rowMinHeight, (std::max)(infoHeight, controlsHeight) + 2 * s.rowPaddingY);
				infoTop = top + (height - infoHeight) / 2;
				int x = m_right - s.rowPaddingRight;
				for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it)
				{
					x -= it->width;
					place(*it, x, top + (height - it->height) / 2, it->width);
					x -= gap;
				}
				if (!m_pending.empty()) labelRight = x + gap - 18;
			}
			else
			{
				infoTop = top + s.stackPaddingY;
				const int rowRight = m_right - s.rowPaddingRight;
				const int fillWidth = rowRight - labelLeft;
				int x = labelLeft;
				int y = infoTop + infoHeight + 11;
				int lineHeight = 0;
				for (const Pending& pending : m_pending)
				{
					const int width = pending.fill ? fillWidth : pending.width;
					if (x > labelLeft && x + width > rowRight)
					{
						x = labelLeft;
						y += lineHeight + kWrapGap;
						lineHeight = 0;
					}
					place(pending, x, y, width);
					lineHeight = (std::max)(lineHeight, pending.height + (pending.scale.empty() ? 0 : 20));
					const size_t count = pending.scale.size();
					for (size_t i = 0; i < count; ++i)
					{
						const int labelY = y + pending.height + 2;
						const int center = x + static_cast<int>(width * (count > 1 ? static_cast<float>(i) / (count - 1) : 0.0f));
						RECT rect{ center - 60, labelY, center + 60, labelY + 18 };
						UINT align = DT_CENTER;
						if (i == 0) { rect = { x + 2, labelY, x + 122, labelY + 18 }; align = DT_LEFT; }
						else if (i + 1 == count) { rect = { x + width - 122, labelY, x + width - 2, labelY + 18 }; align = DT_RIGHT; }
						text(rect, pending.scale[i], F_SMALL, &SettingsSkin::sub, align | DT_VCENTER);
					}
					x += width + gap;
				}
				height = y + lineHeight + s.stackPaddingY - top;
			}
			text({ labelLeft, infoTop, labelRight, infoTop + 20 }, m_label, F_LABEL, &SettingsSkin::text);
			if (described)
				text({ labelLeft, infoTop + 22, labelRight, infoTop + 40 }, m_description, F_DESC, &SettingsSkin::sub);
			m_pending.clear();
			m_y += height;
			++m_rowIndex;
		}

		void closeCard()
		{
			flushRow();
			if (!m_inCard) return;
			m_layout.cards.push_back({ RECT{ m_left, m_cardTop, m_right, m_y }, false });
			m_inCard = false;
		}

		HWND m_page;
		PageLayout& m_layout;
		HDC m_measure;
		int m_left = 0;
		int m_right = 0;
		int m_y = 0;
		int m_cardTop = 0;
		int m_rowIndex = 0;
		int m_group = 0;
		bool m_inCard = false;
		bool m_hasRow = false;
		bool m_stack = false;
		std::wstring m_label;
		std::wstring m_description;
		std::vector<Pending> m_pending;
	};

	int ViewportLeft()
	{
		const SettingsSkin& s = Skin();
		return s.workspacePadding * 2 + s.sidebarWidth - 2;
	}

	RECT ScrollTrack(HWND viewport)
	{
		const SettingsSkin& s = Skin();
		RECT client{};
		logical_client_rect(viewport, &client);
		const int left = client.right - s.scrollGutter + (s.scrollGutter - s.scrollbarWidth) / 2;
		return { left, s.workspacePadding, left + s.scrollbarWidth, client.bottom - s.workspacePadding };
	}

	RECT ScrollThumb(HWND viewport, int index, int scroll)
	{
		RECT client{};
		logical_client_rect(viewport, &client);
		const int content = g_layouts[index].height;
		const int visible = static_cast<int>(client.bottom);
		const int maxScroll = content - visible;
		if (maxScroll <= 0) return {};
		const RECT track = ScrollTrack(viewport);
		const int trackHeight = static_cast<int>(track.bottom - track.top);
		const int thumb = (std::max)(32, trackHeight * visible / content);
		const int top = track.top + (trackHeight - thumb) * scroll / maxScroll;
		return { track.left, top, track.right, top + thumb };
	}
}

SettingsSkin SettingsSkin::light()
{
	return {};
}

SettingsSkin SettingsSkin::dark()
{
	SettingsSkin skin;
	skin.window = 0xFF1F2023;
	skin.sidebar = 0xFF202C3C;
	skin.surface = 0xFF2A2B2F;
	skin.hover = 0xFF323338;
	skin.pressed = 0xFF38393F;
	skin.line = 0x1FFFFFFF;
	skin.lineStrong = 0x33FFFFFF;
	skin.lineHover = 0x4DFFFFFF;
	skin.text = 0xFFE9EAED;
	skin.sub = 0xFFA4A9B1;
	skin.muted = 0xFF7C828B;
	skin.accent = 0xFF4CA0E8;
	skin.accentHover = 0xFF63AEEE;
	skin.accentPressed = 0xFF3D8FD6;
	skin.link = 0xFF63AEEE;
	skin.linkHover = 0xFF8DC5F5;
	skin.linkPressed = 0xFF4CA0E8;
	skin.onAccent = 0xFF0E1A26;
	skin.choiceMark = skin.onAccent;
	skin.danger = 0xFFFF7B6E;
	skin.navHover = 0x0FFFFFFF;
	skin.navActive = 0xFF2A2B2F;
	skin.navShadow = 0x33000000;
	skin.cardShadow = 0x33000000;
	skin.switchBorder = 0xFF9AA0A8;
	skin.switchKnob = 0xFFC5C9CF;
	skin.sliderTrack = 0xFF45474D;
	skin.kbdFill = 0xFF25262A;
	skin.scrollThumb = 0xFF4A4D53;
	skin.scrollThumbHover = 0xFF63676E;
	skin.heroFrom = 0xFF2A2B2F;
	skin.heroTo = 0xFF1F2E3D;
	skin.darkCaption = true;
	return skin;
}

void SettingsWindow::registerClass()
{
	static bool registered = false;
	if (registered) return;
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = &SettingsWindow::wndProc;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_PINYIN));
	wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_PINYIN),
		IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
	wc.hbrBackground = nullptr;
	wc.lpszClassName = kWindowClass;
	RegisterClassExW(&wc);
	wc.hIcon = nullptr;
	wc.hIconSm = nullptr;
	wc.style = 0;
	wc.lpfnWndProc = &SettingsWindow::viewportProc;
	wc.lpszClassName = kViewportClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = &SettingsWindow::pageProc;
	wc.lpszClassName = kPageClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = ControlProc;
	wc.lpszClassName = kControlClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = SliderProc;
	wc.lpszClassName = kSliderClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = PreviewProc;
	wc.lpszClassName = kPreviewClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = SegmentProc;
	wc.lpszClassName = kSegmentClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = IllustrationProc;
	wc.lpszClassName = kIllustrationClass;
	RegisterClassExW(&wc);
	wc.lpfnWndProc = ListProc;
	wc.lpszClassName = kListClass;
	RegisterClassExW(&wc);
	registered = true;
}

void SettingsWindow::bringToTopAndActivate()
{
	if (!s_hwnd)
		return;
	// 临时置顶再取消置顶，确保弹出时盖在所有窗口之上，但不保持 always-on-top
	position_window(s_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
	position_window(s_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
	SetForegroundWindow(s_hwnd);
}

void SettingsWindow::showInstance()
{
	DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	registerClass();
	if (s_hwnd) {
		loadAllPages();
		ShowWindow(s_hwnd, SW_SHOWNORMAL);
		bringToTopAndActivate();
		return;
	}
	g_dpi = DpiScale(GetDpiForSystem());
	CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, g_dpi.pixels(WINDOW_W), g_dpi.pixels(WINDOW_H),
		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
	if (s_hwnd) {
		ShowWindow(s_hwnd, SW_SHOWNORMAL);
	}
	bringToTopAndActivate();
}

void SettingsWindow::showAboutPage()
{
	showInstance();
	if (s_hwnd)
		switchPage(PAGE_COUNT - 1); // 导航最后一页：关于
}

void SettingsWindow::closeInstance()
{
	if (s_hwnd)
		PostMessageW(s_hwnd, WM_CLOSE, 0, 0);
}

const SettingsSkin& SettingsWindow::skin()
{
    return s_skin;
}

void SettingsWindow::setSkin(const SettingsSkin& skin)
{
	DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	FuzzyPinyinDialog::closeInstance();
	LexiconImportDialog::closeInstance();
	AddPhraseDialog::close();
	s_skin = skin;
	if (!s_hwnd) return;
	destroyPages();
	destroyFonts();
	createFonts();
	createControls();
	loadAllPages();
	switchPage(s_currentPage);
	invalidate_logical_rect(s_hwnd, nullptr, TRUE);
}

void SettingsWindow::createFonts()
{
	struct Spec { int pixels; int weight; };
	constexpr Spec specs[F_COUNT] = {
		{ 28, FW_BOLD },     // F_PAGE_TITLE（页面标题）
		{ 22, FW_BOLD },     // F_SIDEBAR_TITLE（侧边栏标题）
		{ 15, FW_NORMAL },   // F_NAV
		{ 15, FW_BOLD },     // F_NAV_ACTIVE（导航选中态）
		{ 14, FW_NORMAL },   // F_LABEL
		{ 13, FW_NORMAL },   // F_CONTROL
		{ 13, FW_BOLD },     // F_SECTION
		{ 12, FW_NORMAL },   // F_DESC
		{ 12, FW_NORMAL },   // F_SMALL
		{ 13, FW_NORMAL },   // F_LINK
	};
	for (int i = 0; i < F_COUNT; ++i)
		g_fonts[i] = CreateFontW(-g_dpi.pixels(specs[i].pixels), 0, 0, 0, specs[i].weight, FALSE, i == F_LINK, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, s_skin.fontName.c_str());
	g_surfaceBrush = CreateSolidBrush(ToColorRef(s_skin.surface));
	if (s_hwnd)
	{
		const COLORREF caption = ToColorRef(s_skin.window);
		DwmSetWindowAttribute(s_hwnd, 35 /*  DWMWA_CAPTION_COLOR（标题栏颜色） */, &caption, sizeof(caption));
		const BOOL dark = s_skin.darkCaption ? TRUE : FALSE;
		DwmSetWindowAttribute(s_hwnd, 20 /*  DWMWA_USE_IMMERSIVE_DARK_MODE（沉浸式深色模式） */, &dark, sizeof(dark));
	}
}

void SettingsWindow::destroyFonts()
{
	for (HFONT& font : g_fonts)
		if (font) { DeleteObject(font); font = nullptr; }
	if (g_surfaceBrush) { DeleteObject(g_surfaceBrush); g_surfaceBrush = nullptr; }
}

LRESULT CALLBACK SettingsWindow::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case kRebuildAppearance:
		rebuildPage((int)SettingsEnum::SettingType::appearance);
		return 0;
	case kRebuildPhrase:
		rebuildPage((int)SettingsEnum::SettingType::phrase);
		return 0;
	case kListContext:
		showListMenu(LOWORD(wParam), HIWORD(wParam), { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		return 0;
	case WM_CREATE:
		s_hwnd = hwnd;
		g_dpi = DpiScale(GetDpiForWindow(hwnd));
		// 在构建页面前先确定尺寸：打开时所在的显示器可能与
		// GetDpiForSystem 使用的主显示器 DPI 不同。
		position_window(hwnd, nullptr, 0, 0, WINDOW_W, WINDOW_H,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		g_skinTab = -1;
		g_phraseTab = 0;
		createFonts();
		createControls();
		loadAllPages();
		switchPage(0);
		return 0;
	case WM_DESTROY:
		destroyPages();
		destroyFonts();
		s_hwnd = nullptr;
		s_currentPage = 0;
		s_navHover = -1;
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_SIZE:
		layoutPages();
		FuzzyPinyinDialog::resizeToParent();
		LexiconImportDialog::resizeToParent();
		return 0;
	case WM_DPICHANGED: {
		const int current = s_currentPage;
		int scroll[PAGE_COUNT];
		std::copy(std::begin(s_scroll), std::end(s_scroll), scroll);
		FuzzyPinyinDialog::closeInstance();
		LexiconImportDialog::closeInstance();
		AddPhraseDialog::close();
		destroyPages();
		destroyFonts();
		g_dpi = DpiScale(LOWORD(wParam));
		const RECT& suggested = *reinterpret_cast<const RECT*>(lParam);
		::SetWindowPos(hwnd, nullptr, suggested.left, suggested.top,
			suggested.right - suggested.left, suggested.bottom - suggested.top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		createFonts();
		createControls();
		loadAllPages();
		switchPage(current);
		for (int i = 0; i < PAGE_COUNT; ++i) scrollPage(i, scroll[i]);
		invalidate_logical_rect(hwnd, nullptr, FALSE);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(hwnd, &ps);
		paintSidebar(dc);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_PRINTCLIENT:
		paintSidebar(reinterpret_cast<HDC>(wParam));
		return 0;
	case WM_MOUSEMOVE: {
		const int hover = navHitTest({ g_dpi.logical(GET_X_LPARAM(lParam)), g_dpi.logical(GET_Y_LPARAM(lParam)) });
		if (hover != s_navHover) {
			s_navHover = hover;
			TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&track);
			RECT sidebar{ 0, 0, ViewportLeft(), 0 };
			RECT client; logical_client_rect(hwnd, &client);
			sidebar.bottom = client.bottom;
			invalidate_logical_rect(hwnd, &sidebar, FALSE);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		if (s_navHover >= 0) {
			s_navHover = -1;
			invalidate_logical_rect(hwnd, nullptr, FALSE);
		}
		return 0;
	case WM_SETCURSOR:
		if (reinterpret_cast<HWND>(wParam) == hwnd && LOWORD(lParam) == HTCLIENT) {
			POINT point; GetCursorPos(&point); screen_to_logical_client(hwnd, &point);
			SetCursor(LoadCursorW(nullptr, navHitTest(point) >= 0 ? IDC_HAND : IDC_ARROW));
			return TRUE;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	case WM_LBUTTONDOWN: {
		const int index = navHitTest({ g_dpi.logical(GET_X_LPARAM(lParam)), g_dpi.logical(GET_Y_LPARAM(lParam)) });
		if (index >= 0) switchPage(index);
		SetFocus(hwnd);
		return 0;
	}
	case WM_MOUSEWHEEL:
		if (s_viewports[s_currentPage])
			return SendMessageW(s_viewports[s_currentPage], msg, wParam, lParam);
		return 0;
	case WM_COMMAND:
		onCommand(wParam, lParam);
		return 0;
	case WM_HSCROLL:
		onScroll(wParam, lParam);
		return 0;
	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

namespace {
constexpr int kSpeechPageIndex = static_cast<int>(SettingsEnum::SettingType::speech);
bool speech_nav_visible() { return coral::speech::win32::kSpeechFeatureEnabled; }
int nav_page_count() { return speech_nav_visible() ? PAGE_COUNT : PAGE_COUNT - 1; }
int nav_page_index(int ordinal) {
	return (!speech_nav_visible() && ordinal >= kSpeechPageIndex) ? ordinal + 1 : ordinal;
}
int nav_ordinal(int page) {
	if (!speech_nav_visible() && page == kSpeechPageIndex) return -1;
	return (!speech_nav_visible() && page > kSpeechPageIndex) ? page - 1 : page;
}
}

RECT SettingsWindow::navRect(int index)
{
	const int ordinal = nav_ordinal(index);
	if (ordinal < 0) return {};
	const SettingsSkin& s = s_skin;
	const int top = s.workspacePadding + 77 + ordinal * (s.navHeight + s.navGap);
	return { s.workspacePadding, top, s.workspacePadding + s.sidebarWidth, top + s.navHeight };
}

int SettingsWindow::navHitTest(POINT point)
{
	for (int ordinal = 0; ordinal < nav_page_count(); ++ordinal) {
		const int page = nav_page_index(ordinal);
		const RECT rect = navRect(page);
		if (PtInRect(&rect, point)) return page;
	}
	return -1;
}

void SettingsWindow::paintSidebar(HDC dc)
{
	const SettingsSkin& s = s_skin;
	RECT client; logical_client_rect(s_hwnd, &client);
	DpiCanvas canvas(ViewportLeft(), client.bottom, g_dpi);
	if (!canvas.valid()) return;
	Surface surface = canvas.surface();
	surface.fill(s.sidebar);
	FillRoundRect(surface, { static_cast<float>(ViewportLeft() - 1), 0.0f,
		static_cast<float>(ViewportLeft()), static_cast<float>(client.bottom) }, 0.0f, s.line);
	const int left = s.workspacePadding + 13;
	for (int ordinal = 0; ordinal < nav_page_count(); ++ordinal) {
		const int i = nav_page_index(ordinal);
		const RECT item = navRect(i);
		const RectF rect = ToRectF(item);
		const float radius = static_cast<float>(s.navRadius);
		if (i == s_currentPage) {
			DrawShadow(surface, rect, radius, { 1.0f, 2.0f, 0.0f, s.navShadow }, 1.0f);
			FillRoundRect(surface, rect, radius, s.navActive);
			FillRoundRect(surface, { rect.left + 2.0f, rect.top + 12.0f, rect.left + 5.0f,
				rect.bottom - 12.0f }, 1.5f, s.accent);
		} else if (i == s_navHover) {
			FillRoundRect(surface, rect, radius, s.navHover);
		}
		const float icon = static_cast<float>(s.navIconSize);
		const bool strong = i == s_currentPage || i == s_navHover;
		StrokeSvgIcon(surface, kNavIcons[i].paths, kNavIcons[i].count,
			static_cast<float>(item.left + 13), rect.top + (rect.bottom - rect.top - icon) * 0.5f,
			icon, 24.0f, s.navIconStroke, strong ? s.text : s.sub);
	}
	canvas.beginText();
	HDC canvasDc = canvas.dc();
	const int headingTop = s.workspacePadding + 11;
	DrawTextIn(canvasDc, L"设置", { left, headingTop, ViewportLeft(), headingTop + 30 },
		F_SIDEBAR_TITLE, s.text, DT_LEFT | DT_VCENTER);
	DrawTextIn(canvasDc, L"个性化你的输入体验", { left, headingTop + 32, ViewportLeft(), headingTop + 51 },
		F_CONTROL, s.sub, DT_LEFT | DT_VCENTER);
	for (int ordinal = 0; ordinal < nav_page_count(); ++ordinal) {
		const int i = nav_page_index(ordinal);
		RECT item = navRect(i);
		item.left += 13 + s.navIconSize + 12;
		item.right -= 13;
		const bool active = i == s_currentPage;
		DrawTextIn(canvasDc, NAV_TITLES[i], item, active ? F_NAV_ACTIVE : F_NAV,
			active || i == s_navHover ? s.text : s.sub, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
	}
	const int footerBottom = client.bottom - s.workspacePadding - 2;
	DrawTextIn(canvasDc, L"CORAL Pinyin · " + g_version,
		{ left, footerBottom - 18, ViewportLeft(), footerBottom }, F_SMALL, s.muted, DT_LEFT | DT_VCENTER);
	canvas.endText();
	canvas.blit(dc, 0, 0);
}

LRESULT CALLBACK SettingsWindow::viewportProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const int index = static_cast<int>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	switch (msg) {
	case WM_NCCREATE: {
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		break;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	case WM_PRINTCLIENT: {
		PAINTSTRUCT ps{};
		HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wParam);
		RECT client; logical_client_rect(hwnd, &client);
		const int gutterLeft = client.right - s_skin.scrollGutter;
		DpiCanvas canvas(s_skin.scrollGutter, client.bottom, g_dpi);
		if (canvas.valid()) {
			Surface surface = canvas.surface();
			surface.fill(s_skin.window);
			RECT thumb = ScrollThumb(hwnd, index, s_scroll[index]);
			if (!IsRectEmpty(&thumb)) {
				OffsetRect(&thumb, -gutterLeft, 0);
				const bool active = g_scrollDrag.page == index || g_scrollDrag.hover;
				const float width = static_cast<float>(thumb.right - thumb.left);
				FillRoundRect(surface, ToRectF(thumb), width * 0.5f,
					active ? s_skin.scrollThumbHover : s_skin.scrollThumb);
			}
			canvas.blit(dc, gutterLeft, 0);
		}
		if (msg == WM_PAINT) EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_MOUSEWHEEL:
		scrollPage(index, s_scroll[index] - GET_WHEEL_DELTA_WPARAM(wParam) * kWheelStep / WHEEL_DELTA);
		return 0;
	case WM_MOUSEMOVE: {
		const POINT point{ g_dpi.logical(GET_X_LPARAM(lParam)), g_dpi.logical(GET_Y_LPARAM(lParam)) };
		if (g_scrollDrag.page == index) {
			RECT client; logical_client_rect(hwnd, &client);
			const RECT track = ScrollTrack(hwnd);
			const RECT thumb = ScrollThumb(hwnd, index, s_scroll[index]);
			const int travel = (track.bottom - track.top) - (thumb.bottom - thumb.top);
			const int maxScroll = g_layouts[index].height - client.bottom;
			if (travel > 0)
				scrollPage(index, (point.y - g_scrollDrag.grab - track.top) * maxScroll / travel);
			return 0;
		}
		const RECT thumb = ScrollThumb(hwnd, index, s_scroll[index]);
		const bool hover = PtInRect(&thumb, point) != FALSE;
		if (hover != g_scrollDrag.hover) {
			g_scrollDrag.hover = hover;
			TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
			TrackMouseEvent(&track);
			invalidate_logical_rect(hwnd, nullptr, FALSE);
		}
		return 0;
	}
	case WM_MOUSELEAVE:
		if (g_scrollDrag.hover) { g_scrollDrag.hover = false; invalidate_logical_rect(hwnd, nullptr, FALSE); }
		return 0;
	case WM_LBUTTONDOWN: {
		const POINT point{ g_dpi.logical(GET_X_LPARAM(lParam)), g_dpi.logical(GET_Y_LPARAM(lParam)) };
		const RECT thumb = ScrollThumb(hwnd, index, s_scroll[index]);
		RECT client; logical_client_rect(hwnd, &client);
		if (IsRectEmpty(&thumb) || point.x < client.right - s_skin.scrollGutter) return 0;
		if (!PtInRect(&thumb, point))
			scrollPage(index, s_scroll[index] + (point.y < thumb.top ? -client.bottom : client.bottom) * 4 / 5);
		const RECT moved = ScrollThumb(hwnd, index, s_scroll[index]);
		g_scrollDrag.page = index;
		g_scrollDrag.grab = (std::clamp)(static_cast<int>(point.y - moved.top), 0,
			static_cast<int>(moved.bottom - moved.top));
		SetCapture(hwnd);
		invalidate_logical_rect(hwnd, nullptr, FALSE);
		return 0;
	}
	case WM_LBUTTONUP:
		if (g_scrollDrag.page == index) ReleaseCapture();
		return 0;
	case WM_CAPTURECHANGED:
		if (g_scrollDrag.page == index) { g_scrollDrag.page = -1; invalidate_logical_rect(hwnd, nullptr, FALSE); }
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK SettingsWindow::pageProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_NCCREATE: {
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
		break;
	}
	case WM_COMMAND:
	case WM_HSCROLL:
		return SendMessageW(s_hwnd, msg, wParam, lParam);
	case WM_MOUSEWHEEL:
		return SendMessageW(GetParent(hwnd), msg, wParam, lParam);
	case WM_CTLCOLOREDIT:
	case WM_CTLCOLORLISTBOX:
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
		SetBkColor((HDC)wParam, ToColorRef(s_skin.surface));
		SetTextColor((HDC)wParam, ToColorRef(s_skin.text));
		return (LRESULT)g_surfaceBrush;
	case WM_LBUTTONDOWN:
		SetFocus(hwnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	case WM_PRINTCLIENT: {
		PAINTSTRUCT ps{};
		HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wParam);
		if (msg == WM_PRINTCLIENT) GetClientRect(hwnd, &ps.rcPaint);
		DpiCanvas* cache = PageCache(hwnd);
		if (cache && cache->valid()) {
			const RECT& r = ps.rcPaint;
			BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, cache->dc(), r.left, r.top, SRCCOPY);
		}
		if (msg == WM_PAINT) EndPaint(hwnd, &ps);
		return 0;
	}
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void SettingsWindow::createControls()
{
	HINSTANCE inst = GetModuleHandleW(nullptr);
	g_version = getVersionString();
	RECT client; logical_client_rect(s_hwnd, &client);
	const int viewportWidth = client.right - ViewportLeft();
	const int pageWidth = viewportWidth - s_skin.scrollGutter;
	for (int i = 0; i < PAGE_COUNT; ++i) {
		s_viewports[i] = create_scaled_window(WS_EX_CONTROLPARENT, kViewportClass, nullptr,
			WS_CHILD | WS_CLIPCHILDREN, ViewportLeft(), 0, viewportWidth, client.bottom,
			s_hwnd, nullptr, inst, reinterpret_cast<void*>(static_cast<INT_PTR>(i)));
		s_pages[i] = create_scaled_window(WS_EX_CONTROLPARENT, kPageClass, nullptr,
			WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, pageWidth, client.bottom,
			s_viewports[i], nullptr, inst, reinterpret_cast<void*>(static_cast<INT_PTR>(i)));
		s_scroll[i] = 0;
	}

	createRoutinePage();
	createKeyboardPage();
	createAppearancePage();
	createLexiconPage();
	createPhrasePage();
	createSeniorPage();
    createSpeechPage();
	createAboutPage();

	for (int i = 0; i < PAGE_COUNT; ++i)
		position_window(s_pages[i], nullptr, 0, 0, pageWidth,
			(std::max)(static_cast<int>(client.bottom), g_layouts[i].height),
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
}

void SettingsWindow::destroyPages()
{
	for (int i = 0; i < PAGE_COUNT; ++i) {
		if (s_viewports[i]) DestroyWindow(s_viewports[i]);
		s_viewports[i] = nullptr;
		s_pages[i] = nullptr;
		s_scroll[i] = 0;
		g_layouts[i] = PageLayout();
	}
	g_scrollDrag = ScrollDrag();
	s_schemeNames.clear();
}

void SettingsWindow::layoutPages()
{
	RECT client; logical_client_rect(s_hwnd, &client);
	for (int i = 0; i < PAGE_COUNT; ++i) {
		if (!s_viewports[i]) continue;
		move_window(s_viewports[i], ViewportLeft(), 0, client.right - ViewportLeft(), client.bottom, TRUE);
		scrollPage(i, s_scroll[i]);
	}
}

void SettingsWindow::scrollPage(int index, int offset)
{
	HWND viewport = s_viewports[index];
	if (!viewport) return;
	RECT client; logical_client_rect(viewport, &client);
	const int maxScroll = (std::max)(0, g_layouts[index].height - static_cast<int>(client.bottom));
	const int value = (std::clamp)(offset, 0, maxScroll);
	if (value == s_scroll[index]) return;
	s_scroll[index] = value;
	position_window(s_pages[index], nullptr, 0, -value, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	RECT gutter{ client.right - s_skin.scrollGutter, 0, client.right, client.bottom };
	invalidate_logical_rect(viewport, &gutter, FALSE);
	UpdateWindow(viewport);
}

void SettingsWindow::createRoutinePage()
{
	const int index = (int)SettingsEnum::SettingType::routine;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.section(L"输入模式");
	b.row(L"拼音方案", L"选择全拼或双拼输入方式");
	b.radio(ID_R_QUAN, L"全拼", true);
	b.radio(ID_R_SHUANG, L"双拼");
	b.row(L"双拼方案", L"仅在启用双拼时生效");
	HWND scheme = b.combo(ID_R_SCHEME, 160, {});
	for (const auto& [name, title] : enumDoublepinyinSchemes()) {
		s_schemeNames.push_back(name);
		SendMessageW(scheme, CB_ADDSTRING, 0, (LPARAM)title.c_str());
	}
	b.row(L"默认语言");
	b.radio(ID_R_DEFZH, L"中文", true);
	b.radio(ID_R_DEFEN, L"英文");
	b.row(L"汉字形式");
	b.radio(ID_R_JIAN, L"简体", true);
	b.radio(ID_R_FAN, L"繁体");

	b.section(L"字符与标点");
	b.row(L"字符宽度");
	b.radio(ID_R_BANJIAO, L"半角", true);
	b.radio(ID_R_QUANJIAO, L"全角");
	b.row(L"中文模式下使用英文标点");
	b.toggle(ID_R_ENPUNCT);
	b.row(L"拼音纠错", L"自动修正常见的拼音输入错误");
	b.button(ID_R_FUZZY, L"模糊音设置");
	b.toggle(ID_R_CORRECT);

	b.section(L"输入行为");
	b.row(L"符号自动配对补全");
	b.toggle(ID_L_AUTOSYMBOL);
	b.row(L"数字后输入英文符号");
	b.toggle(ID_L_DIGITSYMBOL);
	b.row(L"字母后输入英文符号");
	b.toggle(ID_L_LETTERSYMBOL);
	b.finish();
}

void SettingsWindow::createKeyboardPage()
{
	const int index = (int)SettingsEnum::SettingType::keyboard;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.section(L"切换中英文");
	b.row(L"Ctrl + Space");
	b.toggle(ID_K_CS);
	b.row(L"Shift", L"短按 Shift 切换输入语言");
	b.toggle(ID_K_SHIFT);
	b.row(L"Ctrl");
	b.toggle(ID_K_CTRL);

	b.section(L"其他快捷键");
	b.row(L"切换全角与半角");
	b.radio(ID_K_WIDTHON, L"Shift + Space", true);
	b.radio(ID_K_WIDTHOFF, L"不使用");
	b.row(L"切换中英文标点");
	b.radio(ID_K_PUNCTON, L"Ctrl + .", true);
	b.radio(ID_K_PUNCTOFF, L"不使用");
	b.row(L"快捷键切换简繁体");
	b.combo(ID_K_SIMTRAKEY, 128, { L"Ctrl+Shift+F", L"Ctrl+Shift+G" });
	b.toggle(ID_K_SIMTRA);
	b.row(L"候选词翻页", L"选择可用于翻页的按键组合", true);
	b.check(ID_K_FLIPPAGE, L"PageUp / PageDown", true);
	b.check(ID_K_FLIPMINUS, L"- / +", true);
	b.check(ID_K_FLIPBRACE, L"[ / ]", true);
	b.check(ID_K_FLIPCOMMA, L", / .", true);
	b.row(L"按;快捷选中第二候选词，按'快捷选中第三候选词");
	b.toggle(ID_K_SEM);
	b.row(L"以词定字 按快捷键输出选中词的收尾文字");
	b.combo(ID_K_WORD2CHAR, 128, { L"不使用", L"- / +", L"[ / ]" });
	b.finish();
}

void SettingsWindow::createAppearancePage()
{
	const int index = (int)SettingsEnum::SettingType::appearance;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.section(L"候选窗口");
	b.row(L"候选词字体");
	b.combo(ID_A_FONT, 220, {});
	b.row(L"添加系统字体", L"将已安装的系统字体加入上方列表");
	b.button(ID_A_ADDFONT, L"添加");
	b.row(L"候选词大小", L"同时调整候选窗口和文字大小", true);
	b.slider(ID_A_SIZE, 0, 6, { L"最小", L"默认", L"最大" });

	using namespace coral::win32;
	b.section(L"皮肤");
	b.row(L"效果预览", L"上方为收起状态，下方为展开状态，调整会实时显示在这里", true);
	AcceptImageDrops(b.preview(ID_A_PREVIEW, kPreviewHeight));
	b.row(L"颜色模式", L"每套主题都提供浅色与深色两种配色");
	b.combo(ID_A_THEME, 128, { L"浅色", L"深色", L"跟随系统" });

	if (g_skinTab < 0)
		g_skinTab = static_cast<int>(CandidateThemeConfig::parse(sConfig->getCandidateSkin()).kind);
	b.tabs(ID_A_TAB, { L"纯色主题", L"渐变主题", L"图片主题" });
	switch (TabKind()) {
	case CandidateBackground::Solid:
		b.row(L"预设主题", nullptr, true);
		b.tile(ID_A_SOLID + kCandidateDefaultSolidPreset,
			CandidateSolidPresetAt(kCandidateDefaultSolidPreset).name);
		for (int i = 0; i < kCandidateSolidPresetCount; ++i)
			if (i != kCandidateDefaultSolidPreset)
				b.tile(ID_A_SOLID + i, CandidateSolidPresetAt(i).name);
		b.row(L"文字颜色", L"修改只作用于当前颜色模式");
		b.swatch(ID_A_COLOR_TEXT, L"普通");
		b.swatch(ID_A_COLOR_SELTEXT, L"选中");
		b.row(L"背景颜色");
		b.swatch(ID_A_COLOR_BG, L"弹窗");
		b.swatch(ID_A_COLOR_SELBG, L"选中项");
		break;
	case CandidateBackground::Gradient:
		b.row(L"预设主题", nullptr, true);
		for (int i = 0; i < kCandidateGradientPresetCount; ++i)
			b.tile(ID_A_GRADIENT + i, CandidateGradientPresetAt(i).name);
		b.tile(ID_A_GRADIENT + kCandidateGradientPresetCount, L"自定义");
		b.row(L"文字颜色", L"修改只作用于当前颜色模式");
		b.swatch(ID_A_COLOR_TEXT, L"普通");
		b.swatch(ID_A_COLOR_SELTEXT, L"选中");
		b.row(L"选中项颜色");
		b.swatch(ID_A_COLOR_SELBG, L"选中项");
		b.row(L"渐变背景色", L"修改后切换为自定义渐变");
		b.swatch(ID_A_COLOR_FROM, L"起始");
		b.swatch(ID_A_COLOR_TO, L"结束");
		break;
	case CandidateBackground::Image: {
		const CandidateSkin defaults{};
		const SIZE need = defaults.imageSize();
		wchar_t imageHint[160] = {};
		swprintf_s(imageHint, L"支持 JPG/PNG，不超过 %d MB，至少 %d×%d 像素；也可以把图片拖到这里或预览上",
			static_cast<int>(kCandidateImageMaxBytes / (1024 * 1024)), need.cx, need.cy);
		b.row(L"预设主题", imageHint, true);
		for (int i = 0; i < kCandidateImagePresetCount; ++i)
			b.tile(ID_A_IMAGE + i, CandidateImagePresetAt(i).name);
		AcceptImageDrops(b.tile(kCustomImageTile, L"自定义"));
		b.row(L"图片透明度", L"向右拖动让背景图变淡，候选文字保持清晰", true);
		b.slider(ID_A_IMAGEOPACITY, 0, 100, { L"原图", L"完全透明" });
		b.row(L"文字颜色", L"修改只作用于当前颜色模式");
		b.swatch(ID_A_COLOR_TEXT, L"普通");
		b.swatch(ID_A_COLOR_SELTEXT, L"选中");
		b.row(L"背景颜色");
		b.swatch(ID_A_COLOR_SELBG, L"选中项");
		break;
	}
	}

	b.section(L"透明度");
	b.row(L"背景透明度", L"数值越小，背景越通透；文字、按钮和滚动条保持清晰", true);
	b.slider(ID_A_OPACITY, kCandidateOpacityMin, 100, { L"30%", L"65%", L"100%" });
	b.finish();
}

void SettingsWindow::createLexiconPage()
{
	const int index = (int)SettingsEnum::SettingType::lexicon;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.section(L"用户词库");
	b.row(L"导入或导出", L"备份或迁移你的用户词库");
	b.button(ID_L_IMPORT, L"导入词库");
	b.button(ID_L_EXPORT, L"导出词库");
	b.row(L"清除用户词库", L"此操作不可恢复，不影响系统词库");
	b.button(ID_L_CLEAR, L"清除全部用户词库", ButtonTone::Danger);

	b.finish();
}

void SettingsWindow::createPhrasePage()
{
	const int index = (int)SettingsEnum::SettingType::phrase;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.segment(ID_P_SEGMENT);
	if (g_phraseTab == 0) {
		b.illustration(ID_P_ILLUSTRATION, kIllustrationHeight,
			L"添加文字到「常用语」后，输入前 3 个字或其拼音首字母即可使用");
		b.trailingButton(ID_P_ADD, L"添加", ButtonTone::Primary);
		b.section(L"已添加的常用语");
		b.list(ID_P_PHRASES, kListRow * 6, L"还没有常用语，点击「添加」创建；右键条目可编辑或删除");
	} else {
		b.card();
		b.row(L"剪贴板", L"按 v 后可以直接选中打开剪贴板，剪贴板将保留 3 个月内复制的内容");
		b.toggle(ID_P_CLIPSWITCH);
		b.card();
		b.list(ID_P_CLIPS, kListRow * 8, L"暂无剪贴板记录，复制的文字会出现在这里",
			ID_P_CLEARCLIPS, L"清空剪贴板");
	}
	b.finish();
}

void SettingsWindow::createSeniorPage()
{
	const int index = (int)SettingsEnum::SettingType::senior;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.section(L"高级模式");
    b.row(L"智能预测", L"结合连续输入的中文推荐候选；切换窗口后清空上下文");
    b.toggle(ID_S_PREDICTION);
	b.row(L"U 模式", L"笔画/拆字查字，uu 输入分类符号；双拼用 Shift+U 进入");
	b.toggle(ID_S_UMODE);
    b.button(ID_S_UHELP, L"使用方法");
	b.row(L"V 模式", L"数字、金额、日期、时间和计算；双拼用 Shift+V 进入");
	b.toggle(ID_S_VMODE);
    b.button(ID_S_VHELP, L"使用方法");
    b.section(L"翻译");
    b.row(L"启用翻译", L"输入后按翻译快捷键翻译当前候选，方向键选择，回车输出");
    b.toggle(ID_S_TRANSLATION);
    b.row(L"翻译快捷键", L"默认 =；开启 - / + 翻页时，请改用 F1–F12");
    HWND shortcut = b.combo(ID_S_SHORTCUT, 128, {});
    for (const auto& option : coral::translation::kShortcutOptions)
        SendMessageW(shortcut, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label));
    b.row(L"目标语言", L"原文固定为中文");
    HWND target = b.combo(ID_S_TARGET, 180, {});
    for (const auto& language : coral::translation::kTargetLanguages)
        SendMessageW(target, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(language.label));
    b.row(L"AccessKey ID", L"阿里云机器翻译凭据，仅在本机加密保存", true);
    b.password(ID_S_KEYID, 430);
    b.button(ID_S_TRANSLATION_HELP, L"开通教程", ButtonTone::Link);
    b.row(L"AccessKey Secret", nullptr, true);
    b.password(ID_S_SECRET, 330);
    b.button(ID_S_SAVEKEY, L"保存凭据");
	b.finish();
}

void SettingsWindow::createAboutPage()
{
	const int index = (int)SettingsEnum::SettingType::about;
	RECT bounds; logical_client_rect(s_pages[index], &bounds);
	PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
	b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
	b.hero(L"CORAL 拼音", L"版本 " + g_version, ID_AB_CHECKUPDATE, L"检查更新");
	b.section(L"更新检查");
	b.row(L"自动检查更新", L"每天检查一次，发现新版本时提醒；点击更新后下载并启动安装程序。", true);
	b.toggle(ID_AB_AUTOCHECK);
	b.section(L"法律信息");
	b.row(L"用户协议");
	b.button(ID_AB_EULA, L"查看", ButtonTone::Link);
	b.row(L"隐私政策");
	b.button(ID_AB_PRIVACY, L"查看", ButtonTone::Link);
	b.finish();
}

void SettingsWindow::createSpeechPage()
{
    const int index = (int)SettingsEnum::SettingType::speech;
    RECT bounds; logical_client_rect(s_pages[index], &bounds);
    PageBuilder b(s_pages[index], g_layouts[index], bounds.right);
    b.header(NAV_TITLES[index], PAGE_DESCRIPTIONS[index]);
    b.section(L"语音输入");
    b.row(L"启用语音输入", L"开启并配置凭据后，可从快捷键或悬浮栏开始录音");
    b.toggle(ID_V_ENABLED);
    b.row(L"SecretId（AccessKey）", L"腾讯云语音识别凭据，仅在本机加密保存", true);
    b.password(ID_V_KEYID, 430);
    SendDlgItemMessageW(s_pages[index], ID_V_KEYID, EM_SETLIMITTEXT, 512, 0);
    b.row(L"SecretKey", nullptr, true);
    b.password(ID_V_SECRET, 330);
    SendDlgItemMessageW(s_pages[index], ID_V_SECRET, EM_SETLIMITTEXT, 512, 0);
    b.button(ID_V_SAVEKEY, L"保存凭据");
    b.section(L"按下发言");
    b.row(L"按住 Ctrl + Win 发言", L"按下开始收集音频，松开结束并复用已有识别流程；开启后原快捷键暂停");
    b.toggle(ID_V_PUSHTOTALK);
    b.section(L"唤起方式");
    b.row(L"语音输入快捷键", L"未开启“按下发言”时，松开快捷键打开小窗并开始录音", true);
    HWND shortcut = b.combo(ID_V_SHORTCUT, 240, {});
    for (const auto& option : coral::speech::kSpeechShortcuts)
        SendMessageW(shortcut, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.label));
    b.section(L"麦克风");
    b.row(L"检测麦克风", L"检测默认输入设备并显示音量，检测录音不会上传", true);
    b.button(ID_V_MIC, L"检测麦克风");
    b.finish();
}

void SettingsWindow::switchPage(int index)
{
	if (index < 0 || index >= PAGE_COUNT) return;
	for (int i = 0; i < PAGE_COUNT; ++i) {
		if (s_viewports[i])
			ShowWindow(s_viewports[i], i == index ? SW_SHOW : SW_HIDE);
	}
	s_currentPage = index;
	scrollPage(index, 0);
	RECT client; logical_client_rect(s_hwnd, &client);
	RECT sidebar{ 0, 0, ViewportLeft(), client.bottom };
	invalidate_logical_rect(s_hwnd, &sidebar, FALSE);
}

void SettingsWindow::loadAllPages()
{
	loadRoutinePage();
	loadKeyboardPage();
	loadAppearancePage();
	loadLexiconPage();
	loadPhrasePage();
	loadSeniorPage();
    loadSpeechPage();
	loadAboutPage();
}

void SettingsWindow::loadRoutinePage()
{
	HWND p = s_pages[(int)SettingsEnum::SettingType::routine];
	auto check = [p](int id, bool on) {
		HWND c = GetDlgItem(p, id);
		if (c) SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
	};
	check(ID_R_QUAN, !sConfig->getIsDoublepinyinMode());
	check(ID_R_SHUANG, sConfig->getIsDoublepinyinMode());
	check(ID_R_DEFZH, !sConfig->getIsDefaultEnglishMode());
	check(ID_R_DEFEN, sConfig->getIsDefaultEnglishMode());
	check(ID_R_JIAN, !sConfig->runtime_traditional());
	check(ID_R_FAN, sConfig->runtime_traditional());
	check(ID_R_BANJIAO, !sConfig->runtime_full_width());
	check(ID_R_QUANJIAO, sConfig->runtime_full_width());
	check(ID_R_ENPUNCT, sConfig->getIsUseEnglishPunctuation());
	check(ID_R_CORRECT, sConfig->getPinyinCorrection());
	check(ID_L_AUTOSYMBOL, sConfig->getIsAutoSymbolCompletion());
	check(ID_L_DIGITSYMBOL, sConfig->getIsAutoConvertDigitSymbol());
	check(ID_L_LETTERSYMBOL, sConfig->get_auto_convert_letter_symbol());

	HWND scheme = GetDlgItem(p, ID_R_SCHEME);
	if (scheme) {
		const std::string cur = sConfig->getDoublepinyinSchemeName();
		int sel = -1;
		for (size_t i = 0; i < s_schemeNames.size(); ++i)
			if (s_schemeNames[i] == cur) sel = (int)i;
		SendMessageW(scheme, CB_SETCURSEL, sel >= 0 ? sel : 0, 0);
	}
	EnableWindow(GetDlgItem(p, ID_R_SCHEME), sConfig->getIsDoublepinyinMode());
}

void SettingsWindow::loadKeyboardPage()
{
	HWND p = s_pages[(int)SettingsEnum::SettingType::keyboard];
	auto check = [p](int id, bool on) {
		HWND c = GetDlgItem(p, id);
		if (c) SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
	};
	check(ID_K_CS, sConfig->getIsCtrlAndSpaceShortCutOpen());
	check(ID_K_SHIFT, sConfig->getIsShiftShortCutOpen());
	check(ID_K_CTRL, sConfig->getIsCtrlShortCutOpen());
	check(ID_K_WIDTHON, sConfig->getIsShiftAndSpaceShortCutOpen());
	check(ID_K_WIDTHOFF, !sConfig->getIsShiftAndSpaceShortCutOpen());
	check(ID_K_PUNCTON, sConfig->getIsCtrlAndPeriodShortCutOpen());
	check(ID_K_PUNCTOFF, !sConfig->getIsCtrlAndPeriodShortCutOpen());
	check(ID_K_SIMTRA, sConfig->getIsShortCutChangeCharacterSimOrTraOpen());
	check(ID_K_FLIPPAGE, sConfig->get_page_up_down_enabled());
    check(ID_K_FLIPMINUS, sConfig->get_minus_equals_page_enabled());
	check(ID_K_FLIPBRACE, sConfig->getIsBraceFlipPageOpen());
	check(ID_K_FLIPCOMMA, sConfig->getIsCommaAndPeriodFlipPageOpen());
	check(ID_K_SEM, sConfig->getSecondThirdCandidateKey() != SecondThirdCandidateKeyType::NONE);

	HWND simkey = GetDlgItem(p, ID_K_SIMTRAKEY);
	if (simkey)
		SendMessageW(simkey, CB_SETCURSEL, sConfig->getCharacterChangeSimOrTra() == "G" ? 1 : 0, 0);

	HWND w2c = GetDlgItem(p, ID_K_WORD2CHAR);
	if (w2c) {
		int sel = 0;
		switch (sConfig->getWordToCharacterKey()) {
		case WordToCharacterKeyType::MINUS: sel = 1; break;
		case WordToCharacterKeyType::BRACKET: sel = 2; break;
		default: sel = 0; break;
		}
		SendMessageW(w2c, CB_SETCURSEL, sel, 0);
	}
}

void SettingsWindow::loadAppearancePage()
{
	HWND p = s_pages[(int)SettingsEnum::SettingType::appearance];
	HWND size = GetDlgItem(p, ID_A_SIZE);
	if (size) {
		const int configured = sConfig->getCandidateFontSize();
		int nearest = 0;
		for (int i = 1; i < 7; ++i)
			if (std::abs(kCandidateSizeSteps[i] - configured) <
				std::abs(kCandidateSizeSteps[nearest] - configured)) nearest = i;
		SendMessageW(size, TBM_SETPOS, TRUE, nearest);
	}

	refreshCandidateFonts();

	const std::string themeKey = sConfig->getTheme();
	const int themeSel = themeKey == "dark" ? 1 : themeKey == "system" ? 2 : 0;
	HWND themeCombo = GetDlgItem(p, ID_A_THEME);
	if (themeCombo) SendMessageW(themeCombo, CB_SETCURSEL, themeSel, 0);

	g_candidateSkin = CandidateThemeConfig::parse(sConfig->getCandidateSkin());
	HWND opacity = GetDlgItem(p, ID_A_OPACITY);
	if (opacity) SendMessageW(opacity, TBM_SETPOS, TRUE, g_candidateSkin.opacity);
	HWND imageOpacity = GetDlgItem(p, ID_A_IMAGEOPACITY);
	if (imageOpacity) SendMessageW(imageOpacity, TBM_SETPOS, TRUE, 100 - g_candidateSkin.imageOpacity);
	refreshSkinControls();
}

void SettingsWindow::refreshCandidateFonts()
{
	using namespace coral::settings;
	HWND combo = GetDlgItem(s_pages[2], ID_A_FONT);
	if (!combo) return;
	const auto current = utf8ToWstr(sConfig->getCandidateFontName());
	g_candidateFonts = BuildCandidateFonts(ParseAddedFonts(sConfig->getCandidateFontList()),
		current, IsInstalledFont);
	SendMessageW(combo, CB_RESETCONTENT, 0, 0);
	int selection = -1;
	for (const auto& font : g_candidateFonts) {
		const auto item = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(font.label.c_str()));
		if (item == CB_ERR || item == CB_ERRSPACE) continue;
		SendMessageW(combo, CB_SETITEMDATA, item, &font - g_candidateFonts.data());
		if (SameFont(font.name, current)) selection = static_cast<int>(item);
	}
	// 缺失的配置字体不能显示成另一个被选中的字体。
	SendMessageW(combo, CB_SETCURSEL, selection, 0);
	EnableWindow(combo, !g_candidateFonts.empty());
	invalidate_logical_rect(combo, nullptr, FALSE);
}

void SettingsWindow::addCandidateFont()
{
	using namespace coral::settings;
	LOGFONTW font{};
	font.lfCharSet = DEFAULT_CHARSET;
	const auto current = utf8ToWstr(sConfig->getCandidateFontName());
	if (IsInstalledFont(current)) wcscpy_s(font.lfFaceName, current.c_str());
	CHOOSEFONTW chooser{};
	chooser.lStructSize = sizeof(chooser);
	chooser.hwndOwner = s_hwnd;
	chooser.lpLogFont = &font;
	chooser.Flags = CF_SCREENFONTS | CF_FORCEFONTEXIST | CF_NOVERTFONTS |
		CF_NOSCRIPTSEL | CF_NOSIZESEL | CF_NOSTYLESEL | CF_INITTOLOGFONTSTRUCT;
	if (!ChooseFontW(&chooser)) return;
	const std::wstring selected = CanonicalFontName(font.lfFaceName);
	// 对话框关闭后再复查一次，以防字体在对话框打开期间被删除。
	if (!IsInstalledFont(selected)) return;
	auto added = ParseAddedFonts(sConfig->getCandidateFontList());
	if (std::none_of(added.begin(), added.end(), [&](const auto& name) { return SameFont(name, selected); })) {
		added.push_back(selected);
		sConfig->setCandidateFontList(SerializeAddedFonts(added));
	}
	sConfig->setCandidateFontName(wstrToUtf8(selected));
	refreshCandidateFonts();
	invalidate_logical_rect(GetDlgItem(s_pages[2], ID_A_PREVIEW), nullptr, FALSE);
}


void SettingsWindow::applyCandidateSkin()
{
	sConfig->setCandidateSkin(g_candidateSkin.serialize());
	refreshSkinControls();
}

void SettingsWindow::refreshSkinControls()
{
	HWND p = s_pages[(int)SettingsEnum::SettingType::appearance];
	if (!p) return;
	for (HWND child = GetWindow(p, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
		const int id = GetDlgCtrlID(child);
		if (id >= ID_A_PREVIEW && id <= ID_A_TAB_LAST) invalidate_logical_rect(child, nullptr, FALSE);
	}
}

void SettingsWindow::switchSkinTab(int tab)
{
	if (tab == g_skinTab) return;
	g_skinTab = tab;
	PostMessageW(s_hwnd, kRebuildAppearance, 0, 0);
}

void SettingsWindow::switchPhraseTab(int tab)
{
	if (tab == g_phraseTab) return;
	g_phraseTab = tab;
	PostMessageW(s_hwnd, kRebuildPhrase, 0, 0);
}

// tab 切换后各 tab 的行数不同，整页按新 tab 重新排版，并尽量保持滚动位置。
void SettingsWindow::rebuildPage(int index)
{
	DpiContext context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	using SettingsEnum::SettingType;
	if (index != (int)SettingType::appearance && index != (int)SettingType::phrase) return;
	HWND page = s_pages[index];
	if (!page) return;
	SendMessageW(page, WM_SETREDRAW, FALSE, 0);
	while (HWND child = GetWindow(page, GW_CHILD))
		DestroyWindow(child);
	g_layouts[index] = PageLayout();
	if (index == (int)SettingType::appearance) {
		createAppearancePage();
		loadAppearancePage();
	} else {
		createPhrasePage();
		loadPhrasePage();
	}
	RECT viewport{};
	logical_client_rect(s_viewports[index], &viewport);
	RECT bounds{};
	logical_client_rect(page, &bounds);
	position_window(page, nullptr, 0, 0, bounds.right,
		(std::max)(static_cast<int>(viewport.bottom), g_layouts[index].height),
		SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
	const int scroll = s_scroll[index];
	s_scroll[index] = -1;
	scrollPage(index, scroll);
	SendMessageW(page, WM_SETREDRAW, TRUE, 0);
	RedrawWindow(s_viewports[index], nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

void SettingsWindow::selectSkinTile(int id)
{
	TileRef tile;
	if (!TileOf(id, tile)) return;
	CandidateThemeConfig& skin = g_candidateSkin;
	if (tile.kind == CandidateBackground::Image && tile.custom &&
		(skin.imageExtension.empty() || TileSelected(tile))) {
		pickCandidateImage();
		return;
	}
	if (tile.kind == CandidateBackground::Gradient && tile.custom && !skin.customGradientSet) {
		CandidateThemeConfig preset = skin;
		preset.gradient = 1;
		for (int mode = 0; mode < 2; ++mode)
			preset.gradientColors(mode == 1, skin.gradientFrom[mode], skin.gradientTo[mode]);
		skin.customGradientSet = true;
	}
	skin = TileConfig(tile);
	applyCandidateSkin();
}

void SettingsWindow::editSkinColor(int id)
{
	static COLORREF customColors[16] = {};
	CHOOSECOLORW chooser = { sizeof(chooser) };
	chooser.hwndOwner = s_hwnd;
	chooser.rgbResult = ToColorRef(SwatchColor(id));
	chooser.lpCustColors = customColors;
	chooser.Flags = CC_FULLOPEN | CC_RGBINIT;
	if (!ChooseColorW(&chooser)) return;
	const std::uint32_t color = 0xFF000000u | (GetRValue(chooser.rgbResult) << 16) |
		(GetGValue(chooser.rgbResult) << 8) | GetBValue(chooser.rgbResult);
	// 在其他类主题的 tab 下改色，先切换到该 tab 记住的主题再修改。
	CandidateThemeConfig& skin = g_candidateSkin;
	if (skin.kind != TabKind()) skin = TabConfig();
	const int mode = CandidateDark() ? 1 : 0;
	if (id == ID_A_COLOR_FROM || id == ID_A_COLOR_TO) {
		// 从当前渐变出发改为自定义渐变，另一端与另一颜色模式保持原样。
		for (int m = 0; m < 2; ++m)
			skin.gradientColors(m == 1, skin.gradientFrom[m], skin.gradientTo[m]);
		skin.customGradientSet = true;
		skin.gradient = coral::win32::kCandidateGradientPresetCount;
		skin.kind = CandidateBackground::Gradient;
		(id == ID_A_COLOR_FROM ? skin.gradientFrom : skin.gradientTo)[mode] = color;
	}
	else {
		if (!skin.customPalette) {
			skin.palette[0] = skin.effectivePalette(false);
			skin.palette[1] = skin.effectivePalette(true);
			skin.customPalette = true;
		}
		CandidatePalette& palette = skin.palette[mode];
		switch (id) {
		case ID_A_COLOR_TEXT: palette.text = color; break;
		case ID_A_COLOR_SELTEXT: palette.selectedText = color; break;
		case ID_A_COLOR_BG: palette.background = color; break;
		case ID_A_COLOR_SELBG: palette.selectedBackground = color; break;
		}
	}
	applyCandidateSkin();
}

void SettingsWindow::pickCandidateImage()
{
	wchar_t fileName[MAX_PATH] = L"";
	OPENFILENAMEW ofn = { sizeof(ofn) };
	ofn.hwndOwner = s_hwnd;
	ofn.lpstrFilter = L"图片 (*.jpg;*.jpeg;*.png)\0*.jpg;*.jpeg;*.png\0";
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileNameW(&ofn)) importCandidateImage(fileName);
}

void SettingsWindow::importCandidateImage(const std::wstring& source)
{
	namespace fs = std::filesystem;
	const auto warn = [](const wchar_t* message) {
		MessageBoxW(s_hwnd, message, L"CORAL拼音", MB_OK | MB_ICONWARNING);
	};
	std::wstring extension = fs::path(source).extension().wstring();
	std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
	if (extension != L".jpg" && extension != L".jpeg" && extension != L".png") {
		warn(L"仅支持 JPG 或 PNG 格式的图片");
		return;
	}
	std::error_code ec;
	const auto size = fs::file_size(source, ec);
	if (ec || size > coral::win32::kCandidateImageMaxBytes) {
		warn(L"图片大小不能超过 10 MB");
		return;
	}
	const auto image = coral::win32::DecodeSkinImage(source);
	if (!image) {
		warn(L"无法读取该图片，请换一张试试");
		return;
	}
	// 裁剪框按候选窗完全展开时的尺寸取图，原图至少要有这么大。
	const coral::win32::CandidateSkin defaults{};
	const SIZE need = defaults.imageSize();
	if (image->sourceWidth < need.cx || image->sourceHeight < need.cy) {
		wchar_t message[160] = {};
		swprintf_s(message, L"图片尺寸为 %d×%d 像素，小于候选窗口的 %d×%d 像素。\n请选择更大的图片，建议 %d×%d 像素以上。",
			image->sourceWidth, image->sourceHeight, need.cx, need.cy, need.cx * 2, need.cy * 2);
		warn(message);
		return;
	}
	int imageOpacity = g_candidateSkin.imageOpacity;
	TileRef imageTile;
	TileOf(kCustomImageTile, imageTile);
	const auto imageConfig = TileConfig(imageTile);
	const auto background = imageConfig.effectivePalette(CandidateDark()).background;
	const auto cropped = SkinCropDialog::run(s_hwnd, *image, s_skin, imageOpacity,
		background, CandidateDark() ? 0x9E141519 : 0x26FFFFFF);
	if (!cropped) return;
	const std::wstring target = coral::win32::CandidateCustomImagePath(UserDir(), "png");
	fs::create_directories(fs::path(target).parent_path(), ec);
	if (!coral::win32::SaveSkinImagePng(*cropped, target)) {
		warn(L"保存图片失败");
		return;
	}
	fs::remove(coral::win32::CandidateCustomImagePath(UserDir(), "jpg"), ec);
	g_candidateSkin.imageOpacity = imageOpacity;
	g_candidateSkin.imageExtension = "png";
	++g_candidateSkin.imageRevision;
	TileRef tile;
	TileOf(kCustomImageTile, tile);
	g_candidateSkin = TileConfig(tile);
	applyCandidateSkin();
	HWND imageSlider = GetDlgItem(s_pages[2], ID_A_IMAGEOPACITY);
	if (imageSlider) SendMessageW(imageSlider, TBM_SETPOS, TRUE, 100 - imageOpacity);
	if (g_skinTab != static_cast<int>(CandidateBackground::Image)) {
		g_skinTab = static_cast<int>(CandidateBackground::Image);
		PostMessageW(s_hwnd, kRebuildAppearance, 0, 0);
	}
}

void SettingsWindow::loadLexiconPage()
{
	// 本页面现在只包含操作按钮。
}

void SettingsWindow::loadPhrasePage()
{
	HWND p = s_pages[(int)SettingsEnum::SettingType::phrase];
	if (HWND c = GetDlgItem(p, ID_P_CLIPSWITCH))
		SendMessageW(c, BM_SETCHECK, sConfig->getIsClipboardEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
	refreshPhraseList();
	refreshClipboardList();
}

void SettingsWindow::refreshPhraseList()
{
	if (!s_hwnd) return;
	HWND list = GetDlgItem(s_pages[(int)SettingsEnum::SettingType::phrase], ID_P_PHRASES);
	g_phrases.clear();
	if (!list) return;
	const bool loaded = coral::runtime::list_phrase_shortcuts(g_phrases);
	std::vector<std::wstring> texts, notes;
	for (const CustomPhraseEntry& entry : g_phrases) {
		texts.push_back(coral::clipboard::DisplayLine(utf8ToWstr(entry.phrase)));
		notes.push_back(utf8ToWstr(entry.code));
	}
	SetListItems(list, std::move(texts), std::move(notes), loaded ? L"" : L"输入法词库加载中，请稍后再打开此页");
}

void SettingsWindow::refreshClipboardList()
{
	if (!s_hwnd) return;
	HWND p = s_pages[(int)SettingsEnum::SettingType::phrase];
	HWND list = GetDlgItem(p, ID_P_CLIPS);
	g_clipTexts.clear();
	if (!list) return;
	std::vector<std::wstring> texts;
	for (const coral::clipboard::Entry& entry : ClipboardHistory::instance().entries()) {
		g_clipTexts.push_back(entry.text);
		texts.push_back(coral::clipboard::DisplayLine(entry.text));
	}
	if (HWND clear = GetDlgItem(p, ID_P_CLEARCLIPS))
		EnableWindow(clear, !texts.empty());
	SetListItems(list, std::move(texts), {});
}

void SettingsWindow::showListMenu(int listId, int row, POINT screen)
{
	enum : UINT { kEdit = 1, kDelete, kAddPhrase };
	HMENU menu = CreatePopupMenu();
	if (!menu) return;
	if (listId == ID_P_PHRASES && row >= 0 && row < static_cast<int>(g_phrases.size())) {
		AppendMenuW(menu, MF_STRING, kEdit, L"编辑");
		AppendMenuW(menu, MF_STRING, kDelete, L"删除");
	} else if (listId == ID_P_CLIPS && row >= 0 && row < static_cast<int>(g_clipTexts.size())) {
		AppendMenuW(menu, MF_STRING, kDelete, L"删除");
		AppendMenuW(menu, MF_STRING, kAddPhrase, L"添加到常用语");
	} else {
		DestroyMenu(menu);
		return;
	}
	const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
		screen.x, screen.y, 0, s_hwnd, nullptr));
	DestroyMenu(menu);
	if (!command) return;

	if (listId == ID_P_PHRASES) {
		// 菜单期间列表可能已刷新，按菜单弹出前的条目操作。
		const CustomPhraseEntry entry = g_phrases[row];
		if (command == kEdit) {
			AddPhraseDialog::Request request;
			request.content = utf8ToWstr(entry.phrase);
			request.code = utf8ToWstr(entry.code);
			request.editing = true;
			request.originalCode = request.code;
			request.originalContent = request.content;
			AddPhraseDialog::show(s_hwnd, request);
		} else if (command == kDelete) {
			coral::runtime::remove_phrase_shortcut(entry.code, entry.phrase);
			refreshPhraseList();
		}
		return;
	}

	const std::wstring text = g_clipTexts[row];
	if (command == kAddPhrase) {
		AddPhraseDialog::Request request;
		const std::size_t first = text.find_first_not_of(L" \t\r\n\u3000");
		if (first != std::wstring::npos)
			request.content = text.substr(first, text.find_last_not_of(L" \t\r\n\u3000") - first + 1);
		AddPhraseDialog::show(s_hwnd, request);
	} else if (command == kDelete) {
		const auto& entries = ClipboardHistory::instance().entries();
		for (std::size_t i = 0; i < entries.size(); ++i) {
			if (entries[i].text == text) {
				ClipboardHistory::instance().remove(i);
				break;
			}
		}
		refreshClipboardList();
	}
}

void SettingsWindow::clearClipboardHistory()
{
	if (g_clipTexts.empty()) return;
	if (!ConfirmDialog::show(s_hwnd, L"确认清空全部剪贴板记录？")) return;
	ClipboardHistory::instance().clear();
	refreshClipboardList();
}

void SettingsWindow::loadSeniorPage()
{
	HWND p = s_pages[(int)SettingsEnum::SettingType::senior];
	auto check = [p](int id, bool on) {
		HWND c = GetDlgItem(p, id);
		if (c) SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
	};
	check(ID_S_PREDICTION, sConfig->get_smart_prediction());
	check(ID_S_UMODE, sConfig->getIsUMode());
	check(ID_S_VMODE, sConfig->getIsVMode());
    check(ID_S_TRANSLATION, sConfig->get_translation_enabled());
    const auto shortcut = sConfig->get_translation_shortcut();
    for (std::size_t i = 0; i < coral::translation::kShortcutOptions.size(); ++i)
        if (shortcut == coral::translation::kShortcutOptions[i].value)
            SendMessageW(GetDlgItem(p, ID_S_SHORTCUT), CB_SETCURSEL, i, 0);
    const auto credentials = sConfig->get_translation_credentials();
    SetWindowTextW(GetDlgItem(p, ID_S_KEYID), utf8ToWstr(credentials.first).c_str());
    SetWindowTextW(GetDlgItem(p, ID_S_SECRET), utf8ToWstr(credentials.second).c_str());
    const auto target = sConfig->get_translation_target();
    for (std::size_t i = 0; i < coral::translation::kTargetLanguages.size(); ++i)
        if (target == coral::translation::kTargetLanguages[i].code)
            SendMessageW(GetDlgItem(p, ID_S_TARGET), CB_SETCURSEL, i, 0);
    for (const auto id : {ID_S_KEYID, ID_S_SECRET, ID_S_SAVEKEY, ID_S_TARGET})
        EnableWindow(GetDlgItem(p, id), sConfig->get_translation_enabled());
}

void SettingsWindow::loadAboutPage()
{
    HWND page = s_pages[(int)SettingsEnum::SettingType::about];
    SendMessageW(GetDlgItem(page, ID_AB_AUTOCHECK), BM_SETCHECK,
        sConfig->getIsAutoCheck() ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsWindow::loadSpeechPage()
{
    HWND page = s_pages[(int)SettingsEnum::SettingType::speech];
    const bool enabled = sConfig->get_speech_enabled();
    SendMessageW(GetDlgItem(page, ID_V_ENABLED), BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    const auto credentials = sConfig->get_speech_credentials();
    SetWindowTextW(GetDlgItem(page, ID_V_KEYID), utf8ToWstr(credentials.first).c_str());
    SetWindowTextW(GetDlgItem(page, ID_V_SECRET), utf8ToWstr(credentials.second).c_str());
    const auto shortcut = sConfig->get_speech_shortcut();
    for (std::size_t i = 0; i < coral::speech::kSpeechShortcuts.size(); ++i)
        if (shortcut == coral::speech::kSpeechShortcuts[i].value)
            SendMessageW(GetDlgItem(page, ID_V_SHORTCUT), CB_SETCURSEL, i, 0);
    const bool push_to_talk = sConfig->get_speech_push_to_talk();
    SendMessageW(GetDlgItem(page, ID_V_PUSHTOTALK), BM_SETCHECK, push_to_talk ? BST_CHECKED : BST_UNCHECKED, 0);
    EnableWindow(GetDlgItem(page, ID_V_SHORTCUT), !push_to_talk);
    for (const auto id : {ID_V_KEYID, ID_V_SECRET, ID_V_SAVEKEY}) EnableWindow(GetDlgItem(page, id), enabled);
}

void SettingsWindow::onCommand(WPARAM wParam, LPARAM lParam)
{
	const int id = LOWORD(wParam);
	const int code = HIWORD(wParam);
	const bool checked = SendMessageW((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED;

	if (code == kImageDropped) {
		importCandidateImage(g_droppedImage);
		return;
	}
    if (id == ID_S_SAVEKEY && code == BN_CLICKED) {
        wchar_t key_id[4097]{}, secret[4097]{};
        GetWindowTextW(GetDlgItem(s_pages[5], ID_S_KEYID), key_id, 4097);
        GetWindowTextW(GetDlgItem(s_pages[5], ID_S_SECRET), secret, 4097);
        CString id_value(key_id), secret_value(secret);
        id_value.Trim(); secret_value.Trim();
        const bool saved = sConfig->set_translation_credentials(
            wstrToUtf8(id_value.GetString()), wstrToUtf8(secret_value.GetString()));
        SecureZeroMemory(key_id, sizeof(key_id)); SecureZeroMemory(secret, sizeof(secret));
        MessageBoxW(s_hwnd, saved ? L"翻译凭据已保存。" : L"保存失败，请完整填写两项凭据，或同时清空以删除。",
            L"翻译设置", MB_OK | (saved ? MB_ICONINFORMATION : MB_ICONWARNING));
        return;
    }
	if (code != BN_CLICKED && code != CBN_SELENDOK) return;
	if (code == BN_CLICKED && id >= ID_A_TAB && id <= ID_A_TAB_LAST) {
		switchSkinTab(id - ID_A_TAB);
		return;
	}
	if (code == BN_CLICKED && id >= ID_P_TAB && id <= ID_P_TAB_LAST) {
		switchPhraseTab(id - ID_P_TAB);
		return;
	}
	TileRef tile;
	if (code == BN_CLICKED && TileOf(id, tile)) {
		selectSkinTile(id);
		return;
	}

	switch (id) {
    case ID_V_ENABLED:
        sConfig->set_speech_enabled(checked);
        for (const auto control : {ID_V_KEYID, ID_V_SECRET, ID_V_SAVEKEY})
            EnableWindow(GetDlgItem(s_pages[(int)SettingsEnum::SettingType::speech], control), checked);
        break;
    case ID_V_PUSHTOTALK: {
        sConfig->set_speech_push_to_talk(checked);
        HWND page = s_pages[(int)SettingsEnum::SettingType::speech];
        EnableWindow(GetDlgItem(page, ID_V_SHORTCUT), !checked);
        break;
    }
    case ID_V_SAVEKEY: {
        wchar_t key[513]{}, secret[513]{};
        HWND page = s_pages[(int)SettingsEnum::SettingType::speech];
        GetWindowTextW(GetDlgItem(page, ID_V_KEYID), key, 513);
        GetWindowTextW(GetDlgItem(page, ID_V_SECRET), secret, 513);
        CString key_value(key), secret_value(secret);
        key_value.Trim(); secret_value.Trim();
        const bool saved = sConfig->set_speech_credentials(wstrToUtf8(key_value.GetString()), wstrToUtf8(secret_value.GetString()));
        SecureZeroMemory(key, sizeof(key)); SecureZeroMemory(secret, sizeof(secret));
        MessageBoxW(s_hwnd, saved ? L"语音输入凭据已保存。首次使用请确认腾讯云已开通语音识别服务。" :
            L"保存失败，请完整填写 SecretId 与 SecretKey，或同时清空以删除。", L"语音输入", MB_OK | (saved ? MB_ICONINFORMATION : MB_ICONWARNING));
        break;
    }
    case ID_V_SHORTCUT:
        if (code == CBN_SELENDOK) {
            const auto selected = SendMessageW(GetDlgItem(s_pages[(int)SettingsEnum::SettingType::speech], ID_V_SHORTCUT), CB_GETCURSEL, 0, 0);
            if (selected >= 0 && static_cast<std::size_t>(selected) < coral::speech::kSpeechShortcuts.size())
                sConfig->set_speech_shortcut(coral::speech::kSpeechShortcuts[selected].value);
        }
        break;
    case ID_V_MIC:
        if (!coral::speech::win32::request_speech_window(true))
            MessageBoxW(s_hwnd, L"无法打开麦克风检测，请重启 CORAL 拼音后重试。", L"语音输入", MB_OK | MB_ICONWARNING);
        break;
	// ---- 常规页 ----
	case ID_R_QUAN: sConfig->setDoublepinyinMode(false); EnableWindow(GetDlgItem(s_pages[0], ID_R_SCHEME), FALSE); break;
	case ID_R_SHUANG: sConfig->setDoublepinyinMode(true); EnableWindow(GetDlgItem(s_pages[0], ID_R_SCHEME), TRUE); break;
	case ID_R_SCHEME:
		if (code == CBN_SELENDOK) {
			HWND c = GetDlgItem(s_pages[0], ID_R_SCHEME);
			int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
			if (sel >= 0 && sel < (int)s_schemeNames.size())
				sConfig->setDoublepinyinSchemeName(s_schemeNames[sel]);
		}
		break;
	case ID_R_DEFZH: sConfig->setIsDefaultEnglishMode(false); break;
	case ID_R_DEFEN: sConfig->setIsDefaultEnglishMode(true); break;
	case ID_R_JIAN: sConfig->set_runtime_traditional(false); break;
	case ID_R_FAN: sConfig->set_runtime_traditional(true); break;
	case ID_R_BANJIAO: sConfig->set_runtime_full_width(false); break;
	case ID_R_QUANJIAO: sConfig->set_runtime_full_width(true); break;
	case ID_R_ENPUNCT: sConfig->setIsUseEnglishPunctuation(checked); break;
	case ID_R_CORRECT: sConfig->setPinyinCorrection(checked); break;
	case ID_R_FUZZY: FuzzyPinyinDialog::showInstance(s_hwnd); break;

	// ---- 按键页 ----
	case ID_K_CS: sConfig->setChangeLanguageModeShortCut("isCtrlAndSpaceChangeLanguageModeOpen", checked); break;
	case ID_K_SHIFT: sConfig->setChangeLanguageModeShortCut("isShiftChangeLanguageModeOpen", checked); break;
	case ID_K_CTRL: sConfig->setChangeLanguageModeShortCut("isCtrlChangeLanguageModeOpen", checked); break;
	case ID_K_WIDTHON: sConfig->setChangeCharacterWidthShortCut(true); break;
	case ID_K_WIDTHOFF: sConfig->setChangeCharacterWidthShortCut(false); break;
	case ID_K_PUNCTON: sConfig->setChangePunctuationLanguageModeShortCut(true); break;
	case ID_K_PUNCTOFF: sConfig->setChangePunctuationLanguageModeShortCut(false); break;
	case ID_K_SIMTRA: sConfig->setShortCutChangeCharacterSimOrTra(checked); break;
	case ID_K_SIMTRAKEY:
		if (code == CBN_SELENDOK) {
			HWND c = GetDlgItem(s_pages[1], ID_K_SIMTRAKEY);
			int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
			sConfig->setCharacterChangeSimOrTra(sel == 1 ? "G" : "F");
		}
		break;
	case ID_K_FLIPPAGE: sConfig->setFlipPageShortCut("isPageUpDownFlipPageOpen", checked); break;
	case ID_K_FLIPMINUS:
        sConfig->setFlipPageShortCut("isMinusAndPlusFlipPageOpen", checked);
        if (checked && sConfig->get_translation_enabled() &&
            coral::translation::shortcut_conflicts(
                coral::translation::shortcut_option(sConfig->get_translation_shortcut()).virtual_key, true))
            MessageBoxW(s_hwnd, L"已启用 - / + 翻页。翻译快捷键 = 因按键冲突已失效，请到“高级 → 翻译快捷键”改用 F1–F12。",
                L"快捷键冲突", MB_OK | MB_ICONWARNING);
        break;
	case ID_K_FLIPBRACE: sConfig->setFlipPageShortCut("isBraceFlipPageOpen", checked); break;
	case ID_K_FLIPCOMMA: sConfig->setFlipPageShortCut("isCommaAndPeriodFlipPageOpen", checked); break;
	case ID_K_SEM:
		sConfig->setSecondThirdCandidateKey(checked ? SecondThirdCandidateKeyType::SEMICOLON
			: SecondThirdCandidateKeyType::NONE);
		break;
	case ID_K_WORD2CHAR:
		if (code == CBN_SELENDOK) {
			HWND c = GetDlgItem(s_pages[1], ID_K_WORD2CHAR);
			int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
			switch (sel) {
			case 1:
                sConfig->setFlipPageShortCut("isMinusAndPlusFlipPageOpen", false);
                SendMessageW(GetDlgItem(s_pages[1], ID_K_FLIPMINUS), BM_SETCHECK, BST_UNCHECKED, 0);
				sConfig->setWordToCharacterKey(WordToCharacterKeyType::MINUS);
				break;
			case 2:
				sConfig->setFlipPageShortCut("isBraceFlipPageOpen", false);
				SendMessageW(GetDlgItem(s_pages[1], ID_K_FLIPBRACE), BM_SETCHECK, BST_UNCHECKED, 0);
				sConfig->setWordToCharacterKey(WordToCharacterKeyType::BRACKET);
				break;
			default: sConfig->setWordToCharacterKey(WordToCharacterKeyType::NONE); break;
			}
		}
		break;

	// ---- 外观页 ----
	case ID_A_FONT:
		if (code == CBN_SELENDOK) {
			HWND c = GetDlgItem(s_pages[2], ID_A_FONT);
			const auto sel = SendMessageW(c, CB_GETCURSEL, 0, 0);
			const auto index = sel == CB_ERR ? CB_ERR : SendMessageW(c, CB_GETITEMDATA, sel, 0);
			if (index >= 0 && static_cast<size_t>(index) < g_candidateFonts.size())
				sConfig->setCandidateFontName(wstrToUtf8(g_candidateFonts[index].name));
			invalidate_logical_rect(GetDlgItem(s_pages[2], ID_A_PREVIEW), nullptr, FALSE);
		}
		break;
	case ID_A_ADDFONT: addCandidateFont(); break;
	case ID_A_THEME:
		if (code == CBN_SELENDOK) {
			HWND c = GetDlgItem(s_pages[2], ID_A_THEME);
			int sel = (int)SendMessageW(c, CB_GETCURSEL, 0, 0);
			sConfig->setTheme(sel == 1 ? "dark" : sel == 2 ? "system" : "light");
			refreshSkinControls();
		}
		break;
	case ID_A_COLOR_TEXT:
	case ID_A_COLOR_SELTEXT:
	case ID_A_COLOR_BG:
	case ID_A_COLOR_SELBG:
	case ID_A_COLOR_FROM:
	case ID_A_COLOR_TO:
		editSkinColor(id);
		break;

	// ---- 词库页 ----
	case ID_L_EXPORT: exportLexicon(); break;
	case ID_L_IMPORT: importLexicon(); break;
	case ID_L_CLEAR: clear_lexicon(); break;
	case ID_L_AUTOSYMBOL: sConfig->setIsAutoSymbolCompletion(checked); break;
	case ID_L_DIGITSYMBOL: sConfig->setIsAutoConvertDigitSymbol(checked); break;
	case ID_L_LETTERSYMBOL: sConfig->set_auto_convert_letter_symbol(checked); break;

	// ---- 常用语和剪贴板页 ----
	case ID_P_ADD: AddPhraseDialog::show(s_hwnd, {}); break;
	case ID_P_CLIPSWITCH: sConfig->setIsClipboardEnabled(checked); break;
	case ID_P_CLEARCLIPS: clearClipboardHistory(); break;

	// ---- 高级页 ----
    case ID_S_TRANSLATION:
        if (checked && coral::translation::shortcut_conflicts(
            coral::translation::shortcut_option(sConfig->get_translation_shortcut()).virtual_key,
            sConfig->get_minus_equals_page_enabled())) {
            SendMessageW(GetDlgItem(s_pages[5], ID_S_TRANSLATION), BM_SETCHECK, BST_UNCHECKED, 0);
            sConfig->set_translation_enabled(false);
            for (const auto control : {ID_S_KEYID, ID_S_SECRET, ID_S_SAVEKEY, ID_S_TARGET})
                EnableWindow(GetDlgItem(s_pages[5], control), FALSE);
            MessageBoxW(s_hwnd, L"翻译快捷键 = 与 - / + 翻页冲突，请先将翻译快捷键改为 F1–F12，再启用翻译。",
                L"快捷键冲突", MB_OK | MB_ICONWARNING);
            break;
        }
        sConfig->set_translation_enabled(checked);
        for (const auto control : {ID_S_KEYID, ID_S_SECRET, ID_S_SAVEKEY, ID_S_TARGET})
            EnableWindow(GetDlgItem(s_pages[5], control), checked);
        break;
    case ID_S_SHORTCUT:
        if (code == CBN_SELENDOK) {
            HWND control = GetDlgItem(s_pages[5], ID_S_SHORTCUT);
            const auto selected = SendMessageW(control, CB_GETCURSEL, 0, 0);
            if (selected < 0 || static_cast<std::size_t>(selected) >= coral::translation::kShortcutOptions.size()) break;
            const auto& option = coral::translation::kShortcutOptions[selected];
            if (sConfig->get_translation_enabled() && coral::translation::shortcut_conflicts(
                option.virtual_key, sConfig->get_minus_equals_page_enabled())) {
                const auto previous = sConfig->get_translation_shortcut();
                for (std::size_t i = 0; i < coral::translation::kShortcutOptions.size(); ++i)
                    if (previous == coral::translation::kShortcutOptions[i].value)
                        SendMessageW(control, CB_SETCURSEL, i, 0);
                MessageBoxW(s_hwnd, L"翻译快捷键 = 与 - / + 翻页冲突，请选择 F1–F12，或先关闭 - / + 翻页。",
                    L"快捷键冲突", MB_OK | MB_ICONWARNING);
                break;
            }
            sConfig->set_translation_shortcut(option.value);
        }
        break;
    case ID_S_TRANSLATION_HELP:
        if (reinterpret_cast<INT_PTR>(ShellExecuteW(s_hwnd, L"open",
            L"https://coralime.cn/translation-help.html", nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
            MessageBoxW(s_hwnd, L"无法打开浏览器，请访问 https://coralime.cn/translation-help.html", L"翻译开通教程", MB_OK | MB_ICONINFORMATION);
        break;
    case ID_S_TARGET:
        if (code == CBN_SELENDOK) {
            const auto selected = SendMessageW(GetDlgItem(s_pages[5], ID_S_TARGET), CB_GETCURSEL, 0, 0);
            if (selected >= 0 && static_cast<std::size_t>(selected) < coral::translation::kTargetLanguages.size())
                sConfig->set_translation_target(coral::translation::kTargetLanguages[selected].code);
        }
        break;
	case ID_S_PREDICTION: sConfig->set_smart_prediction(checked); break;
	case ID_S_UMODE: sConfig->setIsUMode(checked); break;
	case ID_S_VMODE: sConfig->setIsVMode(checked); break;
    case ID_S_UHELP: MessageBoxW(s_hwnd, coral::composition::kUModeHelp, L"U / UU 模式使用方法", MB_OK | MB_ICONINFORMATION); break;
    case ID_S_VHELP: MessageBoxW(s_hwnd, coral::composition::kVModeHelp, L"V 模式使用方法", MB_OK | MB_ICONINFORMATION); break;

	// ---- 关于页 ----
	case ID_AB_CHECKUPDATE: coral::updates::UpdateCoordinator::instance().check_update(true); break;
	case ID_AB_AUTOCHECK: sConfig->setIsAutoCheck(checked); break;
	case ID_AB_EULA: ShellExecuteW(nullptr, L"open", L"https://coralime.cn/privacy-policy/coral-pinyin/user-agreement", nullptr, nullptr, SW_SHOWNORMAL); break;
	case ID_AB_PRIVACY: ShellExecuteW(nullptr, L"open", L"https://coralime.cn/privacy-policy/coral-pinyin/privacy-policy", nullptr, nullptr, SW_SHOWNORMAL); break;
	}
}

void SettingsWindow::onScroll(WPARAM, LPARAM lParam)
{
	HWND control = reinterpret_cast<HWND>(lParam);
	if (!control) return;
	const int position = static_cast<int>(SendMessageW(control, TBM_GETPOS, 0, 0));
	if (GetDlgCtrlID(control) == ID_A_IMAGEOPACITY) {
		const int opacity = 100 - position;
		if (g_candidateSkin.imageOpacity == opacity) return;
		g_candidateSkin.imageOpacity = opacity;
		applyCandidateSkin();
		return;
	}
	if (GetDlgCtrlID(control) == ID_A_OPACITY) {
		if (g_candidateSkin.opacity == position) return;
		g_candidateSkin.opacity = position;
		sConfig->setCandidateSkin(g_candidateSkin.serialize());
		invalidate_logical_rect(GetDlgItem(s_pages[2], ID_A_PREVIEW), nullptr, FALSE);
		return;
	}
	if (GetDlgCtrlID(control) != ID_A_SIZE) return;
	if (position >= 0 && position < 7 &&
		sConfig->getCandidateFontSize() != kCandidateSizeSteps[position]) {
		sConfig->setCandidateFontSize(kCandidateSizeSteps[position]);
		invalidate_logical_rect(GetDlgItem(s_pages[2], ID_A_PREVIEW), nullptr, FALSE);
	}
}

void SettingsWindow::exportLexicon()
{
	wchar_t fileName[MAX_PATH] = L"用户词库.dat";
	OPENFILENAMEW ofn = { sizeof(ofn) };
	ofn.hwndOwner = s_hwnd;
	ofn.lpstrFilter = L"词库文件 (*.dat;*.txt)\0*.dat;*.txt\0所有文件 (*.*)\0*.*\0";
	ofn.lpstrFile = fileName;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	if (!GetSaveFileNameW(&ofn)) return;
	if (SUCCEEDED(CPinyinModule::Instance().export_user_lexicon(std::filesystem::path(fileName)))) {
		MessageBoxW(s_hwnd, L"导出成功", L"CORAL拼音", MB_OK | MB_ICONINFORMATION);
	}
	else {
		MessageBoxW(s_hwnd, L"导出失败", L"CORAL拼音", MB_OK | MB_ICONERROR);
	}
}

void SettingsWindow::importLexicon()
{
	LexiconImportDialog::showInstance(s_hwnd);
}

void SettingsWindow::clear_lexicon()
{
	if (ConfirmDialog::show(s_hwnd, L"确认清除全部用户词库？该操作不可恢复。")) {
		if (coral::runtime::clear_user_lexicon())
			MessageBoxW(s_hwnd, L"已清除", L"CORAL拼音", MB_OK | MB_ICONINFORMATION);
		else
			MessageBoxW(s_hwnd, L"清除失败", L"CORAL拼音", MB_OK | MB_ICONERROR);
	}
}

std::wstring SettingsWindow::getVersionString()
{
	// 安装器将 PRODUCT_VERSION 写入安装目录的 INI；优先使用该跨平台来源。
	WCHAR exePath[MAX_PATH] = { 0 };
	const DWORD pathLength = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (pathLength > 0 && pathLength < MAX_PATH) {
		const auto version = coral::config::readInstalledAppVersion(
			std::filesystem::path(exePath).parent_path());
		if (!version.empty()) return utf8ToWstr(version);
	}
	// 开发构建或配置缺失/损坏时，回退到本进程的文件版本。
	DWORD handle = 0;
	DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
	if (size > 0) {
		std::vector<BYTE> data(size);
		if (GetFileVersionInfoW(exePath, handle, size, data.data())) {
			VS_FIXEDFILEINFO* info = nullptr;
			UINT len = 0;
			if (VerQueryValueW(data.data(), L"\\", (LPVOID*)&info, &len) && info) {
				wchar_t buf[64];
				swprintf_s(buf, L"%d.%d.%d.%d",
					HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
					HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
				return buf;
			}
		}
	}
	// 本进程无版本资源（dev 构建）时，读安装时写入的注册表版本
	{
		WCHAR regBuf[64] = { 0 };
		DWORD regSize = sizeof(regBuf);
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, coral::product::kAppRegistryWide,
			0, KEY_READ | KEY_WOW64_32KEY, &key) == ERROR_SUCCESS) {
			const LSTATUS status = RegQueryValueExW(key, L"DisplayVersion", nullptr, nullptr,
				reinterpret_cast<LPBYTE>(regBuf), &regSize);
			RegCloseKey(key);
			if (status == ERROR_SUCCESS && regBuf[0] != L'\0')
				return regBuf;
		}
	}
	return L"dev";
}

std::vector<std::pair<std::string, std::wstring>> SettingsWindow::enumDoublepinyinSchemes()
{
	std::vector<std::pair<std::string, std::wstring>> result;
	std::error_code ec;
	const auto dir = hlp::getInstallPath() / "data" / "doublepinyin";
	for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != L".ini") continue;
		const std::string schemeName = entry.path().stem().u8string();
		const auto title = coral::config::readDoublepinyinSchemeTitle(entry.path());
		result.emplace_back(schemeName, utf8ToWstr(title.empty() ? schemeName : title));
	}
	std::sort(result.begin(), result.end());
	return result;
}

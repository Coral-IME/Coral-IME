#pragma once

#include "coral/composition/presentation_gateway.h"
#include "candidate_theme.h"
#include "soft_canvas.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace coral::win32
{
// 颜色为直通（非预乘）0xAARRGGBB。
using CandidateShadow = soft::Shadow;

// 候选弹窗的全部视觉参数。内容尺寸以 96 DPI 下 16px 候选字体的逻辑像素表示，
// 随配置的字号缩放；窗口装饰尺寸（边框、阴影）
// 只随 DPI 缩放。
// `opacity` 仅影响面板与悬停/选中背景。半透明面板使用灰度字形掩码，
// 使文字和控件保留各自的 alpha。
struct CandidateSkin
{
	// 纯色填充色；
	// 也是渐变背景和图像背景下文字混色的代表色。
	std::uint32_t panel = 0xFFFCFCFD;
	CandidateBackground background = CandidateBackground::Solid;
	std::uint32_t gradientFrom = 0;
	std::uint32_t gradientTo = 0;
	std::shared_ptr<const SkinImage> image;
	// 叠加在图像之上绘制，保证两种颜色模式下文字都清晰可读。
	std::uint32_t imageVeil = 0;
	std::uint32_t imageBase = 0xFFFCFCFD;
	int imageOpacity = 100;
	float opacity = 1.0f;
	std::uint32_t border = 0x1A101828;
	std::uint32_t text = 0xFF1F2329;
	std::uint32_t index = 0xFF8B9098;
	std::uint32_t selectedFill = 0xFFE6EEFB;
	std::uint32_t selectedRing = 0x29275F9E;
	std::uint32_t selectedText = 0xFF275F9E;
	std::uint32_t selectedIndex = 0x9E275F9E;
	std::uint32_t rowDivider = 0x0B0F172A;
	std::uint32_t toggleIcon = 0xFF9AA1AB;
	std::uint32_t toggleIconHover = 0xFF5C636E;
	std::uint32_t toggleHoverFill = 0x0E0F172A;
	std::uint32_t scrollThumb = 0x381E2A37;
	std::uint32_t scrollThumbActive = 0x611E2A37;
	CandidateShadow shadows[2]{
		{ 1.0f, 3.0f, 0.0f, 0x14101828 },
		{ 6.0f, 20.0f, -4.0f, 0x29101828 } };

	float borderWidth = 1.0f;
	int panelRadius = 10;
	int paddingX = 4;
	int paddingY = 4;
	int rowHeight = 32;
	int itemInsetY = 2;
	int itemRadius = 7;
	int itemPaddingX = 8;
	int itemGap = 2;
	int indexGap = 5;
	int indexFontSize = 12;
	// 面板宽度固定；行是至少 minItemWidth 宽的列网格，
	// 候选词条按其文本需要占用若干列。折叠行
	// 在能显示更多词语时，可使用最多 collapsedMaxColumns 列的更细网格。
	int panelWidth = 530;
	int minItemWidth = 72;
	int maxColumns = 6;
	int collapsedMaxColumns = 9;
	int toggleSize = 26;
	int toggleRadius = 6;
	int toggleGap = 4;
	int chevronSize = 14;
	float chevronStroke = 1.7f;
	int scrollbarWidth = 4;
	int scrollbarActiveWidth = 6;
	int scrollbarHitWidth = 12;
	// 展开列表滚动后，右侧留出的滚动条沟槽。
	int scrollGutter = 10;
	int maxVisibleRows = 5;
	// 纯视觉上的缓动；选中与滚动状态立即变化。
	// 为 0 时禁用动画。
	int selectionAnimationMs = 100;
	int scrollAnimationMs = 140;
	int fontWeight = FW_NORMAL;
	std::wstring indexFontName;

	// 背景图始终覆盖完全展开的面板，锚定在顶部：折叠面板显示其
	// 前 collapsedImageHeight() 像素。
	// 以 16px 字体的逻辑像素表示；两者同步缩放，因此宽高比固定。
	SIZE imageSize() const { return { panelWidth, paddingY * 2 + maxVisibleRows * rowHeight }; }
	int collapsedImageHeight() const { return paddingY * 2 + rowHeight; }

	static CandidateSkin light();
	static CandidateSkin dark();
	// `theme` 的浅色/深色基底，配置的皮肤叠加在其上。
	static CandidateSkin themed(const std::string& theme, const std::string& skin,
		const std::string& imagePath);
	static CandidateSkin themed(bool dark, const CandidateThemeConfig& config,
		const std::wstring& imagePath);
};

// 由两种窗口宿主共用的候选布局、绘制与命中测试几何。
// HWND 可能由渲染进程或 TSF DLL 持有；任何一方都不拥有 UI 决策。
// 宿主窗口必须使用 WS_EX_LAYERED 且不调用 SetLayeredWindowAttributes：
// 视图通过 UpdateLayeredWindow 以逐像素 alpha 呈现自身。
// 动画运行期间，视图在窗口上占用 kAnimationTimer；宿主需将该
// WM_TIMER 转发给 onTimer()。
class CandidateView final
{
public:
	static constexpr UINT_PTR kAnimationTimer = 0xC0A1;

	struct Cell
	{
		RECT bounds{};
		RECT number{};
		RECT text{};
		std::size_t index = 0;
		std::size_t row = 0;
		std::size_t ordinal = 0;
	};

	void update(HWND window, const composition::CandidateRenderSnapshot& snapshot);
	void clear();
	// 使面板（不含阴影）紧靠锚点的窗口位置。
	POINT place(const RECT& anchor, const RECT& work, int gap) const;
	void present(HWND window, POINT position) const;
	void redraw(HWND window) const;
	void onTimer(HWND window) const { render(window, nullptr); }
	// 绘制到已选入 dc 的 width() x height() 的自顶向下 32bpp DIB 中，
	// 预乘，且已应用背景不透明度。
	void draw(HDC dc, std::uint32_t* pixels) const;
	int hit(POINT point) const;
	// 候选词条与展开按钮使用手型光标。
	bool clickableAt(POINT point) const;
	RECT expandRect() const;
	RECT scrollTrack() const;
	RECT scrollThumb() const;
	bool nearEnd() const;

	// 当悬停部位发生变化且窗口需要重绘时返回 true。
	bool setPointer(POINT point);
	void clearPointer();
	void setScrollDragging(bool dragging) { m_scrollDragging = dragging; }
	// 覆盖由主题推导的皮肤；std::nullopt 恢复浅色/深色。
	void setSkin(std::optional<CandidateSkin> skin) { m_customSkin = std::move(skin); }

	const composition::CandidateRenderSnapshot& snapshot() const { return m_snapshot; }
	const std::vector<std::uint32_t>& rowStarts() const { return m_rowStarts; }
	const std::vector<std::uint32_t>& itemCenters() const { return m_itemCenters; }
	int width() const { return m_width; }
	int height() const { return m_height; }
	RECT panelRect() const { return m_panel; }
	int topRow() const { return m_topRow; }
	int visibleRows() const { return m_visibleRows; }
	int rowCount() const { return static_cast<int>(m_rowStarts.size()); }
	int maxTopRow() const { return (std::max)(0, rowCount() - m_visibleRows); }
	// 无缓动直接跳转（滚动条拖动与轨道点击跟随指针）。
	void setTopRow(int row);
	// 缓动到新行（鼠标滚轮）。
	void scrollRows(int delta);
	int hover() const { return m_hover; }
	void setHover(int index) { m_hover = index; }

private:
	using Clock = std::chrono::steady_clock;

	// 最多四个值的缓动过渡（一个矩形，或一个滚动偏移）。
	struct Motion
	{
		float from[4]{};
		float to[4]{};
		Clock::time_point start{};
		int duration = 0;

		float at(int i, Clock::time_point now) const;
		bool active(Clock::time_point now) const;
		void snap(const float (&value)[4]);
		void moveTo(const float (&value)[4], int milliseconds, Clock::time_point now);
	};

	int scaled(int value) const;
	int chrome(float value) const;
	int rowOffset() const { return m_topRow * scaled(m_skin.rowHeight); }
	std::size_t selectedRow() const;
	void selectionPill(float (&pill)[4]) const;
	void buildBackground();
	void render(HWND window, const POINT* position) const;

	composition::CandidateRenderSnapshot m_snapshot;
	std::optional<CandidateSkin> m_customSkin;
	CandidateSkin m_skin;
	std::vector<Cell> m_cells;
	std::vector<std::uint32_t> m_rowStarts;
	std::vector<std::uint32_t> m_itemCenters;
	std::vector<std::uint32_t> m_background;
	// 缩放到完全展开面板的图像背景，尺寸未变时重复使用。
	std::vector<std::uint32_t> m_imageFill;
	std::shared_ptr<const SkinImage> m_imageFillSource;
	SIZE m_imageFillSize{};
	RECT m_panel{};
    RECT m_hintRect{};
    std::wstring m_hintText;
	int m_contentRight = 0;
	int m_width = 0;
	int m_height = 0;
	int m_topRow = 0;
	int m_visibleRows = 1;
	int m_hover = -1;
	bool m_hoverExpand = false;
	bool m_hoverScroll = false;
	bool m_scrollDragging = false;
	UINT m_dpi = 96;
	Motion m_highlight;
	Motion m_scroll;
	mutable HWND m_timerWindow = nullptr;
};
} // 命名空间 coral::win32 结束

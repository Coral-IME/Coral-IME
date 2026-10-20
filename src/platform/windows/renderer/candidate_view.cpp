#include "candidate_view.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace coral::win32
{
namespace
{
using namespace soft;

// 列表布局（剪贴板历史）：更窄的单列面板，行右侧为选中行的
// 回车徽标预留空间。
constexpr int kListPanelWidth = 400;
constexpr int kListVisibleRows = 6;
constexpr int kListBadgeWidth = 22;

std::wstring ToWide(const std::string& value)
{
	if (value.empty()) return {};
	const int length = ::MultiByteToWideChar(CP_UTF8, 0, value.data(),
		static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(length, L'\0');
	::MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
		result.data(), length);
	return result;
}

int Scale(int value, UINT dpi, int fontSize)
{
	if (value <= 0) return 0;
	return (std::max)(1, ::MulDiv(::MulDiv(value, static_cast<int>(dpi), 96),
		(std::clamp)(fontSize, 12, 28), 16));
}

UINT WindowDpi(HWND window)
{
	using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
	static auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
		::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
	return getDpi ? getDpi(window) : 96;
}

std::wstring FontName(const composition::CandidateRenderSnapshot& snapshot)
{
	std::wstring name = ToWide(snapshot.fontName);
	return name.empty() ? L"Microsoft YaHei UI" : name;
}

int WordPixels(const composition::CandidateRenderSnapshot& snapshot, UINT dpi)
{
	return ::MulDiv((std::clamp)(snapshot.fontSize, 10, 48), static_cast<int>(dpi), 96);
}

std::wstring EllipsizeMiddle(HDC dc, const std::wstring& text, int maxWidth)
{
	SIZE size{};
	if (text.empty() || (::GetTextExtentPoint32W(dc, text.c_str(),
		static_cast<int>(text.size()), &size) && size.cx <= maxWidth)) return text;
	const std::wstring ellipsis = L"…";
	std::size_t low = 0;
	std::size_t high = text.size();
	std::wstring best = ellipsis;
	while (low <= high)
	{
		const std::size_t keep = low + (high - low) / 2;
		const std::size_t prefix = (keep + 1) / 2;
		const std::size_t suffix = keep / 2;
		std::wstring value = text.substr(0, prefix) + ellipsis +
			text.substr(text.size() - suffix);
		SIZE measured{};
		::GetTextExtentPoint32W(dc, value.c_str(),
			static_cast<int>(value.size()), &measured);
		if (measured.cx <= maxWidth)
		{
			best = std::move(value);
			low = keep + 1;
		}
		else
		{
			if (!keep) break;
			high = keep - 1;
		}
	}
	return best;
}
} // namespace

CandidateSkin CandidateSkin::light()
{
	return {};
}

CandidateSkin CandidateSkin::dark()
{
	CandidateSkin skin;
	skin.panel = 0xFF2B2C30;
	skin.border = 0x24FFFFFF;
	skin.text = 0xFFE8E9EC;
	skin.index = 0xFF9097A1;
	skin.selectedFill = 0xFF2E4266;
	skin.selectedRing = 0x338CB4FF;
	skin.selectedText = 0xFFB8D2FF;
	skin.selectedIndex = 0xA6B8D2FF;
	skin.rowDivider = 0x0DFFFFFF;
	skin.toggleIcon = 0xFF8D939C;
	skin.toggleIconHover = 0xFFD8DBE0;
	skin.toggleHoverFill = 0x14FFFFFF;
	skin.scrollThumb = 0x3DFFFFFF;
	skin.scrollThumbActive = 0x6BFFFFFF;
	skin.shadows[0] = { 1.0f, 3.0f, 0.0f, 0x38000000 };
	skin.shadows[1] = { 6.0f, 20.0f, -4.0f, 0x6B000000 };
	return skin;
}

CandidateSkin CandidateSkin::themed(const std::string& theme, const std::string& skin,
	const std::string& imagePath)
{
	return themed(DarkTheme(theme), CandidateThemeConfig::parse(skin), Utf8ToWide(imagePath));
}

CandidateSkin CandidateSkin::themed(bool darkMode, const CandidateThemeConfig& config,
	const std::wstring& imagePath)
{
	CandidateSkin skin = darkMode ? dark() : light();
	skin.opacity = static_cast<float>((std::clamp)(config.opacity, kCandidateOpacityMin, 100)) /
		100.0f;
	// 旧的淡蓝预设保留其手工调校的基础配色。
	if (config.kind == CandidateBackground::Solid && config.solid == 0 && !config.customPalette)
		return skin;
	const CandidatePalette palette = config.effectivePalette(darkMode);
	const auto tint = [](std::uint32_t color, std::uint32_t alpha) {
		return (color & 0x00FFFFFFu) | (alpha << 24);
	};
	skin.text = palette.text;
	skin.index = tint(palette.text, 0x8C);
	skin.selectedText = palette.selectedText;
	skin.selectedIndex = tint(palette.selectedText, 0xA6);
	skin.selectedFill = palette.selectedBackground;
	skin.selectedRing = tint(palette.selectedText, 0x29);
	skin.rowDivider = tint(palette.text, 0x12);
	skin.toggleIcon = tint(palette.text, 0x80);
	skin.toggleIconHover = tint(palette.text, 0xE0);
	skin.toggleHoverFill = tint(palette.text, 0x12);
	skin.scrollThumb = tint(palette.text, 0x38);
	skin.scrollThumbActive = tint(palette.text, 0x61);
	skin.panel = palette.background;
	if (config.kind == CandidateBackground::Gradient)
	{
		skin.background = CandidateBackground::Gradient;
		config.gradientColors(darkMode, skin.gradientFrom, skin.gradientTo);
		skin.panel = Lerp(skin.gradientFrom, skin.gradientTo, 0.5f);
	}
	else if (config.kind == CandidateBackground::Image)
	{
		skin.image = LoadSkinImage(imagePath);
		if (skin.image)
		{
			skin.background = CandidateBackground::Image;
			skin.imageVeil = darkMode ? 0x9E141519 : 0x26FFFFFF;
			skin.imageBase = palette.background;
			skin.imageOpacity = (std::clamp)(config.imageOpacity, 0, 100);
			skin.panel = Over(skin.imageVeil, BlendCandidateImage(skin.image->average,
				skin.imageBase, skin.imageOpacity));
		}
	}
	return skin;
}

int CandidateView::scaled(int value) const
{
	return Scale(value, m_dpi, m_snapshot.fontSize);
}

int CandidateView::chrome(float value) const
{
	return (std::max)(1, static_cast<int>(std::lround(value * static_cast<float>(m_dpi) / 96.0f)));
}

std::size_t CandidateView::selectedRow() const
{
	if (m_rowStarts.empty()) return 0;
	return static_cast<std::size_t>(std::upper_bound(m_rowStarts.begin(),
		m_rowStarts.end(), m_snapshot.selection) - m_rowStarts.begin() - 1);
}

float CandidateView::Motion::at(int i, Clock::time_point now) const
{
	if (duration <= 0 || now >= start + std::chrono::milliseconds(duration)) return to[i];
	const float t = std::chrono::duration<float, std::milli>(now - start).count() /
		static_cast<float>(duration);
	const float remaining = 1.0f - (std::clamp)(t, 0.0f, 1.0f);
	return from[i] + (to[i] - from[i]) * (1.0f - remaining * remaining * remaining);
}

bool CandidateView::Motion::active(Clock::time_point now) const
{
	if (duration <= 0 || now >= start + std::chrono::milliseconds(duration)) return false;
	return !std::equal(std::begin(from), std::end(from), std::begin(to));
}

void CandidateView::Motion::snap(const float (&value)[4])
{
	std::copy(std::begin(value), std::end(value), std::begin(from));
	std::copy(std::begin(value), std::end(value), std::begin(to));
	duration = 0;
}

void CandidateView::Motion::moveTo(const float (&value)[4], int milliseconds,
	Clock::time_point now)
{
	if (std::equal(std::begin(value), std::end(value), std::begin(to))) return;
	// 动画途中改目标（按键连击）会缩短下一段行程，使高亮
	// 在快速导航时不会远远滞后。
	if (active(now)) milliseconds = milliseconds * 2 / 3;
	for (int i = 0; i < 4; ++i) from[i] = at(i, now);
	std::copy(std::begin(value), std::end(value), std::begin(to));
	start = now;
	duration = milliseconds;
}

void CandidateView::selectionPill(float (&pill)[4]) const
{
	pill[0] = pill[1] = pill[2] = pill[3] = 0.0f;
	if (m_snapshot.selection >= m_cells.size()) return;
	const RECT& bounds = m_cells[m_snapshot.selection].bounds;
	const int insetX = scaled(m_skin.itemGap) / 2;
	const int insetY = scaled(m_skin.itemInsetY);
	pill[0] = static_cast<float>(bounds.left + insetX);
	pill[1] = static_cast<float>(bounds.top + insetY);
	pill[2] = static_cast<float>(bounds.right - insetX);
	pill[3] = static_cast<float>(bounds.bottom - insetY);
}

void CandidateView::setTopRow(int row)
{
	m_topRow = (std::clamp)(row, 0, maxTopRow());
	const float offset[4]{ static_cast<float>(rowOffset()) };
	m_scroll.snap(offset);
}

void CandidateView::scrollRows(int delta)
{
	m_topRow = (std::clamp)(m_topRow + delta, 0, maxTopRow());
	const float offset[4]{ static_cast<float>(rowOffset()) };
	m_scroll.moveTo(offset, m_skin.scrollAnimationMs, Clock::now());
}

void CandidateView::clear()
{
	if (m_timerWindow) ::KillTimer(m_timerWindow, kAnimationTimer);
	m_timerWindow = nullptr;
	m_highlight = {};
	m_scroll = {};
	m_snapshot = {};
	m_cells.clear();
	m_rowStarts.clear();
	m_itemCenters.clear();
	m_background.clear();
	m_panel = {};
    m_hintRect = {};
    m_hintText.clear();
	m_contentRight = 0;
	m_width = 0;
	m_height = 0;
	m_topRow = 0;
	m_visibleRows = 1;
	clearPointer();
	m_scrollDragging = false;
}

void CandidateView::update(HWND window, const composition::CandidateRenderSnapshot& snapshot)
{
	// 只有未变化（或仅追加）列表内的选中移动才做动画；
	// 新输入、展开/折叠与尺寸变化都直接跳到新布局。
	const auto& previous = m_snapshot.candidates;
	bool continuous = !m_cells.empty() && m_snapshot.expanded == snapshot.expanded &&
		m_snapshot.fontSize == snapshot.fontSize &&
		m_snapshot.context.serviceInstanceId == snapshot.context.serviceInstanceId &&
		m_snapshot.context.contextId == snapshot.context.contextId &&
		m_snapshot.context.contextGeneration == snapshot.context.contextGeneration &&
		previous.size() <= snapshot.candidates.size();
	for (std::size_t i = 0; continuous && i < previous.size(); ++i)
		continuous = previous[i].id == snapshot.candidates[i].id;
	const std::size_t previousRow = selectedRow();
	const int previousWidth = m_width;
	const int previousHeight = m_height;
	m_snapshot = snapshot;
    m_hintText = ToWide(snapshot.hint);
    m_hintRect = {};
	m_dpi = WindowDpi(window);
	if (!m_dpi) m_dpi = 96;
	m_skin = m_customSkin ? *m_customSkin :
		CandidateSkin::themed(snapshot.theme, snapshot.skin, snapshot.skinImage);
	clearPointer();
	m_cells.clear();
	m_rowStarts.clear();
	m_itemCenters.clear();
	if (snapshot.candidates.empty() && snapshot.hint.empty())
	{
		m_background.clear();
		m_panel = {};
		m_width = 0;
		m_height = 0;
		m_topRow = 0;
		return;
	}
	const CandidateSkin& skin = m_skin;
	const bool list = snapshot.listLayout;
	const int paddingX = scaled(skin.paddingX);
	const int paddingY = scaled(skin.paddingY);
	const int rowHeight = scaled(skin.rowHeight);
	const int itemPadding = scaled(skin.itemPaddingX);
	const int indexGap = list ? 0 : scaled(skin.indexGap);
	const int panelWidth = scaled(list ? kListPanelWidth : skin.panelWidth);
	// 两种模式下跨度都按折叠宽度度量，因此展开网格只拉伸、
	// 在候选追加时保持稳定。
	const int available = (std::max)(1, panelWidth - paddingX * 2 -
		scaled(skin.toggleGap) - scaled(skin.toggleSize));
	const int columns = (std::clamp)(available / (std::max)(1, scaled(skin.minItemWidth)),
		1, (std::max)(1, skin.maxColumns));
	const int collapsedColumns = (std::max)(columns, skin.collapsedMaxColumns);

	HDC dc = ::GetDC(window);
	const std::wstring fontName = FontName(snapshot);
	ScopedFont wordFont(fontName, WordPixels(snapshot, m_dpi), skin.fontWeight);
	ScopedFont indexFont(skin.indexFontName.empty() ? fontName : skin.indexFontName,
		scaled(skin.indexFontSize), FW_NORMAL);
	HGDIOBJ oldFont = ::SelectObject(dc, indexFont.get());
	SIZE digit{};
	if (!list)
		::GetTextExtentPoint32W(dc, snapshot.letterSelection ? L"W" : L"8", 1, &digit);
    int hintHeight = 0;
    if (!m_hintText.empty()) {
        RECT bounds{0, 0, panelWidth - paddingX * 2 - itemPadding * 2, 0};
        ::DrawTextW(dc, m_hintText.c_str(), static_cast<int>(m_hintText.size()), &bounds,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        hintHeight = bounds.bottom + scaled(8);
    }
	::SelectObject(dc, wordFont.get());
	const std::size_t count = snapshot.candidates.size();
	std::vector<int> desired(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		const std::wstring word = ToWide(snapshot.candidates[i].text);
		SIZE word_size{};
		::GetTextExtentPoint32W(dc, word.c_str(), static_cast<int>(word.size()), &word_size);
		desired[i] = itemPadding * 2 + static_cast<int>(digit.cx) + indexGap +
			static_cast<int>(word_size.cx);
	}
	::SelectObject(dc, oldFont);
	::ReleaseDC(window, dc);

	const auto spanOf = [&](std::size_t i, int grid) {
		return (std::clamp)((desired[i] * grid + available - 1) / available, 1, grid);
	};
	const auto rowEnd = [&](std::size_t start, int grid) {
		int column = 0;
		std::size_t i = start;
		for (; i < count; ++i)
		{
			const int span = spanOf(i, grid);
			if (column && column + span > grid) break;
			column += span;
		}
		return i;
	};
	struct Slot
	{
		int column;
		int span;
		int grid;
	};
	std::vector<Slot> slots;
	slots.reserve(count);
	for (std::size_t start = 0, row = 0; start < count; ++row)
	{
		// 折叠行只显示单行，因此每行在展开网格与 collapsedMaxColumns 之间
		// 选择能容纳最多词语的那个网格。
		int grid = list ? 1 : columns;
		std::size_t end = list ? start + 1 : rowEnd(start, grid);
		for (int candidate = columns + 1; !list && !snapshot.expanded && candidate <= collapsedColumns;
			++candidate)
		{
			const std::size_t candidateEnd = rowEnd(start, candidate);
			if (candidateEnd > end)
			{
				grid = candidate;
				end = candidateEnd;
			}
		}
		m_rowStarts.push_back(static_cast<std::uint32_t>(start));
		for (int column = 0; start < end; ++start)
		{
			Cell cell;
			cell.index = start;
			cell.row = row;
			cell.ordinal = m_cells.size() - m_rowStarts.back();
			m_cells.push_back(cell);
			const int span = list ? 1 : spanOf(start, grid);
			slots.push_back({ column, span, grid });
			column += span;
		}
	}

	const int current = static_cast<int>(selectedRow());
	if (!snapshot.expanded)
	{
		m_topRow = (std::max)(0, current);
		m_visibleRows = 1;
	}
	else
	{
		const int maxRows = list ? kListVisibleRows : skin.maxVisibleRows;
		m_visibleRows = (std::min)((std::max)(1, maxRows), rowCount());
		m_topRow = (std::clamp)(m_topRow, 0, maxTopRow());
		if (current < m_topRow) m_topRow = current;
		else if (current >= m_topRow + m_visibleRows)
			m_topRow = current - m_visibleRows + 1;
	}
	if (count == 0) { m_visibleRows = 0; m_topRow = 0; }
	// 展开列表隐藏切换按钮并填满面板。
	// 在可能还有更多候选到达时保留滚动条沟槽，
	// 使追加的行不会移动用户正在注视的列。
	const bool scrolls = rowCount() > m_visibleRows || snapshot.hasMore;
	const int contentWidth = !snapshot.expanded ? available :
		panelWidth - paddingX * 2 - (scrolls ? scaled(skin.scrollGutter) : 0);
	const int panelHeight = paddingY * 2 + m_visibleRows * rowHeight + hintHeight;

	const float scale = static_cast<float>(m_dpi) / 96.0f;
	float marginLeft = 0.0f;
	float marginTop = 0.0f;
	float marginBottom = 0.0f;
	for (const CandidateShadow& shadow : skin.shadows)
	{
		if (Alpha(shadow.color) <= 0.0f) continue;
		const float reach = ShadowReach(shadow, scale);
		marginLeft = (std::max)(marginLeft, reach);
		marginTop = (std::max)(marginTop, reach - shadow.offsetY * scale);
		marginBottom = (std::max)(marginBottom, reach + shadow.offsetY * scale);
	}
	const int sideMargin = static_cast<int>(std::ceil(marginLeft));
	const int topMargin = static_cast<int>(std::ceil(marginTop));
	m_panel = { sideMargin, topMargin, sideMargin + panelWidth, topMargin + panelHeight };
	m_width = panelWidth + sideMargin * 2;
	m_height = panelHeight + topMargin + static_cast<int>(std::ceil(marginBottom));

    if (hintHeight) m_hintRect = {m_panel.left + paddingX + itemPadding,
        m_panel.top + paddingY + m_visibleRows * rowHeight + scaled(4),
        m_panel.right - paddingX - itemPadding, m_panel.bottom - paddingY - scaled(4)};
	const int originX = m_panel.left + paddingX;
	const int originY = m_panel.top + paddingY;
	m_contentRight = originX + contentWidth;
	for (std::size_t i = 0; i < m_cells.size(); ++i)
	{
		Cell& cell = m_cells[i];
		const Slot& slot = slots[i];
		const int left = originX + contentWidth * slot.column / slot.grid;
		const int right = originX + contentWidth * (slot.column + slot.span) / slot.grid;
		const int top = originY + static_cast<int>(cell.row) * rowHeight;
		cell.bounds = { left, top, right, top + rowHeight };
		cell.number = { left + itemPadding, top,
			left + itemPadding + static_cast<int>(digit.cx), top + rowHeight };
		cell.text = { cell.number.right + indexGap, top,
			right - itemPadding - (list ? scaled(kListBadgeWidth) : 0), top + rowHeight };
		m_itemCenters.push_back(static_cast<std::uint32_t>((left + right) / 2));
	}
	buildBackground();

	const bool animate = continuous && m_width == previousWidth && m_height == previousHeight &&
		(snapshot.expanded || selectedRow() == previousRow);
	float pill[4];
	selectionPill(pill);
	const float offset[4]{ static_cast<float>(rowOffset()) };
	if (animate)
	{
		const auto now = Clock::now();
		m_highlight.moveTo(pill, skin.selectionAnimationMs, now);
		m_scroll.moveTo(offset, skin.scrollAnimationMs, now);
	}
	else
	{
		m_highlight.snap(pill);
		m_scroll.snap(offset);
	}
}

void CandidateView::buildBackground()
{
	m_background.assign(static_cast<std::size_t>(m_width) * m_height, 0);
	Surface surface(m_background.data(), m_width, m_height);
	const float scale = static_cast<float>(m_dpi) / 96.0f;
	const RectF panel = ToRectF(m_panel);
	const float radius = static_cast<float>(scaled(m_skin.panelRadius));
	for (const CandidateShadow& shadow : m_skin.shadows)
		DrawShadow(surface, panel, radius, shadow, scale);
	const int panelWidth = m_panel.right - m_panel.left;
	const int panelHeight = m_panel.bottom - m_panel.top;
	if (m_skin.background == CandidateBackground::Gradient)
	{
		const float width = static_cast<float>((std::max)(1, panelWidth));
		const float height = static_cast<float>((std::max)(1, panelHeight));
		ShadeRoundRect(surface, panel, radius, [&](int x, int y) {
			const float t = 0.85f * static_cast<float>(x - m_panel.left) / width +
				0.15f * static_cast<float>(y - m_panel.top) / height;
			return Lerp(m_skin.gradientFrom, m_skin.gradientTo, t);
		});
	}
	else if (m_skin.background == CandidateBackground::Image && m_skin.image &&
		panelWidth > 0 && panelHeight > 0)
	{
		const int fillHeight = (std::max)(panelHeight, scaled(m_skin.paddingY) * 2 +
			(std::max)(1, m_skin.maxVisibleRows) * scaled(m_skin.rowHeight));
		if (m_imageFillSource != m_skin.image || m_imageFillSize.cx != panelWidth ||
			m_imageFillSize.cy != fillHeight)
		{
			m_imageFill = CoverSkinImage(*m_skin.image, panelWidth, fillHeight);
			m_imageFillSource = m_skin.image;
			m_imageFillSize = { panelWidth, fillHeight };
		}
		ShadeRoundRect(surface, panel, radius, [&](int x, int y) {
			const int column = (std::clamp)(x - static_cast<int>(m_panel.left), 0, panelWidth - 1);
			const int row = (std::clamp)(y - static_cast<int>(m_panel.top), 0, fillHeight - 1);
			return Over(m_skin.imageVeil, BlendCandidateImage(
				m_imageFill[static_cast<std::size_t>(row) * panelWidth + column],
				m_skin.imageBase, m_skin.imageOpacity));
		});
	}
	else
	{
		FillRoundRect(surface, panel, radius, m_skin.panel);
	}
	// 在添加边框和控件之前先缩放预乘背景。
	const unsigned strength = static_cast<unsigned>(std::lround(
		(std::clamp)(m_skin.opacity, 0.0f, 1.0f) * 255.0f));
	for (auto& pixel : m_background)
	{
		const auto channel = [&](int shift) {
			return (((pixel >> shift) & 0xFFu) * strength + 127) / 255 << shift;
		};
		pixel = channel(24) | channel(16) | channel(8) | channel(0);
	}
	StrokeRoundRect(surface, panel, radius,
		static_cast<float>(chrome(m_skin.borderWidth)), m_skin.border);
}

POINT CandidateView::place(const RECT& anchor, const RECT& work, int gap) const
{
	const int panelWidth = m_panel.right - m_panel.left;
	const int panelHeight = m_panel.bottom - m_panel.top;
	const int x = (std::clamp)(static_cast<int>(anchor.left), static_cast<int>(work.left),
		(std::max)(static_cast<int>(work.left), static_cast<int>(work.right) - panelWidth));
	int y = anchor.bottom + gap;
	if (y + panelHeight > work.bottom) y = anchor.top - gap - panelHeight;
	y = (std::clamp)(y, static_cast<int>(work.top),
		(std::max)(static_cast<int>(work.top), static_cast<int>(work.bottom) - panelHeight));
	return { x - m_panel.left, y - m_panel.top };
}

int CandidateView::hit(POINT point) const
{
	if (!::PtInRect(&m_panel, point)) return -1;
	point.y += rowOffset();
	for (const Cell& cell : m_cells)
	{
		if (cell.row < static_cast<std::size_t>(m_topRow) ||
			cell.row >= static_cast<std::size_t>(m_topRow + m_visibleRows)) continue;
		if (::PtInRect(&cell.bounds, point)) return static_cast<int>(cell.index);
	}
	return -1;
}

bool CandidateView::clickableAt(POINT point) const
{
	const RECT toggle = expandRect();
	return hit(point) >= 0 || ::PtInRect(&toggle, point);
}

RECT CandidateView::expandRect() const
{
	if (::IsRectEmpty(&m_panel) || m_snapshot.expanded || m_snapshot.candidates.empty()) return {};
	const int size = scaled(m_skin.toggleSize);
	const int right = m_panel.right - scaled(m_skin.paddingX);
	const int top = m_panel.top + scaled(m_skin.paddingY) +
		(scaled(m_skin.rowHeight) - size) / 2;
	return { right - size, top, right, top + size };
}

RECT CandidateView::scrollTrack() const
{
	const int center = (m_contentRight + m_panel.right) / 2;
	const int half = scaled(m_skin.scrollbarHitWidth) / 2;
	const int inset = scaled(m_skin.paddingY) + scaled(2);
	return { center - half, m_panel.top + inset, center + half, m_panel.top + scaled(m_skin.paddingY) + m_visibleRows * scaled(m_skin.rowHeight) - scaled(2) };
}

RECT CandidateView::scrollThumb() const
{
	const RECT track = scrollTrack();
	const int rows = rowCount();
	if (!m_snapshot.expanded || rows <= m_visibleRows || track.bottom <= track.top)
		return { 0, 0, 0, 0 };
	const int trackHeight = track.bottom - track.top;
	const int thumbHeight = (std::min)(trackHeight,
		(std::max)(scaled(24), trackHeight * m_visibleRows / rows));
	const int travel = trackHeight - thumbHeight;
	const int top = track.top + travel * m_topRow / maxTopRow();
	return { track.left, top, track.right, top + thumbHeight };
}

bool CandidateView::nearEnd() const
{
	return m_topRow + m_visibleRows + 2 >= rowCount();
}

bool CandidateView::setPointer(POINT point)
{
	const RECT toggle = expandRect();
	const bool expand = ::PtInRect(&toggle, point) != FALSE;
	const RECT thumb = scrollThumb();
	const RECT track = scrollTrack();
	const bool scroll = !::IsRectEmpty(&thumb) && ::PtInRect(&track, point);
	// 悬停候选只用手指光标标记，不重绘。
	const bool changed = expand != m_hoverExpand || scroll != m_hoverScroll;
	m_hover = hit(point);
	m_hoverExpand = expand;
	m_hoverScroll = scroll;
	return changed;
}

void CandidateView::clearPointer()
{
	m_hover = -1;
	m_hoverExpand = false;
	m_hoverScroll = false;
}

void CandidateView::present(HWND window, POINT position) const
{
	render(window, &position);
}

void CandidateView::redraw(HWND window) const
{
	render(window, nullptr);
}

void CandidateView::render(HWND window, const POINT* position) const
{
	const std::size_t count = static_cast<std::size_t>(m_width) * m_height;
	if (!window || !count || m_background.size() != count) return;
	BITMAPINFO info{};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = m_width;
	info.bmiHeader.biHeight = -m_height;
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
	HGDIOBJ oldBitmap = ::SelectObject(dc, bitmap);
	draw(dc, static_cast<std::uint32_t*>(bits));

	SIZE size{ m_width, m_height };
	POINT source{ 0, 0 };
	POINT destination = position ? *position : POINT{};
	BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	::UpdateLayeredWindow(window, screen, position ? &destination : nullptr, &size, dc,
		&source, 0, &blend, ULW_ALPHA);
	::SelectObject(dc, oldBitmap);
	::DeleteObject(bitmap);
	::DeleteDC(dc);
	::ReleaseDC(nullptr, screen);

	const auto now = Clock::now();
	const bool moving = m_highlight.active(now) || m_scroll.active(now);
	if (moving && m_timerWindow != window)
	{
		if (m_timerWindow) ::KillTimer(m_timerWindow, kAnimationTimer);
		::SetTimer(window, kAnimationTimer, USER_TIMER_MINIMUM, nullptr);
		m_timerWindow = window;
	}
	else if (!moving && m_timerWindow)
	{
		::KillTimer(m_timerWindow, kAnimationTimer);
		m_timerWindow = nullptr;
	}
}

void CandidateView::draw(HDC dc, std::uint32_t* pixels) const
{
	const std::size_t count = static_cast<std::size_t>(m_width) * m_height;
	if (!dc || !pixels || !count || m_background.size() != count) return;
	std::memcpy(pixels, m_background.data(), count * sizeof(std::uint32_t));
	Surface surface(pixels, m_width, m_height);

	const CandidateSkin& skin = m_skin;
	const auto now = Clock::now();
	const int rowHeight = scaled(skin.rowHeight);
	const float scrollY = m_scroll.at(0, now);
	const int offset = static_cast<int>(std::lround(scrollY));
	const int firstRow = (std::max)(0, static_cast<int>(std::floor(scrollY / rowHeight)));
	const int lastRow = static_cast<int>(std::ceil((scrollY + m_visibleRows * rowHeight) /
		rowHeight));
	const int contentTop = m_panel.top + scaled(skin.paddingY);
	const RECT content{ m_panel.left, contentTop, m_panel.right,
		contentTop + m_visibleRows * rowHeight };
	const int insetY = scaled(skin.itemInsetY);
	const int insetX = scaled(skin.itemGap) / 2;
	const float itemRadius = static_cast<float>(scaled(skin.itemRadius));
	const float hairline = static_cast<float>(chrome(1.0f));
	const std::size_t activeRow = selectedRow();
	const auto visible = [firstRow, lastRow](const Cell& cell) {
		return cell.row >= static_cast<std::size_t>(firstRow) &&
			cell.row < static_cast<std::size_t>(lastRow);
	};
	const auto backgroundColor = [&](std::uint32_t color) {
		const unsigned alpha = static_cast<unsigned>(std::lround(
			(color >> 24) * (std::clamp)(skin.opacity, 0.0f, 1.0f)));
		return (color & 0x00FFFFFFu) | (alpha << 24);
	};
	const auto backdrop = [&](const Cell& cell) {
		return cell.index == m_snapshot.selection ? Over(skin.selectedFill, skin.panel) : skin.panel;
	};

	surface.setClip(content);
	if (m_snapshot.expanded && Alpha(skin.rowDivider) > 0.0f)
	{
		const float left = static_cast<float>(m_panel.left + scaled(skin.paddingX) +
			scaled(skin.itemPaddingX) / 2);
		const float right = static_cast<float>(m_contentRight - scaled(skin.itemPaddingX) / 2);
		for (int row = (std::max)(1, firstRow); row <= lastRow; ++row)
		{
			const float y = static_cast<float>(contentTop + row * rowHeight) - scrollY;
			if (y <= content.top || y >= content.bottom - hairline) continue;
			FillRoundRect(surface, { left, y, right, y + hairline }, 0.0f, skin.rowDivider);
		}
	}
	if (m_snapshot.selection < m_cells.size())
	{
		const RectF pill{ m_highlight.at(0, now), m_highlight.at(1, now) - scrollY,
			m_highlight.at(2, now), m_highlight.at(3, now) - scrollY };
		FillRoundRect(surface, pill, itemRadius, backgroundColor(skin.selectedFill));
		StrokeRoundRect(surface, pill, itemRadius, hairline, skin.selectedRing);
		if (m_snapshot.listLayout)
		{
			// 回车徽标：选中行按回车键上屏。
			const float unit = static_cast<float>(scaled(16)) / 16.0f;
			const float cx = pill.right - static_cast<float>(scaled(skin.itemPaddingX)) -
				static_cast<float>(scaled(kListBadgeWidth)) * 0.5f;
			const float cy = (pill.top + pill.bottom) * 0.5f;
			const float stem[] = { cx + 4.5f * unit, cy - 4.5f * unit, cx + 4.5f * unit, cy + 1.5f * unit,
				cx - 4.5f * unit, cy + 1.5f * unit };
			const float head[] = { cx - 1.5f * unit, cy - 1.5f * unit, cx - 4.5f * unit, cy + 1.5f * unit,
				cx - 1.5f * unit, cy + 4.5f * unit };
			StrokePolyline(surface, stem, 3, 1.5f * unit, skin.selectedIndex);
			StrokePolyline(surface, head, 3, 1.5f * unit, skin.selectedIndex);
		}
	}
	surface.resetClip();

	const RECT toggle = expandRect();
	if (!::IsRectEmpty(&toggle))
	{
		if (m_hoverExpand)
			FillRoundRect(surface, ToRectF(toggle),
				static_cast<float>(scaled(skin.toggleRadius)), backgroundColor(skin.toggleHoverFill));
		const float unit = static_cast<float>(scaled(16)) / 16.0f;
		DrawChevron(surface, (toggle.left + toggle.right) * 0.5f,
			(toggle.top + toggle.bottom) * 0.5f, static_cast<float>(scaled(skin.chevronSize)),
			skin.chevronStroke * unit, false,
			m_hoverExpand ? skin.toggleIconHover : skin.toggleIcon);
	}

	const RECT thumb = scrollThumb();
	if (!::IsRectEmpty(&thumb))
	{
		const RECT track = scrollTrack();
		const float thumbHeight = static_cast<float>(thumb.bottom - thumb.top);
		const float travel = static_cast<float>(track.bottom - track.top) - thumbHeight;
		const float progress = (std::clamp)(scrollY /
			static_cast<float>((std::max)(1, maxTopRow() * rowHeight)), 0.0f, 1.0f);
		const float top = static_cast<float>(track.top) + travel * progress;
		const bool active = m_hoverScroll || m_scrollDragging;
		const float width = static_cast<float>(scaled(active ? skin.scrollbarActiveWidth :
			skin.scrollbarWidth));
		const float center = (thumb.left + thumb.right) * 0.5f;
		FillRoundRect(surface, { center - width * 0.5f, top, center + width * 0.5f,
			top + thumbHeight }, width * 0.5f,
			active ? skin.scrollThumbActive : skin.scrollThumb);
	}

	// 半透明面板使用灰度字形掩码，再合成不透明墨色。
	// 不透明面板保留 ClearType；它假设背景不透明。
	const bool translucent = skin.opacity < 1.0f;
	std::vector<std::uint32_t> composed;
	if (translucent) {
		composed.assign(pixels, pixels + count);
		// GDI 的黑底白字抗锯齿经过伽马校正，会为深色墨产生更重的
		// 覆盖掩码。改为匹配原生的深墨浅底文字，
		// 并在合成墨色时反转灰度掩码。
		std::fill(pixels, pixels + count, 0xFFFFFFFFu);
	}
	const auto paintText = [&](const std::wstring& text, RECT rect, UINT format,
		std::uint32_t color, RECT clip) {
		RECT bounds{};
		const RECT canvas{ 0, 0, m_width, m_height };
		::IntersectRect(&bounds, &rect, &clip);
		::IntersectRect(&bounds, &bounds, &canvas);
		const int saved = translucent ? ::SaveDC(dc) : 0;
		if (translucent && !saved) return;
		if (translucent)
			::IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
		::SetTextColor(dc, translucent ? RGB(0, 0, 0) : ToColorRef(color));
		::DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
		if (!translucent) return;
		if (saved) ::RestoreDC(dc, saved);
		::GdiFlush();
		Surface ink(composed.data(), m_width, m_height);
		ink.paint(ToRectF(bounds), color, [&](float x, float y) {
			const auto index = static_cast<std::size_t>(y) * m_width + static_cast<int>(x);
			const auto mask = pixels[index];
			pixels[index] = 0xFFFFFFFFu;
			return static_cast<float>(255u - (mask & 0xFFu)) / 255.0f;
		});
	};
	// GDI 文字会清掉它触碰到的每个像素的 alpha 字节。
	std::vector<std::uint8_t> alpha;
	if (!translucent) {
		alpha.resize(count);
		for (std::size_t i = 0; i < count; ++i)
			alpha[i] = static_cast<std::uint8_t>(pixels[i] >> 24);
	}
	const std::wstring fontName = FontName(m_snapshot);
	const DWORD quality = translucent ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
	ScopedFont wordFont(fontName, WordPixels(m_snapshot, m_dpi), skin.fontWeight, quality);
	ScopedFont indexFont(skin.indexFontName.empty() ? fontName : skin.indexFontName,
		scaled(skin.indexFontSize), FW_NORMAL, quality);
	HGDIOBJ oldFont = ::SelectObject(dc, indexFont.get());
	::SetBkMode(dc, TRANSPARENT);
	::IntersectClipRect(dc, content.left, content.top, content.right, content.bottom);
	constexpr UINT kTextFormat = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX;
	for (const Cell& cell : m_cells)
	{
		if (m_snapshot.listLayout || cell.row != activeRow || !visible(cell)) continue;
		const bool selected = cell.index == m_snapshot.selection;
		const std::uint32_t color = selected ? skin.selectedIndex : skin.index;
		RECT number = cell.number;
		::OffsetRect(&number, 0, -offset);
		const std::wstring label = m_snapshot.letterSelection
            ? std::wstring(1, static_cast<wchar_t>(L'A' + cell.ordinal))
            : std::to_wstring(cell.ordinal + 1);
		paintText(label, number, kTextFormat | DT_CENTER, Over(color, backdrop(cell)), content);
	}
	::SelectObject(dc, wordFont.get());
	for (const Cell& cell : m_cells)
	{
		if (!visible(cell)) continue;
		const bool selected = cell.index == m_snapshot.selection;
		RECT textRect = cell.text;
		::OffsetRect(&textRect, 0, -offset);
		const std::wstring text = ToWide(m_snapshot.candidates[cell.index].text);
		const std::wstring word = m_snapshot.listLayout ? text :
			EllipsizeMiddle(dc, text, textRect.right - textRect.left);
		paintText(word, textRect, kTextFormat | DT_LEFT | (m_snapshot.listLayout ? DT_END_ELLIPSIS : 0),
			Over(selected ? skin.selectedText : skin.text, backdrop(cell)), content);
	}
    ::SelectClipRgn(dc, nullptr);
    if (!m_hintText.empty()) {
        ::SelectObject(dc, indexFont.get());
        paintText(m_hintText, m_hintRect, DT_WORDBREAK | DT_NOPREFIX,
            Over(skin.index, skin.panel), { 0, 0, m_width, m_height });
    }
	::SelectObject(dc, oldFont);
	::GdiFlush();
	if (translucent)
		std::memcpy(pixels, composed.data(), count * sizeof(std::uint32_t));
	else
		for (std::size_t i = 0; i < count; ++i)
			pixels[i] = (pixels[i] & 0x00FFFFFFu) | (static_cast<std::uint32_t>(alpha[i]) << 24);
}
} // 命名空间 coral::win32 结束

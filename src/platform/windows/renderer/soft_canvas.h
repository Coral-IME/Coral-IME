#pragma once

// 由候选弹窗、TSF 快捷菜单和设置窗口共用的仅头文件抗锯齿光栅器。
// GDI 无法抗锯齿图形，也无法保留 alpha 通道，
// 因此图形先由 CPU 渲染到预乘 BGRA DIB 中，
// GDI 只在不透明区域之上绘制 ClearType 文字。

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace coral::win32::soft
{
// 颜色为直通（非预乘）0xAARRGGBB。
struct Shadow
{
	float offsetY = 0.0f;
	float blur = 0.0f;
	float spread = 0.0f;
	std::uint32_t color = 0;
};

struct RectF
{
	float left;
	float top;
	float right;
	float bottom;
};

inline RectF ToRectF(const RECT& rect)
{
	return { static_cast<float>(rect.left), static_cast<float>(rect.top),
		static_cast<float>(rect.right), static_cast<float>(rect.bottom) };
}

inline float Alpha(std::uint32_t color)
{
	return static_cast<float>((color >> 24) & 0xFF) / 255.0f;
}

// 把直通色合成到不透明色之上，返回不透明结果。
inline std::uint32_t Over(std::uint32_t top, std::uint32_t bottom)
{
	const float a = Alpha(top);
	const auto mix = [a, top, bottom](int shift) {
		const float value = static_cast<float>((top >> shift) & 0xFF) * a +
			static_cast<float>((bottom >> shift) & 0xFF) * (1.0f - a);
		return static_cast<std::uint32_t>(value + 0.5f) << shift;
	};
	return 0xFF000000u | mix(16) | mix(8) | mix(0);
}

inline COLORREF ToColorRef(std::uint32_t color)
{
	return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

inline bool SystemDarkTheme()
{
	DWORD light = 1;
	DWORD bytes = sizeof(light);
	::RegGetValueW(HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &bytes);
	return light == 0;
}

// 配置主题键："light"、"dark" 或 "system"（空表示跟随系统）。
inline bool DarkTheme(const std::string& theme)
{
	if (theme == "dark") return true;
	if (theme != "system" && !theme.empty()) return false;
	return SystemDarkTheme();
}

class ScopedFont
{
public:
	ScopedFont(const std::wstring& name, int pixels, int weight, DWORD quality = CLEARTYPE_QUALITY)
		: m_font(::CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			quality, DEFAULT_PITCH | FF_DONTCARE, name.c_str())) {}
	~ScopedFont() { if (m_font) ::DeleteObject(m_font); }
	ScopedFont(const ScopedFont&) = delete;
	ScopedFont& operator=(const ScopedFont&) = delete;
	HFONT get() const { return m_font; }

private:
	HFONT m_font;
};

// 预乘 BGRA 软件表面。
class Surface
{
public:
	// 可选的逻辑坐标到设备坐标缩放；既有候选绘制直接使用设备像素。
	Surface(std::uint32_t* pixels, int width, int height, float scale = 1.0f)
		: m_pixels(pixels), m_width(width), m_height(height), m_clip{ 0, 0, width, height },
		  m_scale(scale > 0.0f ? scale : 1.0f) {}

	void setClip(const RECT& clip) {
		m_clip = { static_cast<LONG>(std::floor(clip.left * m_scale)),
			static_cast<LONG>(std::floor(clip.top * m_scale)),
			static_cast<LONG>(std::ceil(clip.right * m_scale)),
			static_cast<LONG>(std::ceil(clip.bottom * m_scale)) };
	}
	void resetClip() { m_clip = { 0, 0, m_width, m_height }; }
	void fill(std::uint32_t color)
	{
		const float a = Alpha(color);
		const auto channel = [a](std::uint32_t value, int shift) {
			return static_cast<std::uint32_t>(((value >> shift) & 0xFF) * a + 0.5f) << shift;
		};
		const std::uint32_t pixel = (color & 0xFF000000u) |
			channel(color, 16) | channel(color, 8) | channel(color, 0);
		std::fill(m_pixels, m_pixels + static_cast<std::size_t>(m_width) * m_height, pixel);
	}

	template <typename Coverage>
	void paint(const RectF& bounds, std::uint32_t color, Coverage coverage)
	{
		if (Alpha(color) <= 0.0f) return;
		const int left = (std::max)(static_cast<int>(m_clip.left),
			static_cast<int>(std::floor(bounds.left * m_scale)));
		const int top = (std::max)(static_cast<int>(m_clip.top),
			static_cast<int>(std::floor(bounds.top * m_scale)));
		const int right = (std::min)(static_cast<int>(m_clip.right),
			static_cast<int>(std::ceil(bounds.right * m_scale)));
		const int bottom = (std::min)(static_cast<int>(m_clip.bottom),
			static_cast<int>(std::ceil(bounds.bottom * m_scale)));
		for (int y = top; y < bottom; ++y)
			for (int x = left; x < right; ++x)
			{
				const float value = coverage((static_cast<float>(x) + 0.5f) / m_scale,
					(static_cast<float>(y) + 0.5f) / m_scale);
				if (value > 0.0f) blend(m_pixels[y * m_width + x], color, value);
			}
	}

	// 与 paint() 类似，但使用逐像素直通色（渐变、图像）。
	template <typename Shade, typename Coverage>
	void shade(const RectF& bounds, Shade colorAt, Coverage coverage)
	{
		const int left = (std::max)(static_cast<int>(m_clip.left),
			static_cast<int>(std::floor(bounds.left * m_scale)));
		const int top = (std::max)(static_cast<int>(m_clip.top),
			static_cast<int>(std::floor(bounds.top * m_scale)));
		const int right = (std::min)(static_cast<int>(m_clip.right),
			static_cast<int>(std::ceil(bounds.right * m_scale)));
		const int bottom = (std::min)(static_cast<int>(m_clip.bottom),
			static_cast<int>(std::ceil(bounds.bottom * m_scale)));
		for (int y = top; y < bottom; ++y)
			for (int x = left; x < right; ++x)
			{
				const float value = coverage((static_cast<float>(x) + 0.5f) / m_scale,
					(static_cast<float>(y) + 0.5f) / m_scale);
				if (value > 0.0f) blend(m_pixels[y * m_width + x],
					colorAt(x / m_scale, y / m_scale), value);
			}
	}

private:
	static void blend(std::uint32_t& pixel, std::uint32_t color, float coverage)
	{
		const float a = Alpha(color) * (std::min)(coverage, 1.0f);
		const float keep = 1.0f - a;
		const auto channel = [&](int shift, float source) {
			const float value = source * a +
				static_cast<float>((pixel >> shift) & 0xFF) * keep;
			return static_cast<std::uint32_t>((std::min)(255.0f, value + 0.5f)) << shift;
		};
		pixel = channel(24, 255.0f) |
			channel(16, static_cast<float>((color >> 16) & 0xFF)) |
			channel(8, static_cast<float>((color >> 8) & 0xFF)) |
			channel(0, static_cast<float>(color & 0xFF));
	}

	std::uint32_t* m_pixels;
	int m_width;
	int m_height;
	RECT m_clip;
	float m_scale;
};

inline float RoundRectDistance(float x, float y, const RectF& rect, float radius)
{
	const float halfWidth = (rect.right - rect.left) * 0.5f;
	const float halfHeight = (rect.bottom - rect.top) * 0.5f;
	radius = (std::clamp)(radius, 0.0f, (std::min)(halfWidth, halfHeight));
	const float qx = std::fabs(x - rect.left - halfWidth) - (halfWidth - radius);
	const float qy = std::fabs(y - rect.top - halfHeight) - (halfHeight - radius);
	const float ox = (std::max)(qx, 0.0f);
	const float oy = (std::max)(qy, 0.0f);
	return std::sqrt(ox * ox + oy * oy) + (std::min)((std::max)(qx, qy), 0.0f) - radius;
}

inline float Coverage(float distance)
{
	return (std::clamp)(0.5f - distance, 0.0f, 1.0f);
}

inline void FillRoundRect(Surface& surface, const RectF& rect, float radius, std::uint32_t color)
{
	surface.paint(rect, color, [&](float x, float y) {
		return Coverage(RoundRectDistance(x, y, rect, radius));
	});
}

template <typename Shade>
inline void ShadeRoundRect(Surface& surface, const RectF& rect, float radius, Shade colorAt)
{
	surface.shade(rect, colorAt, [&](float x, float y) {
		return Coverage(RoundRectDistance(x, y, rect, radius));
	});
}

// 直通色插值；t 在 [0, 1] 区间内。
inline std::uint32_t Lerp(std::uint32_t from, std::uint32_t to, float t)
{
	t = (std::clamp)(t, 0.0f, 1.0f);
	const auto mix = [&](int shift) {
		const float a = static_cast<float>((from >> shift) & 0xFF);
		const float b = static_cast<float>((to >> shift) & 0xFF);
		return static_cast<std::uint32_t>(a + (b - a) * t + 0.5f) << shift;
	};
	return mix(24) | mix(16) | mix(8) | mix(0);
}

inline void StrokeRoundRect(Surface& surface, const RectF& rect, float radius, float width,
	std::uint32_t color)
{
	surface.paint(rect, color, [&](float x, float y) {
		const float distance = RoundRectDistance(x, y, rect, radius);
		return Coverage(distance) - Coverage(distance + width);
	});
}

inline void FillCircle(Surface& surface, float cx, float cy, float radius, std::uint32_t color)
{
	FillRoundRect(surface, { cx - radius, cy - radius, cx + radius, cy + radius }, radius, color);
}

inline void StrokeCircle(Surface& surface, float cx, float cy, float radius, float width,
	std::uint32_t color)
{
	StrokeRoundRect(surface, { cx - radius, cy - radius, cx + radius, cy + radius },
		radius, width, color);
}

inline float ShadowReach(const Shadow& shadow, float scale)
{
	return (shadow.blur + shadow.spread) * scale;
}

// 圆角矩形的高斯阴影：sigma = blur / 2，类似 CSS box-shadow。
inline void DrawShadow(Surface& surface, const RectF& panel, float radius,
	const Shadow& shadow, float scale)
{
	const float spread = shadow.spread * scale;
	const float offset = shadow.offsetY * scale;
	const RectF rect{ panel.left - spread, panel.top - spread + offset,
		panel.right + spread, panel.bottom + spread + offset };
	const float sigma = (std::max)(0.5f, shadow.blur * scale * 0.5f);
	const float reach = sigma * 2.0f;
	const float cornerRadius = (std::max)(0.0f, radius + spread);
	const float factor = 1.0f / (sigma * 1.41421356f);
	surface.paint({ rect.left - reach, rect.top - reach, rect.right + reach,
		rect.bottom + reach }, shadow.color, [&](float x, float y) {
		return 0.5f * std::erfc(RoundRectDistance(x, y, rect, cornerRadius) * factor);
	});
}

inline float SegmentDistance(float x, float y, float ax, float ay, float bx, float by)
{
	const float px = x - ax;
	const float py = y - ay;
	const float dx = bx - ax;
	const float dy = by - ay;
	const float length = dx * dx + dy * dy;
	const float t = length > 0.0f ?
		(std::clamp)((px * dx + py * dy) / length, 0.0f, 1.0f) : 0.0f;
	const float ex = px - dx * t;
	const float ey = py - dy * t;
	return std::sqrt(ex * ex + ey * ey);
}

// 圆头折线；点为 {x0, y0, x1, y1, ...}。
inline void StrokePolyline(Surface& surface, const float* points, int count, float stroke,
	std::uint32_t color)
{
	if (count < 2) return;
	RectF bounds{ points[0], points[1], points[0], points[1] };
	for (int i = 1; i < count; ++i)
	{
		bounds.left = (std::min)(bounds.left, points[i * 2]);
		bounds.top = (std::min)(bounds.top, points[i * 2 + 1]);
		bounds.right = (std::max)(bounds.right, points[i * 2]);
		bounds.bottom = (std::max)(bounds.bottom, points[i * 2 + 1]);
	}
	const float half = stroke * 0.5f;
	bounds = { bounds.left - half - 1.0f, bounds.top - half - 1.0f,
		bounds.right + half + 1.0f, bounds.bottom + half + 1.0f };
	surface.paint(bounds, color, [&](float x, float y) {
		float distance = 1e9f;
		for (int i = 0; i + 1 < count; ++i)
			distance = (std::min)(distance, SegmentDistance(x, y, points[i * 2],
				points[i * 2 + 1], points[i * 2 + 2], points[i * 2 + 3]));
		return (std::clamp)(half + 0.5f - distance, 0.0f, 1.0f);
	});
}

// 16 单位的 "M4 6.4 L8 10.4 L12 6.4" 折线箭头，在其自身边界内居中。
inline void DrawChevron(Surface& surface, float cx, float cy, float size, float stroke,
	bool up, std::uint32_t color)
{
	const float unit = size / 16.0f;
	const float wing = 4.0f * unit;
	const float rise = (up ? -2.0f : 2.0f) * unit;
	const float points[] = { cx - wing, cy - rise, cx, cy + rise, cx + wing, cy - rise };
	StrokePolyline(surface, points, 3, stroke, color);
}

// 把 UI 图标用到的 SVG 路径数据子集（M L H V C S A Z，
// 绝对与相对命令）展平为线段 {x0, y0, x1, y1, ...}。
inline std::vector<float> FlattenSvgPath(const char* d)
{
	std::vector<float> segments;
	float x = 0.0f, y = 0.0f, startX = 0.0f, startY = 0.0f;
	float controlX = 0.0f, controlY = 0.0f;
	char command = 0;
	char previous = 0;
	const auto skip = [&d]() { while (*d == ' ' || *d == ',' || *d == '\n' || *d == '\t') ++d; };
	const auto number = [&d, &skip]() {
		skip();
		char* end = nullptr;
		const float value = std::strtof(d, &end);
		d = end;
		return value;
	};
	const auto flag = [&d, &skip]() { skip(); return *d++ == '1'; };
	const auto line = [&segments, &x, &y](float toX, float toY) {
		segments.insert(segments.end(), { x, y, toX, toY });
		x = toX;
		y = toY;
	};
	const auto cubic = [&](float x1, float y1, float x2, float y2, float x3, float y3) {
		const float x0 = x, y0 = y;
		constexpr int kSteps = 10;
		for (int i = 1; i <= kSteps; ++i)
		{
			const float t = static_cast<float>(i) / kSteps;
			const float u = 1.0f - t;
			line(u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3,
				u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3);
		}
		controlX = x2;
		controlY = y2;
	};
	const auto arc = [&](float rx, float ry, float rotation, bool large, bool sweep,
		float toX, float toY) {
		constexpr float kPi = 3.14159265f;
		rx = std::fabs(rx);
		ry = std::fabs(ry);
		if (rx <= 0.0f || ry <= 0.0f) { line(toX, toY); return; }
		const float phi = rotation * kPi / 180.0f;
		const float cosPhi = std::cos(phi), sinPhi = std::sin(phi);
		const float dx = (x - toX) * 0.5f, dy = (y - toY) * 0.5f;
		const float px = cosPhi * dx + sinPhi * dy;
		const float py = -sinPhi * dx + cosPhi * dy;
		const float lambda = px * px / (rx * rx) + py * py / (ry * ry);
		if (lambda > 1.0f) { rx *= std::sqrt(lambda); ry *= std::sqrt(lambda); }
		const float numerator = rx * rx * ry * ry - rx * rx * py * py - ry * ry * px * px;
		const float denominator = rx * rx * py * py + ry * ry * px * px;
		float coef = denominator > 0.0f ?
			std::sqrt((std::max)(0.0f, numerator / denominator)) : 0.0f;
		if (large == sweep) coef = -coef;
		const float cxp = coef * rx * py / ry;
		const float cyp = -coef * ry * px / rx;
		const float cx = cosPhi * cxp - sinPhi * cyp + (x + toX) * 0.5f;
		const float cy = sinPhi * cxp + cosPhi * cyp + (y + toY) * 0.5f;
		const float start = std::atan2((py - cyp) / ry, (px - cxp) / rx);
		float delta = std::atan2((-py - cyp) / ry, (-px - cxp) / rx) - start;
		if (sweep && delta < 0.0f) delta += 2.0f * kPi;
		if (!sweep && delta > 0.0f) delta -= 2.0f * kPi;
		const int steps = (std::max)(4, static_cast<int>(std::ceil(std::fabs(delta) * 6.0f)));
		for (int i = 1; i <= steps; ++i)
		{
			const float angle = start + delta * static_cast<float>(i) / steps;
			const float ex = rx * std::cos(angle), ey = ry * std::sin(angle);
			line(cosPhi * ex - sinPhi * ey + cx, sinPhi * ex + cosPhi * ey + cy);
		}
		x = toX;
		y = toY;
	};
	for (;;)
	{
		skip();
		if (!*d) break;
		if (std::isalpha(static_cast<unsigned char>(*d))) command = *d++;
		else if (command == 'M') command = 'L';
		else if (command == 'm') command = 'l';
		const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
		const float ox = relative ? x : 0.0f;
		const float oy = relative ? y : 0.0f;
		const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
		switch (upper)
		{
		case 'M':
		{
			x = ox + number();
			y = oy + number();
			startX = x;
			startY = y;
			break;
		}
		case 'L': { const float tx = ox + number(); line(tx, oy + number()); break; }
		case 'H': line(ox + number(), y); break;
		case 'V': line(x, oy + number()); break;
		case 'C':
		{
			const float x1 = ox + number(), y1 = oy + number();
			const float x2 = ox + number(), y2 = oy + number();
			const float x3 = ox + number(), y3 = oy + number();
			cubic(x1, y1, x2, y2, x3, y3);
			break;
		}
		case 'S':
		{
			const bool smooth = previous == 'C' || previous == 'S';
			const float x1 = smooth ? 2 * x - controlX : x;
			const float y1 = smooth ? 2 * y - controlY : y;
			const float x2 = ox + number(), y2 = oy + number();
			const float x3 = ox + number(), y3 = oy + number();
			cubic(x1, y1, x2, y2, x3, y3);
			break;
		}
		case 'A':
		{
			const float rx = number(), ry = number(), rotation = number();
			const bool large = flag();
			const bool sweep = flag();
			const float tx = ox + number();
			arc(rx, ry, rotation, large, sweep, tx, oy + number());
			break;
		}
		case 'Z': line(startX, startY); break;
		default: return segments;
		}
		previous = upper;
	}
	return segments;
}

// 以圆头和圆角连接描边以方形 viewBox 创作的 SVG 图标路径，
// 作为一次覆盖遍历绘制，重叠子路径不会重复混色。
inline void StrokeSvgIcon(Surface& surface, const char* const* paths, int count,
	float left, float top, float size, float viewBox, float stroke, std::uint32_t color)
{
	std::vector<float> segments;
	const float unit = size / viewBox;
	for (int i = 0; i < count; ++i)
	{
		const std::vector<float> path = FlattenSvgPath(paths[i]);
		for (std::size_t j = 0; j + 1 < path.size(); j += 2)
		{
			segments.push_back(left + path[j] * unit);
			segments.push_back(top + path[j + 1] * unit);
		}
	}
	const float half = stroke * 0.5f;
	surface.paint({ left - stroke, top - stroke, left + size + stroke, top + size + stroke },
		color, [&](float px, float py) {
		float distance = 1e9f;
		for (std::size_t i = 0; i + 3 < segments.size(); i += 4)
			distance = (std::min)(distance, SegmentDistance(px, py, segments[i],
				segments[i + 1], segments[i + 2], segments[i + 3]));
		return (std::clamp)(half + 0.5f - distance, 0.0f, 1.0f);
	});
}

// 选入内存 DC 的自顶向下 32bpp DIB。通过 surface() 绘制图形，
// GDI 文字须包裹在 beginText()/endText() 中以保住 alpha 通道。
class Canvas
{
public:
	Canvas(int width, int height)
		: m_width((std::max)(1, width)), m_height((std::max)(1, height))
	{
		BITMAPINFO info{};
		info.bmiHeader.biSize = sizeof(info.bmiHeader);
		info.bmiHeader.biWidth = m_width;
		info.bmiHeader.biHeight = -m_height;
		info.bmiHeader.biPlanes = 1;
		info.bmiHeader.biBitCount = 32;
		info.bmiHeader.biCompression = BI_RGB;
		HDC screen = ::GetDC(nullptr);
		m_dc = ::CreateCompatibleDC(screen);
		void* bits = nullptr;
		m_bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
		::ReleaseDC(nullptr, screen);
		m_pixels = static_cast<std::uint32_t*>(bits);
		if (m_dc && m_bitmap) m_oldBitmap = ::SelectObject(m_dc, m_bitmap);
		if (m_pixels) std::memset(m_pixels, 0, count() * sizeof(std::uint32_t));
	}
	~Canvas()
	{
		if (m_oldBitmap) ::SelectObject(m_dc, m_oldBitmap);
		if (m_bitmap) ::DeleteObject(m_bitmap);
		if (m_dc) ::DeleteDC(m_dc);
	}
	Canvas(const Canvas&) = delete;
	Canvas& operator=(const Canvas&) = delete;

	bool valid() const { return m_pixels != nullptr && m_oldBitmap != nullptr; }
	HDC dc() const { return m_dc; }
	int width() const { return m_width; }
	int height() const { return m_height; }
	std::uint32_t* pixels() const { return m_pixels; }
	std::size_t count() const { return static_cast<std::size_t>(m_width) * m_height; }
	Surface surface() const { return Surface(m_pixels, m_width, m_height); }

	void beginText()
	{
		::GdiFlush();
		m_alpha.resize(count());
		for (std::size_t i = 0; i < m_alpha.size(); ++i)
			m_alpha[i] = static_cast<std::uint8_t>(m_pixels[i] >> 24);
		::SetBkMode(m_dc, TRANSPARENT);
	}
	void endText()
	{
		::SelectClipRgn(m_dc, nullptr);
		::GdiFlush();
		for (std::size_t i = 0; i < m_alpha.size(); ++i)
			m_pixels[i] = (m_pixels[i] & 0x00FFFFFFu) | (static_cast<std::uint32_t>(m_alpha[i]) << 24);
	}

	// 分层窗口：逐像素 alpha，可选移动。
	void present(HWND window, const POINT* position) const
	{
		SIZE size{ m_width, m_height };
		POINT source{ 0, 0 };
		POINT destination = position ? *position : POINT{};
		BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		HDC screen = ::GetDC(nullptr);
		::UpdateLayeredWindow(window, screen, position ? &destination : nullptr, &size, m_dc,
			&source, 0, &blend, ULW_ALPHA);
		::ReleaseDC(nullptr, screen);
	}

	// 不透明窗口：画布必须用不透明颜色完整绘制。
	void blit(HDC target, int x, int y) const
	{
		::BitBlt(target, x, y, m_width, m_height, m_dc, 0, 0, SRCCOPY);
	}

private:
	int m_width;
	int m_height;
	HDC m_dc = nullptr;
	HBITMAP m_bitmap = nullptr;
	HGDIOBJ m_oldBitmap = nullptr;
	std::uint32_t* m_pixels = nullptr;
	std::vector<std::uint8_t> m_alpha;
};
} // 命名空间 coral::win32::soft 结束

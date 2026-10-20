#include "pch.h"
#include "skincropdialog.h"
#include "settings_dpi.h"
#include "soft_canvas.h"
#include "candidate_view.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

using namespace coral::win32;
using namespace coral::win32::soft;

namespace
{
	constexpr wchar_t kDialogClass[] = L"CoralPinyinSkinCropDialog";
	constexpr int kWidth = 640;
	constexpr int kHeight = 570;
	constexpr int kPadding = 24;
	constexpr int kTitleHeight = 60;
	constexpr RECT kStage{ kPadding, kTitleHeight, kWidth - kPadding, 412 };
	constexpr RECT kClose{ kWidth - kPadding - 28, 16, kWidth - kPadding, 44 };
	constexpr RECT kConfirm{ kWidth - kPadding - 88, kHeight - 24 - 34, kWidth - kPadding, kHeight - 24 };
	constexpr RECT kCancel{ kConfirm.left - 10 - 88, kConfirm.top, kConfirm.left - 10, kConfirm.bottom };
	constexpr RECT kOpacityTrack{ 160, 452, kWidth - kPadding - 64, 484 };
	constexpr float kHandle = 8.0f;
	constexpr int kStageInset = 16;

	enum Part { None, Move, TopLeft, TopRight, BottomLeft, BottomRight, Close, Cancel, Confirm, ImageOpacity };

	struct Dialog
	{
		const SkinImage* image = nullptr;
		SettingsSkin skin;
		HFONT title = nullptr;
		HFONT text = nullptr;
		HFONT caption = nullptr;
		// 图像适配到舞台内。
		std::vector<std::uint32_t> display;
		RECT imageRect{};
		double scale = 1.0;           // 每个解码图像像素对应的显示像素数
		// 裁剪框以解码图像像素计；高度 = 宽度 * 宽高比。
		double cropX = 0, cropY = 0, cropWidth = 0;
		double aspect = 1.0;
		double minWidth = 1.0;
		double collapsedRatio = 0.25; // 折叠时框内可见部分的比例
		int outWidth = 0;
		int outHeight = 0;
		int imageOpacity = 100;
		std::uint32_t background = 0xFFFFFFFF;
		std::uint32_t previewVeil = 0;
		bool opacityFocus = false;
		Part drag = None;
		POINT grab{};
		double grabX = 0, grabY = 0, grabWidth = 0;
		Part hover = None;
		Part pressed = None;
		bool done = false;
		bool accepted = false;
	};

	double CropHeight(const Dialog& d) { return d.cropWidth * d.aspect; }

	RectF CropRect(const Dialog& d)
	{
		const float left = static_cast<float>(d.imageRect.left + d.cropX * d.scale);
		const float top = static_cast<float>(d.imageRect.top + d.cropY * d.scale);
		return { left, top, left + static_cast<float>(d.cropWidth * d.scale),
			top + static_cast<float>(CropHeight(d) * d.scale) };
	}

	bool Inside(const RECT& rect, POINT point) { return PtInRect(&rect, point) != FALSE; }

	Part HitTest(const Dialog& d, POINT point)
	{
		if (Inside(kClose, point)) return Close;
		if (Inside(kCancel, point)) return Cancel;
		if (Inside(kConfirm, point)) return Confirm;
		const RECT opacityHit{ kOpacityTrack.left - 10, kOpacityTrack.top,
			kOpacityTrack.right + 10, kOpacityTrack.bottom };
		if (Inside(opacityHit, point)) return ImageOpacity;
		const RectF crop = CropRect(d);
		const auto onCorner = [&](float x, float y) {
			return std::fabs(point.x - x) <= kHandle && std::fabs(point.y - y) <= kHandle;
		};
		if (onCorner(crop.left, crop.top)) return TopLeft;
		if (onCorner(crop.right, crop.top)) return TopRight;
		if (onCorner(crop.left, crop.bottom)) return BottomLeft;
		if (onCorner(crop.right, crop.bottom)) return BottomRight;
		if (point.x >= crop.left && point.x < crop.right && point.y >= crop.top && point.y < crop.bottom)
			return Move;
		return None;
	}

	void DragTo(Dialog& d, POINT point)
	{
		if (d.drag == ImageOpacity) {
			const int transparency = (std::clamp)(static_cast<int>(std::lround(
				100.0 * (point.x - kOpacityTrack.left) / (kOpacityTrack.right - kOpacityTrack.left))), 0, 100);
			d.imageOpacity = 100 - transparency;
			return;
		}
		const double width = d.image->width;
		const double height = d.image->height;
		if (d.drag == Move)
		{
			d.cropX = (std::clamp)(d.grabX + (point.x - d.grab.x) / d.scale, 0.0, width - d.cropWidth);
			d.cropY = (std::clamp)(d.grabY + (point.y - d.grab.y) / d.scale, 0.0, height - CropHeight(d));
			return;
		}
		// 被拖动角的对角保持不动；宽高比始终不变。
		const bool left = d.drag == TopLeft || d.drag == BottomLeft;
		const bool top = d.drag == TopLeft || d.drag == TopRight;
		const double anchorX = left ? d.grabX + d.grabWidth : d.grabX;
		const double anchorY = top ? d.grabY + d.grabWidth * d.aspect : d.grabY;
		const double pointerX = (point.x - d.imageRect.left) / d.scale;
		const double pointerY = (point.y - d.imageRect.top) / d.scale;
		double cropWidth = (std::max)(std::fabs(pointerX - anchorX), std::fabs(pointerY - anchorY) / d.aspect);
		const double maxWidth = (std::min)(left ? anchorX : width - anchorX,
			(top ? anchorY : height - anchorY) / d.aspect);
		cropWidth = (std::clamp)(cropWidth, (std::min)(d.minWidth, maxWidth), maxWidth);
		d.cropWidth = cropWidth;
		d.cropX = left ? anchorX - cropWidth : anchorX;
		d.cropY = top ? anchorY - cropWidth * d.aspect : anchorY;
	}

	void PaintLabel(Canvas& canvas, HFONT font, std::uint32_t color, const std::wstring& text,
		RECT rect, UINT format)
	{
		HGDIOBJ old = SelectObject(canvas.dc(), font);
		SetTextColor(canvas.dc(), ToColorRef(color));
		::DrawTextW(canvas.dc(), text.c_str(), static_cast<int>(text.size()), &rect,
			format | DT_NOPREFIX | DT_SINGLELINE);
		SelectObject(canvas.dc(), old);
	}

	int TextWidth(HDC dc, HFONT font, const wchar_t* text)
	{
		HGDIOBJ old = SelectObject(dc, font);
		SIZE size{};
		GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
		SelectObject(dc, old);
		return size.cx;
	}

	void Paint(const Dialog& d, HDC target)
	{
		const SettingsSkin& s = d.skin;
		const bool dark = s.darkCaption;
		Canvas canvas(kWidth, kHeight);
		if (!canvas.valid()) return;
		Surface surface = canvas.surface();
		surface.fill(s.surface);

		// 舞台及适配其中的图像；框外区域被遮罩。
		const RectF stage = ToRectF(kStage);
		FillRoundRect(surface, stage, 10.0f, dark ? 0xFF35373D : 0xFFC4C7CC);
		const RectF crop = CropRect(d);
		const std::uint32_t veil = dark ? 0x99000000 : 0x8CE6E8EB;
		const int imageWidth = d.imageRect.right - d.imageRect.left;
		surface.shade(ToRectF(d.imageRect), [&](int x, int y) {
			const int column = (std::clamp)(x - static_cast<int>(d.imageRect.left), 0, imageWidth - 1);
			const int row = (std::max)(0, y - static_cast<int>(d.imageRect.top));
			const std::uint32_t pixel = d.display[static_cast<std::size_t>(row) * imageWidth + column];
			const bool inside = x + 0.5f >= crop.left && x + 0.5f <= crop.right &&
				y + 0.5f >= crop.top && y + 0.5f <= crop.bottom;
			return inside ? Over(d.previewVeil, BlendCandidateImage(pixel, d.background, d.imageOpacity))
				: Over(veil, pixel);
		}, [](float, float) { return 1.0f; });

		// 折叠/展开分隔线。
		const float divider = std::round(crop.top + (crop.bottom - crop.top) * static_cast<float>(d.collapsedRatio));
		for (float x = crop.left + 4.0f; x < crop.right - 4.0f; x += 10.0f)
		{
			const float end = (std::min)(x + 6.0f, crop.right - 4.0f);
			FillRoundRect(surface, { x, divider, end, divider + 1.0f }, 0.0f, 0x59000000);
			FillRoundRect(surface, { x, divider - 1.0f, end, divider }, 0.0f, 0xF2FFFFFF);
		}

		StrokeRoundRect(surface, crop, 0.0f, 1.5f, s.accent);
		for (const float x : { crop.left, crop.right })
			for (const float y : { crop.top, crop.bottom })
			{
				const RectF handle{ x - 4.5f, y - 4.5f, x + 4.5f, y + 4.5f };
				FillRoundRect(surface, handle, 1.5f, 0xFFFFFFFF);
				StrokeRoundRect(surface, handle, 1.5f, 1.5f, s.accent);
			}

		// 分隔线两侧的标签，在能放入框内时显示。
		const int labelHeight = 20;
		const wchar_t* const upper = L"收起时显示";
		const wchar_t* const lower = L"展开后延伸";
		const int upperWidth = TextWidth(canvas.dc(), d.caption, upper) + 14;
		const int lowerWidth = TextWidth(canvas.dc(), d.caption, lower) + 14;
		const int labelLeft = static_cast<int>(crop.left) + 8;
		const bool showUpper = divider - crop.top >= labelHeight + 8 && crop.right - labelLeft >= upperWidth + 8;
		const bool showLower = crop.bottom - divider >= labelHeight + 8 && crop.right - labelLeft >= lowerWidth + 8;
		const RECT upperRect{ labelLeft, static_cast<int>(divider) - 4 - labelHeight,
			labelLeft + upperWidth, static_cast<int>(divider) - 4 };
		const RECT lowerRect{ labelLeft, static_cast<int>(divider) + 5,
			labelLeft + lowerWidth, static_cast<int>(divider) + 5 + labelHeight };
		if (showUpper) FillRoundRect(surface, ToRectF(upperRect), 4.0f, 0x99000000);
		if (showLower) FillRoundRect(surface, ToRectF(lowerRect), 4.0f, 0x99000000);

		// 按钮与关闭图标。
		const auto button = [&](const RECT& rect, Part part, bool primary) {
			const bool hot = d.hover == part;
			const bool down = hot && d.pressed == part;
			const RectF box = ToRectF(rect);
			if (primary)
				FillRoundRect(surface, box, static_cast<float>(s.controlRadius),
					down ? s.accentPressed : hot ? s.accentHover : s.accent);
			else
			{
				FillRoundRect(surface, box, static_cast<float>(s.controlRadius),
					down ? s.pressed : hot ? s.hover : s.surface);
				StrokeRoundRect(surface, { box.left + 0.5f, box.top + 0.5f, box.right - 0.5f, box.bottom - 0.5f },
					static_cast<float>(s.controlRadius), 1.0f, hot ? s.lineHover : s.lineStrong);
			}
		};
		button(kCancel, Cancel, false);
		button(kConfirm, Confirm, true);
		if (d.hover == Close)
			FillRoundRect(surface, ToRectF(kClose), 6.0f, d.pressed == Close ? s.pressed : s.hover);
		const float cx = (kClose.left + kClose.right) / 2.0f;
		const float cy = (kClose.top + kClose.bottom) / 2.0f;
		const float first[] = { cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f };
		const float second[] = { cx + 5.0f, cy - 5.0f, cx - 5.0f, cy + 5.0f };
		StrokePolyline(surface, first, 2, 1.4f, s.sub);
		StrokePolyline(surface, second, 2, 1.4f, s.sub);

		// 只有图片淡入候选面板色；文字保持不透明。
		const float trackY = (kOpacityTrack.top + kOpacityTrack.bottom) / 2.0f;
		const float knobX = kOpacityTrack.left + (kOpacityTrack.right - kOpacityTrack.left) *
			(100 - d.imageOpacity) / 100.0f;
		FillRoundRect(surface, { static_cast<float>(kOpacityTrack.left), trackY - 2,
			static_cast<float>(kOpacityTrack.right), trackY + 2 }, 2, s.sliderTrack);
		FillRoundRect(surface, { static_cast<float>(kOpacityTrack.left), trackY - 2, knobX, trackY + 2 }, 2, s.accent);
		FillRoundRect(surface, { knobX - 8, trackY - 8, knobX + 8, trackY + 8 }, 8, s.surface);
		StrokeRoundRect(surface, { knobX - 8, trackY - 8, knobX + 8, trackY + 8 }, 8,
			d.opacityFocus ? 2.5f : 1.5f, s.accent);
		canvas.beginText();
		wchar_t transparency[16];
		swprintf_s(transparency, L"%d%%", 100 - d.imageOpacity);
		PaintLabel(canvas, d.text, s.text, L"图片透明度", { kPadding, 452, 152, 484 }, DT_LEFT | DT_VCENTER);
		PaintLabel(canvas, d.text, s.text, transparency, { kOpacityTrack.right + 12, 452, kWidth - kPadding, 484 }, DT_RIGHT | DT_VCENTER);
		PaintLabel(canvas, d.caption, s.muted, L"原图", { kOpacityTrack.left, 484, kOpacityTrack.left + 80, 504 }, DT_LEFT | DT_VCENTER);
		PaintLabel(canvas, d.caption, s.muted, L"完全透明", { kOpacityTrack.right - 100, 484, kOpacityTrack.right, 504 }, DT_RIGHT | DT_VCENTER);
		PaintLabel(canvas, d.title, s.text, L"裁剪图片", { kPadding, 0, kWidth - 80, kTitleHeight }, DT_LEFT | DT_VCENTER);
		if (showUpper) PaintLabel(canvas, d.caption, 0xFFFFFFFF, upper, upperRect, DT_CENTER | DT_VCENTER);
		if (showLower) PaintLabel(canvas, d.caption, 0xFFFFFFFF, lower, lowerRect, DT_CENTER | DT_VCENTER);
		PaintLabel(canvas, d.caption, s.sub,
			L"拖动或缩放裁剪框调整区域。虚线以上是候选窗收起时显示的部分，以下是展开后的延伸区域。",
			{ kPadding, kStage.bottom + 8, kWidth - kPadding, kStage.bottom + 28 }, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);
		wchar_t size[96];
		swprintf_s(size, L"将按 %d × %d 像素保存", d.outWidth, d.outHeight);
		PaintLabel(canvas, d.caption, s.muted, size,
			{ kPadding, kCancel.top, kCancel.left - 12, kCancel.bottom }, DT_LEFT | DT_VCENTER);
		PaintLabel(canvas, d.text, s.text, L"取消", kCancel, DT_CENTER | DT_VCENTER);
		PaintLabel(canvas, d.text, s.onAccent, L"确定", kConfirm, DT_CENTER | DT_VCENTER);
		canvas.endText();
		canvas.blit(target, 0, 0);
	}

	void UpdateOutputSize(Dialog& d, const SIZE& frame)
	{
		// 框体的原生分辨率，不小于窗口尺寸且至多为其两倍。
		const double native = d.cropWidth * d.image->sourceWidth / (std::max)(1, d.image->width);
		d.outWidth = static_cast<int>(std::lround((std::clamp)(native, static_cast<double>(frame.cx), frame.cx * 2.0)));
		d.outHeight = static_cast<int>(std::lround(d.outWidth * d.aspect));
	}

	SIZE FrameSize()
	{
		const CandidateSkin defaults{};
		return defaults.imageSize();
	}

	LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		auto* d = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		switch (msg)
		{
		case WM_NCCREATE:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
			break;
		case WM_NCHITTEST:
		{
			POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
			ScreenToClient(hwnd, &point);
			if (point.y < kTitleHeight && !Inside(kClose, point)) return HTCAPTION;
			return HTCLIENT;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC dc = BeginPaint(hwnd, &ps);
			if (d) Paint(*d, dc);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_PRINTCLIENT:
			if (d) Paint(*d, reinterpret_cast<HDC>(wParam));
			return 0;
		case WM_SETCURSOR:
			if (d && LOWORD(lParam) == HTCLIENT)
			{
				POINT point;
				GetCursorPos(&point);
				ScreenToClient(hwnd, &point);
				const Part part = d->drag != None ? d->drag : HitTest(*d, point);
				LPCWSTR cursor = IDC_ARROW;
				if (part == Move) cursor = IDC_SIZEALL;
				else if (part == TopLeft || part == BottomRight) cursor = IDC_SIZENWSE;
				else if (part == TopRight || part == BottomLeft) cursor = IDC_SIZENESW;
				else if (part == Close || part == Cancel || part == Confirm || part == ImageOpacity) cursor = IDC_HAND;
				SetCursor(LoadCursorW(nullptr, cursor));
				return TRUE;
			}
			break;
		case WM_MOUSEMOVE:
			if (d)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				if (d->drag != None)
				{
					DragTo(*d, point);
					if (d->drag != ImageOpacity) UpdateOutputSize(*d, FrameSize());
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
				const Part part = HitTest(*d, point);
				if (part != d->hover)
				{
					d->hover = part;
					InvalidateRect(hwnd, nullptr, FALSE);
				}
				TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&track);
			}
			return 0;
		case WM_MOUSELEAVE:
			if (d && d->hover != None && d->drag == None)
			{
				d->hover = None;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		case WM_LBUTTONDOWN:
			if (d)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const Part part = HitTest(*d, point);
				d->opacityFocus = part == ImageOpacity;
				SetFocus(hwnd);
				if (part == Close || part == Cancel || part == Confirm)
					d->pressed = part;
				else if (part != None)
				{
					d->drag = part;
					d->grab = point;
					d->grabX = d->cropX;
					d->grabY = d->cropY;
					d->grabWidth = d->cropWidth;
					if (part == ImageOpacity) DragTo(*d, point);
				}
				SetCapture(hwnd);
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		case WM_LBUTTONUP:
			if (d)
			{
				const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				const Part pressed = d->pressed;
				d->pressed = None;
				d->drag = None;
				if (GetCapture() == hwnd) ReleaseCapture();
				if (pressed != None && HitTest(*d, point) == pressed)
				{
					d->accepted = pressed == Confirm;
					d->done = true;
				}
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		case WM_CAPTURECHANGED:
			if (d) { d->drag = None; d->pressed = None; }
			return 0;
		case WM_KEYDOWN:
			if (d && wParam == VK_TAB) {
				d->opacityFocus = !d->opacityFocus;
				InvalidateRect(hwnd, nullptr, FALSE);
				return 0;
			}
			if (d && d->opacityFocus) {
				int value = d->imageOpacity;
				if (wParam == VK_LEFT || wParam == VK_UP) ++value;
				else if (wParam == VK_RIGHT || wParam == VK_DOWN) --value;
				else if (wParam == VK_HOME) value = 100;
				else if (wParam == VK_END) value = 0;
				else value = -1;
				if (value >= 0 || wParam == VK_RIGHT || wParam == VK_DOWN) {
					d->imageOpacity = (std::clamp)(value, 0, 100);
					InvalidateRect(hwnd, nullptr, FALSE);
					return 0;
				}
			}
			if (d && (wParam == VK_ESCAPE || wParam == VK_RETURN))
			{
				d->accepted = wParam == VK_RETURN;
				d->done = true;
				return 0;
			}
			break;
		case WM_CLOSE:
			if (d) d->done = true;
			return 0;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	void RegisterDialogClass()
	{
		static bool registered = false;
		if (registered) return;
		WNDCLASSEXW wc{ sizeof(wc) };
		wc.style = CS_DROPSHADOW;
		wc.lpfnWndProc = DialogProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		wc.lpszClassName = kDialogClass;
		registered = RegisterClassExW(&wc) != 0;
	}
}

std::shared_ptr<SkinImage> SkinCropDialog::run(HWND owner, const SkinImage& image, const SettingsSkin& skin,
	int& imageOpacity, std::uint32_t background, std::uint32_t veil)
{
	// 在混合 DPI 模式下保持此固定像素编辑器的原始尺寸。
	coral::settings_ui::DpiContext context(DPI_AWARENESS_CONTEXT_UNAWARE);
	if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) return nullptr;
	RegisterDialogClass();

	const CandidateSkin defaults{};
	const SIZE frame = defaults.imageSize();
	Dialog d;
	d.image = &image;
	d.skin = skin;
	d.imageOpacity = (std::clamp)(imageOpacity, 0, 100);
	d.background = background;
	d.previewVeil = veil;
	d.aspect = static_cast<double>(frame.cy) / frame.cx;
	d.collapsedRatio = static_cast<double>(defaults.collapsedImageHeight()) / frame.cy;
	// 裁剪框按源像素度量时不得小于窗口尺寸。
	d.minWidth = (std::min)(static_cast<double>(image.width),
		frame.cx * static_cast<double>(image.width) / (std::max)(1, image.sourceWidth));

	const int stageWidth = kStage.right - kStage.left - kStageInset * 2;
	const int stageHeight = kStage.bottom - kStage.top - kStageInset * 2;
	d.scale = (std::min)(static_cast<double>(stageWidth) / image.width,
		static_cast<double>(stageHeight) / image.height);
	const int displayWidth = (std::max)(1, static_cast<int>(std::lround(image.width * d.scale)));
	const int displayHeight = (std::max)(1, static_cast<int>(std::lround(image.height * d.scale)));
	d.scale = static_cast<double>(displayWidth) / image.width;
	d.display = CoverSkinImage(image, displayWidth, displayHeight);
	d.imageRect.left = kStage.left + (kStage.right - kStage.left - displayWidth) / 2;
	d.imageRect.top = kStage.top + (kStage.bottom - kStage.top - displayHeight) / 2;
	d.imageRect.right = d.imageRect.left + displayWidth;
	d.imageRect.bottom = d.imageRect.top + displayHeight;

	d.cropWidth = (std::min)(static_cast<double>(image.width), image.height / d.aspect);
	d.cropX = (image.width - d.cropWidth) / 2.0;
	d.cropY = (image.height - CropHeight(d)) / 2.0;
	UpdateOutputSize(d, frame);

	d.title = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, skin.fontName.c_str());
	d.text = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, skin.fontName.c_str());
	d.caption = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, skin.fontName.c_str());

	RECT anchor{};
	if (!owner || !GetWindowRect(owner, &anchor))
		SystemParametersInfoW(SPI_GETWORKAREA, 0, &anchor, 0);
	const int x = anchor.left + (anchor.right - anchor.left - kWidth) / 2;
	const int y = anchor.top + (anchor.bottom - anchor.top - kHeight) / 2;
	HWND hwnd = CreateWindowExW(0, kDialogClass, L"裁剪图片", WS_POPUP | WS_CLIPCHILDREN,
		x, y, kWidth, kHeight, owner, nullptr, GetModuleHandleW(nullptr), &d);
	if (!hwnd)
	{
		DeleteObject(d.title);
		DeleteObject(d.text);
		DeleteObject(d.caption);
		return nullptr;
	}
	const int corner = 2; // DWMWCP_ROUND（圆角）
	DwmSetWindowAttribute(hwnd, 33 /*  DWMWA_WINDOW_CORNER_PREFERENCE（窗口圆角偏好） */, &corner, sizeof(corner));

	if (owner) EnableWindow(owner, FALSE);
	ShowWindow(hwnd, SW_SHOW);
	SetForegroundWindow(hwnd);
	SetFocus(hwnd);
	MSG msg{};
	bool quit = false;
	while (!d.done)
	{
		if (GetMessageW(&msg, nullptr, 0, 0) <= 0)
		{
			quit = true;
			break;
		}
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
	if (owner)
	{
		EnableWindow(owner, TRUE);
		SetActiveWindow(owner);
	}
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
	DestroyWindow(hwnd);
	DeleteObject(d.title);
	DeleteObject(d.text);
	DeleteObject(d.caption);
	if (quit) PostQuitMessage(static_cast<int>(msg.wParam));
	if (!d.accepted) return nullptr;

	const int cropX = static_cast<int>(std::lround(d.cropX));
	const int cropY = static_cast<int>(std::lround(d.cropY));
	const int cropWidth = static_cast<int>(std::lround(d.cropWidth));
	const int cropHeight = static_cast<int>(std::lround(CropHeight(d)));
	const auto cropped = CropSkinImage(image, cropX, cropY, cropWidth, cropHeight, d.outWidth, d.outHeight);
	if (cropped) imageOpacity = d.imageOpacity;
	return cropped;
}

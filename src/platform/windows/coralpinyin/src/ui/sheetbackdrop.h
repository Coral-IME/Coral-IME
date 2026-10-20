#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SHEETBACKDROP_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SHEETBACKDROP_H_INCLUDED

#include "settingswindow.h"
#include "soft_canvas.h"

#include <memory>

// 设置窗口内嵌面板（模糊音、词库导入）共用：把父窗口当前画面截成底图，面板盖在其上。
namespace sheet
{
// WM_PRINT 会递归累加窗口/非客户区偏移。显式渲染每个
// 客户区，使视口/页面/控件坐标与裁剪保持局部。
inline void PaintClientTree(HWND hwnd, HDC dc, HWND excluded = nullptr)
{
	const int saved = SaveDC(dc);
	if (!saved) return;
	RECT client{}; GetClientRect(hwnd, &client);
	IntersectClipRect(dc, 0, 0, client.right, client.bottom);
	const int paintState = SaveDC(dc);
	if (paintState) {
		SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
		RestoreDC(dc, paintState); // 控件的绘制处理可能修改 DC。
	}
	// 从后往前，与真实的子窗口 z 顺序一致。
	HWND child = GetWindow(hwnd, GW_CHILD);
	if (child) child = GetWindow(child, GW_HWNDLAST);
	for (; child; child = GetWindow(child, GW_HWNDPREV)) {
		if (child == excluded || !(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
		POINT position{}; MapWindowPoints(child, hwnd, &position, 1);
		POINT origin{}; GetViewportOrgEx(dc, &origin);
		const int childState = SaveDC(dc);
		if (!childState) continue;
		SetViewportOrgEx(dc, origin.x + position.x, origin.y + position.y, nullptr);
		PaintClientTree(child, dc, excluded);
		RestoreDC(dc, childState);
	}
	RestoreDC(dc, saved);
}

inline std::unique_ptr<coral::win32::soft::Canvas> CaptureBackdrop(HWND parent, HWND excluded = nullptr)
{
	RECT client{}; GetClientRect(parent, &client);
	auto canvas = std::make_unique<coral::win32::soft::Canvas>(client.right, client.bottom);
	if (canvas->valid()) {
		canvas->surface().fill(SettingsWindow::skin().window);
		PaintClientTree(parent, canvas->dc(), excluded);
		GdiFlush();
	}
	return canvas;
}

// 底图 + 半透明遮罩
inline void PaintBackdrop(coral::win32::soft::Canvas& canvas, const coral::win32::soft::Canvas* backdrop)
{
	using namespace coral::win32::soft;
	auto surface = canvas.surface();
	surface.fill(SettingsWindow::skin().window);
	if (backdrop && backdrop->valid()) {
		backdrop->blit(canvas.dc(), 0, 0);
		GdiFlush();
		for (std::size_t i = 0; i < canvas.count(); ++i) canvas.pixels()[i] |= 0xFF000000;
	}
	FillRoundRect(surface, { 0, 0, static_cast<float>(canvas.width()), static_cast<float>(canvas.height()) }, 0, 0x48000000);
}
}

#endif // SHEETBACKDROP_H

#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGS_DPI_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGS_DPI_H_INCLUDED

#include "soft_canvas.h"

namespace coral::settings_ui
{
// 布局与输入保持 96-DPI 单位。只有 Win32 与光栅边界
// 转换为设备像素；文字直接按设备尺寸渲染。
class DpiScale
{
public:
    explicit DpiScale(UINT dpi = 96) : dpi_(dpi ? dpi : 96) {}
    UINT dpi() const { return dpi_; }
    float factor() const { return static_cast<float>(dpi_) / 96.0f; }
    int pixels(int value) const { return ::MulDiv(value, dpi_, 96); }
    int logical(int value) const { return ::MulDiv(value, 96, dpi_); }
    RECT pixels(RECT rect) const {
        return { pixels(rect.left), pixels(rect.top), pixels(rect.right), pixels(rect.bottom) };
    }
    RECT logical(RECT rect) const {
        return { logical(rect.left), logical(rect.top), logical(rect.right), logical(rect.bottom) };
    }
    POINT logical(POINT point) const { return { logical(point.x), logical(point.y) }; }
private:
    UINT dpi_;
};

// 仅设置窗口选择逐显示器绘制。其他顶层窗口保持
// 既有的感知级别，Windows 在每个窗口的上下文中分发消息。
class DpiContext
{
public:
    explicit DpiContext(DPI_AWARENESS_CONTEXT context)
        : previous_(::SetThreadDpiAwarenessContext(context)) {}
    ~DpiContext() { if (previous_) ::SetThreadDpiAwarenessContext(previous_); }
    DpiContext(const DpiContext&) = delete;
    DpiContext& operator=(const DpiContext&) = delete;
private:
    DPI_AWARENESS_CONTEXT previous_;
};

class DpiCanvas : public coral::win32::soft::Canvas
{
public:
    DpiCanvas(int width, int height, DpiScale scale)
        : Canvas(scale.pixels(width), scale.pixels(height)), scale_(scale) {}
    coral::win32::soft::Surface surface() const {
        return { pixels(), width(), height(), scale_.factor() };
    }
    void blit(HDC target, int x, int y) const {
        Canvas::blit(target, scale_.pixels(x), scale_.pixels(y));
    }
private:
    DpiScale scale_;
};
}
#endif

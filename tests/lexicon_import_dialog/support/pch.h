#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// 把生产对话框与共享内存及用户 INI 文件隔离。
struct TestConfig {
    bool traditional = false;
    bool runtime_traditional() const { return traditional; }
};
inline TestConfig testConfig;
inline TestConfig* sConfig = &testConfig;

#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGSWINDOW_H_INCLUDED
struct SettingsSkin {
    std::uint32_t window = 0xFFF7F8FA, surface = 0xFFFFFFFF, hover = 0xFFF5F7F9, pressed = 0xFFEDF0F3;
    std::uint32_t line = 0x1A192634, lineStrong = 0x33192634, text = 0xFF18202A;
    std::uint32_t sub = 0xFF5B6573, muted = 0xFF7B8490, danger = 0xFFC42B1C;
    std::uint32_t accent = 0xFF0F6CBD, accentHover = 0xFF115EA3, accentPressed = 0xFF0C5A9E;
    std::uint32_t onAccent = 0xFFFFFFFF, scrollThumb = 0xFFB8C0C9, scrollThumbHover = 0xFF98A2AD;
    bool darkCaption = false;
    std::wstring fontName = L"Microsoft YaHei UI";
};
class SettingsWindow {
public:
    static inline SettingsSkin testSkin;
    static const SettingsSkin& skin() { return testSkin; }
};

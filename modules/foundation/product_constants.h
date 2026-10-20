#pragma once
#ifndef CORAL_MODULES_FOUNDATION_PRODUCT_CONSTANTS_H_INCLUDED
#define CORAL_MODULES_FOUNDATION_PRODUCT_CONSTANTS_H_INCLUDED

#include <string>

namespace coralpinyinglobal
{
    enum class ThemeMode : int
    {
        LightTheme = 0,
        DarkTheme = 1,
        SystemTheme = 2
    };

    constexpr int DEFAULT_CANDIDATE_COUNT = 7;
    constexpr int DEFAULT_CANDIDATE_FONT_SIZE = 16;
    constexpr int MINIMUM_CANDIDATE_FONT_SIZE = 12;
    constexpr int MAXIMUM_CANDIDATE_FONT_SIZE = 28;
    constexpr const char* const BACKGROUND_COLOR = "background";
	constexpr const char* const CARD_BACKGROUND_COLOR = "cardBackground";
	constexpr const char* const TEXT_COLOR = "textColor";
	constexpr const char* const SECONDARY_TEXT_COLOR = "secondaryText";
	constexpr const char* const BORDER_COLOR = "borderColor";
	constexpr const char* const ACCENT_COLOR = "accentColor";
	constexpr const char* const HOVER_COLOR = "hoverColor";
	constexpr const char* const SELECTED_COLOR = "selectedColor";
	constexpr const char* const SEPARATOR_COLOR = "separatorColor";
	constexpr const char* const DISABLED_TEXT_COLOR = "disabledText";
	constexpr const char* const DISABLED_BACKGROUND_COLOR = "disabledBackground";
	constexpr const char* const DISABLED_BORDER_COLOR = "disabledBorder";
	constexpr const char* const TODAY_COUNT_COLOR = "todayCountColor";

	constexpr const char* const LIGHT_THEME = "light";
	constexpr const char* const DARK_THEME = "dark";
	constexpr const char* const SYSTEM_THEME = "system";

	std::string getColor(const std::string& colorKey, const ThemeMode mode);
} // 命名空间 coralpinyinglobal 结束

#endif // COMMONDEFS_H

#include "product_constants.h"
#include <map>

namespace coralpinyinglobal
{
	const std::map<std::string, std::string> lightThemeColors = 
	{
		{BACKGROUND_COLOR, "#F7F7F7"},
		{CARD_BACKGROUND_COLOR, "#FFFFFF"},
		{TEXT_COLOR, "#333333"},
		{SECONDARY_TEXT_COLOR, "#999999"},
		{BORDER_COLOR, "#E5E5E5"},
		{ACCENT_COLOR, "#3B64FC"},
		{HOVER_COLOR, "#4F87F5"},
		{SELECTED_COLOR, "#3B64FC"},
		{SEPARATOR_COLOR, "#F3F3F3"},
		{DISABLED_TEXT_COLOR, "#999999"},
		{DISABLED_BACKGROUND_COLOR, "#F0F0F0"},
		{DISABLED_BORDER_COLOR, "#DDDDDD"},
		{TODAY_COUNT_COLOR, "#0000FF"}
	};

	const std::map<std::string, std::string> darkThemeColors = 
	{
		{BACKGROUND_COLOR, "#333333"},
		{CARD_BACKGROUND_COLOR, "#2D2D2D"},
		{TEXT_COLOR, "#FFFFFF"},
		{SECONDARY_TEXT_COLOR, "#666666"},
		{BORDER_COLOR, "#404040"},
		{ACCENT_COLOR, "#4A90E2"},
		{HOVER_COLOR, "#5BA3F5"},
		{SELECTED_COLOR, "#4A90E2"},
		{SEPARATOR_COLOR, "#404040"},
		{DISABLED_TEXT_COLOR, "#777777"},
		{DISABLED_BACKGROUND_COLOR, "#2D2D2D"},
		{DISABLED_BORDER_COLOR, "#333333"},
		{TODAY_COUNT_COLOR, "#2563EB"}
	};
	
	std::string getColor(const std::string& colorKey, const ThemeMode mode)
	{
		if (mode == ThemeMode::DarkTheme)
		{
			auto it = darkThemeColors.find(colorKey);
			if (it != darkThemeColors.end())
			{
				return it->second;
			}
			else
			{
				return "#FFFFFF"; // 默认值
			}
		}
		else
		{
			auto it = lightThemeColors.find(colorKey);
			if (it != lightThemeColors.end())
			{
				return it->second;
			}
			else
			{
				return "#000000"; // 默认值
			}
		}
	}
} // 命名空间 coralpinyinglobal 结束

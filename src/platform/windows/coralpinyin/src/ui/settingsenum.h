#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGSENUM_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGSENUM_H_INCLUDED
namespace SettingsEnum {
	enum class SettingType : unsigned int {
		routine = 0,//常规
		keyboard,//按键
		appearance,//外观
		lexicon,//词库和自学习
		phrase,//常用语和剪贴板
		senior,//高级
		speech,//语音输入
		about //关于输入法和升级
	};
}

#endif // SETTINGSENUM_H

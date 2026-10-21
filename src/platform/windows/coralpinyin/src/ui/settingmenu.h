#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGMENU_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGMENU_H_INCLUDED

#include "pch.h"

// 菜单控制器（Qt-free）：菜单 UI 由 coraltsf 的原生 CSettingMenu 实现（经 IPC 转发到此处）。
// 设置窗口/添加词条窗口为 Win32 原生实现；仅在用户点击反馈时打开官网。

class SettingMenu
{
public:
	static SettingMenu* getMenu();

	void showSettingMenu();
	void showAddPhrasePage(const std::wstring& pinyin, const std::wstring& word);
	void onSendFeedbackActionTriggered();

private:
	SettingMenu() = default;
	~SettingMenu() = default;
	SettingMenu(const SettingMenu&) = delete;
	SettingMenu& operator=(const SettingMenu&) = delete;

	void openUrl(const std::wstring& url);
};

#endif // SETTINGMENU_H

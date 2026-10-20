#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGMENU_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_SETTINGMENU_H_INCLUDED

#include "pch.h"

class SettingsWindow;
namespace coralnet { class HttpRequest; }

// 菜单控制器（Qt-free）：菜单 UI 由 coraltsf 的原生 CSettingMenu 实现（经 IPC 转发到此处）。
// 设置窗口/添加词条窗口为 Win32 原生实现；网络使用 coralnet（libcurl）。

class SettingMenu
{
public:
	static SettingMenu* getMenu();

	void showSettingMenu();
	void showAddPhrasePage(const std::wstring& pinyin, const std::wstring& word);
	void onSendFeedbackActionTriggered();
	void fetchPlaceholderData();

private:
	SettingMenu() = default;
	~SettingMenu() = default;
	SettingMenu(const SettingMenu&) = delete;
	SettingMenu& operator=(const SettingMenu&) = delete;

	void openUrl(const std::wstring& url);
	void onPlaceholderDataReceived(const std::string& body, bool success);

private:
	std::unique_ptr<coralnet::HttpRequest> m_placeholderRequest;
};

#endif // SETTINGMENU_H

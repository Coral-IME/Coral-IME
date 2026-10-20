#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_CONFIRMDIALOG_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_CONFIRMDIALOG_H_INCLUDED

#include "pch.h"

/*
 * Qt-free 确认对话框（MessageBox 封装，保持与原 confirmdialog 相同的调用语义）。
 */
class ConfirmDialog
{
public:
	// 返回 true 表示用户确认
	static bool show(HWND parent, const std::wstring& message,
		const std::wstring& title = L"CORAL拼音", bool defaultButtonIsCancel = true);

	// 返回：true=是 false=否
	static bool showYesNo(HWND parent, const std::wstring& message,
		const std::wstring& title = L"CORAL拼音");
};

#endif // CONFIRMDIALOG_H

#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_ADDPHRASEDIALOG_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_ADDPHRASEDIALOG_H_INCLUDED

#include "pch.h"

/*
 * 常用语添加/编辑弹窗（单实例）。字段：内容 / 输入码；按钮：取消、添加。
 * 输入码留空时使用默认输入码：内容前 3 个汉字的拼音首字母，或开头英文单词的前 3 个字母。
 * owner 非空时以模态方式盖在 owner 上；从输入法候选窗唤起时 owner 为空，居中屏幕显示。
 */
class AddPhraseDialog
{
public:
	struct Request
	{
		std::wstring content;
		std::wstring code;
		// 编辑已有常用语：保存时先删除旧条目。
		bool editing = false;
		std::wstring originalCode;
		std::wstring originalContent;
	};

	static void show(HWND owner, const Request& request);
	static void close();
	static bool isInstanceAlive() { return s_hwnd != nullptr; }

	// 内容为空或无法生成时返回空串。
	static std::wstring defaultCode(const std::wstring& content);

private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK editProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR id, DWORD_PTR data);

	static inline HWND s_hwnd = nullptr;
};

#endif // ADDPHRASEDIALOG_H

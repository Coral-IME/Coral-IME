#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_LEXICONIMPORTDIALOG_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_LEXICONIMPORTDIALOG_H_INCLUDED

#include "pch.h"

// 设置窗口内嵌的词库导入面板：选择文件 → 预览（编码/格式/拼音/查重）→ 导入结果。
class LexiconImportDialog
{
public:
	static void showInstance(HWND parent);
	// 外部迁移工具交回的 TSV 仍经过同一预览、校验和用户确认。
	static void show_file(HWND parent, const std::wstring& path);
	static void closeInstance();
	static void resizeToParent();

private:
	static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	static inline HWND s_hwnd = nullptr;
};

#endif // LEXICONIMPORTDIALOG_H

#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_CLIPBOARDHISTORY_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_CLIPBOARDHISTORY_H_INCLUDED

#include "clipboard_history.h"

#include <windows.h>

#include <filesystem>
#include <vector>

// 剪贴板历史的唯一写入方，只在主线程使用。监听系统剪贴板，
// 把复制的文字写入 LocalLow 下的历史文件，供 TSF 的剪贴板候选读取。
class ClipboardHistory
{
public:
	static ClipboardHistory& instance();

	void attach(HWND listener);
	void detach();
	void onClipboardUpdate();

	const std::vector<coral::clipboard::Entry>& entries();
	bool remove(std::size_t index);
	void clear();

private:
	ClipboardHistory() = default;
	void ensureLoaded();
	void pruneExpired();
	void persist();
	static bool readClipboardText(HWND owner, std::wstring& text);

	HWND m_listener = nullptr;
	bool m_loaded = false;
	std::filesystem::path m_path;
	std::vector<coral::clipboard::Entry> m_entries;
};

#endif // CLIPBOARDHISTORY_H

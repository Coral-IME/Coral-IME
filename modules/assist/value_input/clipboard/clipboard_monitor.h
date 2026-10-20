#pragma once
#ifndef CORAL_MODULES_ASSIST_VALUE_INPUT_CLIPBOARD_CLIPBOARD_MONITOR_H_INCLUDED
#define CORAL_MODULES_ASSIST_VALUE_INPUT_CLIPBOARD_CLIPBOARD_MONITOR_H_INCLUDED

// -------------------------------------------------------------------------- //
//	文件名		：	clipboardmonitor.h
//	创建者		：	
//	创建时间	：	2024-08-18
//	功能描述	：	vv模式获取剪切板历史记录-剪切板监听器
//
// -------------------------------------------------------------------------- //
#define  UNICODE
#define WINVER 0x0600
#if defined(_WIN32) || defined(_MSC_VER)
#include <windows.h>
#endif
#include <string>
#include <thread>
#include <atomic>
#include "clipboard_ring.h"
class ClipboardMonitor
{

public:
	ClipboardMonitor();
	~ClipboardMonitor();
	std::vector<std::string> get_recently_data();
	bool start();
	void stop();
private:
	ClipboardMonitor(const ClipboardMonitor&) = delete;
	ClipboardMonitor& operator=(const ClipboardMonitor&) = delete;
	void run();
#if defined(_WIN32) || defined(_MSC_VER)
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#elif defined(__linux__) || defined(__ANDROID__)
//todo



#endif 
private:
#if defined(_WIN32) || defined(_MSC_VER)
	HWND hwnd_;
#endif 
	std::atomic<bool> running_;
	std::thread monitor_thread_;
	static CircularList data_;
	
};

ClipboardMonitor& get_k_clipboard_monitor_instance();

#endif // KCLIPBOARDMONITOR_H

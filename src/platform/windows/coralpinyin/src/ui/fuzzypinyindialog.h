#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_FUZZYPINYINDIALOG_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_UI_FUZZYPINYINDIALOG_H_INCLUDED

#include "pch.h"

// 内嵌可滚动的模糊拼音面板。19 个配置索引与
// 即时保存行为与输入引擎共享。
class FuzzyPinyinDialog
{
public:
    static void showInstance(HWND parent);
    static void closeInstance();
    static void resizeToParent();

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static inline HWND s_hwnd = nullptr;
};

#endif // FUZZYPINYINDIALOG_H

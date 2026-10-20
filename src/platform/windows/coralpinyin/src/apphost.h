#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_APPHOST_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALPINYIN_SRC_APPHOST_H_INCLUDED

#include "pch.h"

// 隐藏宿主窗口：承载全部周期定时器与跨线程任务投递，
// 替代原 QApplication 事件循环 + QMetaObject::invokeMethod(Qt::QueuedConnection) + QTimer。
// 任意线程可调用 appPostTask()，任务在主线程消息循环中串行执行。

bool appInit();               // 创建宿主窗口并启动全部周期任务
bool appStartSpeech();
void appStopSpeech();
bool appProcessSpeechMessage(MSG& message);
void appShutdown();           // 停止定时器并销毁宿主窗口
HWND appGetHwnd();            // 宿主窗口句柄（可能为 null）
void appPostTask(std::function<void()> task); // 线程安全：投递任务到主线程执行

#endif // APPHOST_H

#include "coral/product/product_identity.h"
#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_PROCESSMONITOR_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_PROCESSMONITOR_H_INCLUDED

#include <windows.h>

extern HANDLE g_hEvent; // 线程退出全局事件句柄

// 从注册表中读取 coralpinyin.exe 的路径
std::wstring GetCoralPinyinExePath();

// 检查进程是否在运行
bool IsProcessRunning(const wchar_t* process_name);

// 安装程序在其文件操作完成前持有这个既有的互斥量。
bool isInstalling();

// 监控和重启 coralpinyin.exe 的函数
void MonitorAndRestartCoralPinyin(const std::wstring& exePath);

// 检查目标进程是否加载指定 DLL；仅用于现有平台模块诊断
int IsDllLoadedInProcess(DWORD pid, const std::wstring& dllName);

// 重启 coralpinyin.exe 的函数
void RestartCoralPinyin();
// 确保coralpinyinsvc服务运行
bool EnsureServiceRunning(const wchar_t* serviceName = coral::product::kServiceNameWide);

#endif // PROCESSMONITOR_H

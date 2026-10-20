#pragma once
#ifndef CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TSFWINDOWSEVENT_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_CORALTSF_SRC_TSFWINDOWSEVENT_H_INCLUDED

#include <Windows.h>
#include <atlstr.h>
#include "shareddata.h"

struct KeyHandlerResult
{
    int inputType;
    int isCandidateShow;
    CString selected_text;
};

class KTSFWindowsEventManager
{
// 1) 打开/映射共享内存 "Loacal\\SharedMemory"
// 2) 打开事件 "EventFromTSF" 和 "EventAckToTSF"
// 3) 把数据写到共享内存并通知主进程处理，等待主进程完成后读取结果
public:
    static KTSFWindowsEventManager& instance();
    void initialize();
    void uninitialize();
    KeyHandlerResult invokeKeyHandlerOnMain(int pressKey, int preservedKeyStatus, const CString& rc);
    void setIsSearch(bool mode);
    bool getSearch() const;
private:
    KTSFWindowsEventManager();
    ~KTSFWindowsEventManager();
    KTSFWindowsEventManager(const KTSFWindowsEventManager&) = delete;
    KTSFWindowsEventManager& operator=(const KTSFWindowsEventManager&) = delete;
private:
    HANDLE      m_hMapFile = nullptr;        // 共享内存映射句柄
    SharedData* m_pSharedData = nullptr;    
    HANDLE      m_hEventFromTSF = nullptr;   // TSF -> 主进程
    HANDLE      m_hEventAckToTSF = nullptr;  // 主进程 -> TSF
    bool        m_initialized = false;  
    bool        m_isSearch = false;        //如果是searchhost进程，直接使用管道方案，也可以当作使用管道的标志位。
    int         m_timeoutCounter = 0;
    int         m_requestId = 0;
};

#endif // KTSFWINDOWSEVENT_H

#include "pch.h"
#include "tsfwindowsevent.h"

KTSFWindowsEventManager& KTSFWindowsEventManager::instance()
{
    static KTSFWindowsEventManager s_instance;
    return s_instance;
}

KTSFWindowsEventManager::KTSFWindowsEventManager()
{}

KTSFWindowsEventManager::~KTSFWindowsEventManager()
{
    uninitialize();
}

void KTSFWindowsEventManager::initialize()
{
    if (m_initialized)
        return ;

    m_hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SHAREDMEMORY);

    if (!m_hMapFile)
    {
        m_hMapFile = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(SharedData),
            SHAREDMEMORY
        );
        if (!m_hMapFile)
        {//windows中有些应用打开共享内存的时候需要提供管理员权限，比如win11的搜索栏
         //所以在这里有一个标志位m_isSearch，用来判断是否为搜索栏类似的无权打开共享内存的应用，是的话就走管道
            m_isSearch = true;
            return ;
        }
    }

    m_pSharedData = reinterpret_cast<SharedData*>(
        MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 0)
        );
    if (!m_pSharedData)
    {
        m_isSearch = true;
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
        return ;
    }
    // TSF -> 主进程
    m_hEventFromTSF = OpenEventW(EVENT_MODIFY_STATE, FALSE, EVENTFROMTSF);
    if (!m_hEventFromTSF)
    {
        m_isSearch = true;
        UnmapViewOfFile(m_pSharedData);
        m_pSharedData = nullptr;
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
        return ;

    }
    // 主进程 -> TSF
    m_hEventAckToTSF = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, EVENTTOTSF);
    if (!m_hEventAckToTSF)
    {
        m_isSearch = true;
        UnmapViewOfFile(m_pSharedData);
        m_pSharedData = nullptr;
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
        CloseHandle(m_hEventFromTSF);
        m_hEventFromTSF = NULL;
        return ;
    }
    m_initialized = true;
    return ;
}

void KTSFWindowsEventManager::uninitialize()
{
    if (m_pSharedData)
    {
        UnmapViewOfFile(m_pSharedData);
        m_pSharedData = nullptr;
    }
    if (m_hMapFile)
    {
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
    }
    if (m_hEventFromTSF)
    {
        CloseHandle(m_hEventFromTSF);
        m_hEventFromTSF = NULL;
    }
    if (m_hEventAckToTSF)
    {
        CloseHandle(m_hEventAckToTSF);
        m_hEventAckToTSF = NULL;
    }
    m_initialized = false;
}

// 写入共享内存 -> SetEvent -> Wait -> 读取结果
KeyHandlerResult KTSFWindowsEventManager::invokeKeyHandlerOnMain(int pressKey, int preservedKeyStatus, const CString& rc)
{
    KeyHandlerResult ret{};
    if (!m_initialized || !m_pSharedData)
    {
        ret.isCandidateShow = -1;
        ret.inputType = -1;
        ret.selected_text = "init fail,shared memory created failed";
        return ret;
    }

    // 写入共享内存 
    m_pSharedData->request_id = (++m_requestId) % INT_MAX;
    m_pSharedData->pressKey = pressKey;
    m_pSharedData->preservedKeyStatus = preservedKeyStatus;
    _tcscpy_s(m_pSharedData->rc, 256, rc);
    m_pSharedData->reset();

    ResetEvent(m_hEventAckToTSF);
    // SetEvent
    SetEvent(m_hEventFromTSF);
    // Wait
    DWORD dw = WaitForSingleObject(m_hEventAckToTSF, 1000);
    //这里需要设置一个等待超时的机制，当主程序由于某种原因挂掉，这里将无法收到事件从而阻塞掉当前进程；
    //之前的机制是：一直等待，直到有事件传来，如果主进程没有消息传来就会阻塞当前进程；
    //现在的机制为如果等待超过1000ms，放弃这次解析，最坏的结果是输入没有响应但不会导致进程卡住；
    //同时当这种超时连续发生三次的话，直接退化成管道方案，从而不影响正常输入。
    if (dw == WAIT_OBJECT_0)
    {
        m_timeoutCounter = 0;
    }
    else
    {
        m_timeoutCounter++;
        if (m_timeoutCounter >= 3)
            // 超时超过3次，退化为管道通信
            m_isSearch = true;
        ret.isCandidateShow = -1;
        ret.inputType = -1;
        ret.selected_text = "timeout";
        return ret;
    }
    
    if (m_pSharedData->responseId != m_requestId)
    {
        ret.isCandidateShow = -1;
        ret.inputType = -1;
        ret.selected_text = "Response mismatch or outdated";
        return ret;
    }

    // 读取结果
    ret.inputType = m_pSharedData->inputType;
    ret.isCandidateShow = m_pSharedData->isCandidateShow;
    ret.selected_text = m_pSharedData->selected_text;

    return ret;
}

void KTSFWindowsEventManager::setIsSearch(bool mode)
{
    m_isSearch = mode;
}

bool KTSFWindowsEventManager::getSearch() const
{
    return m_isSearch;
}

#include "pch.h"
#include "pipeserver.h"
#include "pipeio.h"
#include "pipe_peer_scope.h"
#include <memory>
#include <new>

CPipeServer::CPipeServer()
    : m_pfnCallback(nullptr), m_CallbackContext(nullptr), m_bIsRunning(FALSE), m_hThread(nullptr)
{
    ::InitializeThreadpoolEnvironment(&m_environment);
}

CPipeServer::~CPipeServer()
{
    Stop();
    ::DestroyThreadpoolEnvironment(&m_environment);
}

HRESULT CPipeServer::Start(LPCWSTR name, long threads, PFN_CallBack callback, LPVOID context)
{
    if (m_bIsRunning) return S_FALSE;
    if (ipc_hlp::IsStrEmpty(name) || threads <= 0) return E_INVALIDARG;
    m_pfnCallback = callback;
    m_CallbackContext = context;
    m_strPipeName.Format(L"\\\\.\\PIPE\\%s", name);

    HRESULT hr = S_OK;
    m_stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) hr = HRESULT_FROM_WIN32(::GetLastError());
    if (SUCCEEDED(hr) && !m_PipeMgr.Init(m_strPipeName))
        hr = HRESULT_FROM_WIN32(::GetLastError());
    if (SUCCEEDED(hr))
    {
        m_pool = ::CreateThreadpool(nullptr);
        if (!m_pool) hr = HRESULT_FROM_WIN32(::GetLastError());
    }
    if (SUCCEEDED(hr))
    {
        ::SetThreadpoolThreadMaximum(m_pool, threads);
        if (!::SetThreadpoolThreadMinimum(m_pool, threads))
            hr = HRESULT_FROM_WIN32(::GetLastError());
    }
    if (SUCCEEDED(hr))
    {
        ::SetThreadpoolCallbackPool(&m_environment, m_pool);
        m_work = ::CreateThreadpoolWork(_WorkCallback, this, &m_environment);
        if (!m_work) hr = HRESULT_FROM_WIN32(::GetLastError());
    }
    if (SUCCEEDED(hr))
    {
        m_hThread = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, _ConnectThread, this, 0, nullptr));
        if (!m_hThread) hr = E_OUTOFMEMORY;
    }
    if (FAILED(hr))
    {
        _CloseResources();
        return hr;
    }
    m_bIsRunning = TRUE;
    return S_OK;
}

void CPipeServer::_CloseResources()
{
    if (m_stopEvent) ::SetEvent(m_stopEvent);
    if (m_hThread)
    {
        ::WaitForSingleObject(m_hThread, INFINITE);
        ::CloseHandle(m_hThread);
        m_hThread = nullptr;
    }
    if (m_work)
    {
        // 排空排队的回调：每个回调取回自己的管道，Stop 之后不再发起 I/O。
        ::WaitForThreadpoolWorkCallbacks(m_work, FALSE);
        ::CloseThreadpoolWork(m_work);
        m_work = nullptr;
    }
    if (m_pool)
    {
        ::CloseThreadpool(m_pool);
        m_pool = nullptr;
    }
    m_PipeMgr.UnInit();
    if (m_stopEvent)
    {
        ::CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
    m_bIsRunning = FALSE;
}

void CPipeServer::Stop()
{
    if (m_bIsRunning) _CloseResources();
}

unsigned CALLBACK CPipeServer::_ConnectThread(LPVOID context)
{
    auto* server = static_cast<CPipeServer*>(context);
    while (::WaitForSingleObject(server->m_stopEvent, 0) != WAIT_OBJECT_0)
    {
        HANDLE pipe = server->m_PipeMgr.CreatePipe();
        if (pipe == INVALID_HANDLE_VALUE)
        {
            if (::WaitForSingleObject(server->m_stopEvent, 10) == WAIT_OBJECT_0) break;
            continue;
        }
        HRESULT hr = coral_ipc::Connect(pipe, server->m_stopEvent);
        if (FAILED(hr) || ::WaitForSingleObject(server->m_stopEvent, 0) == WAIT_OBJECT_0)
        {
            server->m_PipeMgr.ClosePipe(pipe);
            continue;
        }
        try
        {
            std::lock_guard<std::mutex> lock(server->m_jobsMutex);
            server->m_jobs.push(pipe);
            ::SubmitThreadpoolWork(server->m_work);
        }
        catch (const std::bad_alloc&)
        {
            server->m_PipeMgr.ClosePipe(pipe);
        }
    }
    return 0;
}

void CALLBACK CPipeServer::_WorkCallback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK)
{
    auto* server = static_cast<CPipeServer*>(context);
    HANDLE pipe;
    {
        std::lock_guard<std::mutex> lock(server->m_jobsMutex);
        pipe = server->m_jobs.front();
        server->m_jobs.pop();
    }
    _DealConnectPipe(server, pipe);
}

void CALLBACK CPipeServer::_DealConnectPipe(LPVOID context, LPVOID handle)
{
    auto* server = static_cast<CPipeServer*>(context);
    HANDLE pipe = static_cast<HANDLE>(handle);
    // 所有退出路径（包括回调/分配异常）都把实例归还对象池。
    struct ReturnPipe
    {
        CPipeMgr& manager;
        HANDLE pipe;
        ~ReturnPipe() { manager.ClosePipe(pipe); }
    } cleanup{server->m_PipeMgr, pipe};

    try
    {
        const ULONGLONG deadline = ::GetTickCount64() + coral_ipc::ServerIoTimeoutMs;
        DWORD length = 0, transferred = 0;
        HRESULT hr = coral_ipc::Transfer(pipe, &length, sizeof(length), transferred,
                                         false, deadline, server->m_stopEvent);
        if (FAILED(hr) || !coral_ipc::ValidLength(length)) return;
        std::unique_ptr<WCHAR[]> buffer(new (std::nothrow) WCHAR[static_cast<size_t>(length) + 1]);
        if (!buffer) return;
        hr = coral_ipc::Transfer(pipe, buffer.get(), length * sizeof(WCHAR), transferred,
                                 false, deadline, server->m_stopEvent);
        if (FAILED(hr)) return;
        buffer[length] = 0;
        if (wmemchr(buffer.get(), 0, length)) return;

        CString response;
        if (server->m_pfnCallback)
        {
            ULONG process_id = 0;
            ULONG session_id = 0;
            std::optional<coral::ipc::PipePeer> peer;
            if (::GetNamedPipeClientProcessId(pipe, &process_id) && process_id &&
                ::GetNamedPipeClientSessionId(pipe, &session_id))
                peer = coral::ipc::PipePeer{process_id, session_id};
            coral::ipc::ScopedPipePeer scope(peer);
            server->m_pfnCallback(server->m_CallbackContext, buffer.get(), response);
        }
        length = static_cast<DWORD>(response.GetLength());
        if (!coral_ipc::ValidLength(length)) return;
        hr = coral_ipc::Transfer(pipe, &length, sizeof(length), transferred,
                                 true, deadline, server->m_stopEvent);
        if (FAILED(hr)) return;
        hr = coral_ipc::Transfer(pipe, const_cast<LPWSTR>(response.GetString()),
                                 length * sizeof(WCHAR), transferred, true, deadline, server->m_stopEvent);
        if (FAILED(hr)) return;
        // 保持管道连接，直到客户端消费完回复并关闭。
        // 永不关闭的客户端受同一截止期限和 Stop 事件约束。
        coral_ipc::Transfer(pipe, &length, sizeof(length), transferred,
                            false, deadline, server->m_stopEvent);
    }
    catch (...)
    {
        // 不允许 API/解析异常逃出 Windows 工作线程回调。
    }
}

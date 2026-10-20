#include "pch.h"
#include "pipeclient.h"
#include "pipeio.h"

CPipeClient::CPipeClient() : m_hPipe(INVALID_HANDLE_VALUE), m_deadline(0) {}
CPipeClient::~CPipeClient() { DisconnectServer(); }

HRESULT CPipeClient::ConnectServer(LPCWSTR name, int retries)
{
    if (ipc_hlp::IsStrEmpty(name) || retries <= 0) return E_INVALIDARG;
    DisconnectServer();
    m_deadline = ::GetTickCount64() + coral_ipc::CallTimeoutMs;
    CString pipeName;
    pipeName.Format(L"\\\\.\\PIPE\\%s", name);
    HRESULT hr = E_FAIL;
    for (int attempt = 0; attempt < retries; ++attempt)
    {
        hr = _TryConnectServer(pipeName);
        if (SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_TIMEOUT)) return hr;
        if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
            hr != HRESULT_FROM_WIN32(ERROR_PIPE_BUSY)) return hr;
        if (attempt + 1 < retries)
        {
            DWORD remaining = coral_ipc::Remaining(m_deadline);
            if (!remaining) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            DWORD backoff = attempt == 0 ? 20 : 40;
            ::Sleep(remaining < backoff ? remaining : backoff);
        }
    }
    return hr;
}

HRESULT CPipeClient::SendToServer(const void* buffer, DWORD bytes, PDWORD sent)
{
    if (!sent) return E_INVALIDARG;
    if (m_hPipe == INVALID_HANDLE_VALUE) return E_FAIL;
    return coral_ipc::Transfer(m_hPipe, const_cast<void*>(buffer), bytes, *sent, true, m_deadline);
}

HRESULT CPipeClient::RecvFromServer(void* buffer, DWORD bytes, PDWORD received)
{
    if (!received) return E_INVALIDARG;
    if (m_hPipe == INVALID_HANDLE_VALUE) return E_FAIL;
    return coral_ipc::Transfer(m_hPipe, buffer, bytes, *received, false, m_deadline);
}

void CPipeClient::DisconnectServer()
{
    if (m_hPipe != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(m_hPipe);
        m_hPipe = INVALID_HANDLE_VALUE;
    }
}

HRESULT CPipeClient::_TryConnectServer(LPCWSTR name)
{
    for (;;)
    {
        DWORD remaining = coral_ipc::Remaining(m_deadline);
        if (!remaining) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        m_hPipe = ::CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (m_hPipe != INVALID_HANDLE_VALUE)
        {
            DWORD mode = PIPE_READMODE_MESSAGE;
            if (::SetNamedPipeHandleState(m_hPipe, &mode, nullptr, nullptr)) return S_OK;
            const DWORD error = ::GetLastError();
            DisconnectServer();
            return HRESULT_FROM_WIN32(error);
        }
        DWORD error = ::GetLastError();
        if (error != ERROR_PIPE_BUSY) return HRESULT_FROM_WIN32(error);
        if (!::WaitNamedPipeW(name, remaining))
        {
            error = ::GetLastError();
            return HRESULT_FROM_WIN32(error == ERROR_SEM_TIMEOUT ? ERROR_TIMEOUT : error);
        }
    }
}

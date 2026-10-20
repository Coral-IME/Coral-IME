#pragma once

#include <Windows.h>

namespace coral_ipc
{
// 长度前缀以 UTF-16 码元计数，而不是字节。每条线路载荷上限 1 MiB。
constexpr DWORD MaxMessageChars = 512 * 1024;
constexpr DWORD CallTimeoutMs = 1500;
constexpr DWORD ServerIoTimeoutMs = 1500;

inline bool ValidLength(DWORD length)
{
    return length > 0 && length <= MaxMessageChars;
}

inline DWORD Remaining(ULONGLONG deadline)
{
    const ULONGLONG now = ::GetTickCount64();
    return now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
}

class Event
{
public:
    Event() : m_handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Event() { if (m_handle) ::CloseHandle(m_handle); }
    HANDLE get() const { return m_handle; }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
private:
    HANDLE m_handle;
};

inline HRESULT Await(HANDLE pipe, OVERLAPPED& operation, DWORD timeout,
                     HANDLE stop, DWORD& transferred)
{
    HANDLE events[] = { stop, operation.hEvent };
    DWORD result = stop ? ::WaitForMultipleObjects(2, events, FALSE, timeout)
                        : ::WaitForSingleObject(operation.hEvent, timeout);
    if (result == (stop ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0))
    {
        if (::GetOverlappedResult(pipe, &operation, &transferred, FALSE))
            return S_OK;
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    DWORD error = result == WAIT_TIMEOUT ? ERROR_TIMEOUT :
                  result == WAIT_FAILED ? ::GetLastError() : ERROR_OPERATION_ABORTED;
    ::CancelIoEx(pipe, &operation);
    // 内核必须用完 OVERLAPPED 和缓冲区之后才能销毁它们。
    ::GetOverlappedResult(pipe, &operation, &transferred, TRUE);
    return HRESULT_FROM_WIN32(error);
}

inline HRESULT Transfer(HANDLE pipe, void* buffer, DWORD bytes, DWORD& transferred,
                        bool write, ULONGLONG deadline, HANDLE stop = nullptr)
{
    transferred = 0;
    if (!buffer || bytes == 0) return E_INVALIDARG;
    if (stop && ::WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
        return HRESULT_FROM_WIN32(ERROR_OPERATION_ABORTED);
    if (Remaining(deadline) == 0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);

    Event event;
    if (!event.get()) return HRESULT_FROM_WIN32(::GetLastError());
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    BOOL done = write ? ::WriteFile(pipe, buffer, bytes, &transferred, &operation)
                      : ::ReadFile(pipe, buffer, bytes, &transferred, &operation);
    if (!done)
    {
        const DWORD error = ::GetLastError();
        if (error != ERROR_IO_PENDING) return HRESULT_FROM_WIN32(error);
        HRESULT hr = Await(pipe, operation, Remaining(deadline), stop, transferred);
        if (FAILED(hr)) return hr;
    }
    // 每个前缀与载荷构成一条完整的管道消息。拒绝过短/超长帧。
    return transferred == bytes ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

inline HRESULT Connect(HANDLE pipe, HANDLE stop)
{
    Event event;
    if (!event.get()) return HRESULT_FROM_WIN32(::GetLastError());
    OVERLAPPED operation{};
    operation.hEvent = event.get();
    if (::ConnectNamedPipe(pipe, &operation)) return S_OK;
    const DWORD error = ::GetLastError();
    if (error == ERROR_PIPE_CONNECTED) return S_OK;
    if (error != ERROR_IO_PENDING) return HRESULT_FROM_WIN32(error);
    DWORD transferred = 0;
    return Await(pipe, operation, INFINITE, stop, transferred);
}
}

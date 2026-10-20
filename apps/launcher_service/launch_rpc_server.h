#pragma once
#include <windows.h>
#include <atlstr.h>
#include "ipcserver.h"
#include "launch_request.h"

namespace coral::service {
class LaunchRpcServer {
public:
    explicit LaunchRpcServer(LaunchActions& actions) : actions_(actions) {}
    ~LaunchRpcServer();
    LaunchRpcServer(const LaunchRpcServer&) = delete;
    LaunchRpcServer& operator=(const LaunchRpcServer&) = delete;
    HRESULT start(const wchar_t* pipe_name);
    void stop();
private:
    static HRESULT CALLBACK launch_settings(void* context, const std::vector<CString>& params, CIpcResponse& response);
    static HRESULT CALLBACK launch_session_host(void* context, const std::vector<CString>& params, CIpcResponse& response);
    static HRESULT CALLBACK launch_renderer(void* context, const std::vector<CString>& params, CIpcResponse& response);
    HRESULT dispatch(LaunchTarget target, const std::vector<CString>& params, CIpcResponse& response);
    LaunchActions& actions_;
    CIpcServer server_;
    bool started_ = false;
};
} // 命名空间 coral::service 结束

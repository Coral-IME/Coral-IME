#include "launch_rpc_server.h"
#include "pipe_peer.h"

namespace coral::service {
namespace {
HRESULT reply_hresult(const LaunchReply& reply) {
    switch (reply.status) {
    case LaunchStatus::kStarted: case LaunchStatus::kAlreadyRunning: return S_OK;
    case LaunchStatus::kInstalling: return S_FALSE;
    case LaunchStatus::kInvalidArgument: return E_INVALIDARG;
    case LaunchStatus::kAccessDenied: return E_ACCESSDENIED;
    case LaunchStatus::kPathNotFound: return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    case LaunchStatus::kLaunchFailed: return reply.error ? HRESULT_FROM_WIN32(reply.error) : E_FAIL;
    default: return E_FAIL;
    }
}
}
LaunchRpcServer::~LaunchRpcServer() { stop(); }
HRESULT LaunchRpcServer::start(const wchar_t* pipe_name) {
    if (started_) return S_FALSE;
    auto hr = server_.RegisterApi(L"LaunchMainProcess", launch_settings);
    if (SUCCEEDED(hr)) hr = server_.RegisterApi(L"LaunchEngineHost", launch_session_host);
    if (SUCCEEDED(hr)) hr = server_.RegisterApi(L"LaunchRenderer", launch_renderer);
    if (SUCCEEDED(hr)) hr = server_.Start(pipe_name, this, 1);
    started_ = SUCCEEDED(hr);
    return hr;
}
void LaunchRpcServer::stop() {
    if (started_) server_.Stop();
    started_ = false;
}
HRESULT CALLBACK LaunchRpcServer::launch_settings(void* context, const std::vector<CString>& params, CIpcResponse& response) {
    return static_cast<LaunchRpcServer*>(context)->dispatch(LaunchTarget::kSettings, params, response);
}
HRESULT CALLBACK LaunchRpcServer::launch_session_host(void* context, const std::vector<CString>& params, CIpcResponse& response) {
    return static_cast<LaunchRpcServer*>(context)->dispatch(LaunchTarget::kSessionHost, params, response);
}
HRESULT CALLBACK LaunchRpcServer::launch_renderer(void* context, const std::vector<CString>& params, CIpcResponse& response) {
    return static_cast<LaunchRpcServer*>(context)->dispatch(LaunchTarget::kRenderer, params, response);
}
HRESULT LaunchRpcServer::dispatch(LaunchTarget target, const std::vector<CString>& params, CIpcResponse& response) {
    std::uint32_t session_id = 0;
    LaunchReply reply{LaunchStatus::kInvalidArgument};
    if (params.size() == 1 && parse_session_id(std::wstring_view(params[0].GetString(), params[0].GetLength()), session_id)) {
        std::optional<CallerSession> caller;
        const auto peer = ipc::current_pipe_peer();
        if (peer) caller = CallerSession{peer->process_id, peer->session_id};
        reply = handle_launch({target, session_id}, caller, actions_);
    }
    response.AddKeyValue(L"status", launch_status_name(reply.status));
    if (reply.error) response.AddKeyValue(L"error", static_cast<long>(reply.error));
    return reply_hresult(reply);
}
} // 命名空间 coral::service 结束

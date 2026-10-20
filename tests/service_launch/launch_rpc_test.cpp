#include "launch_rpc_server.h"
#include "ipcclient.h"
#include "pipe_peer.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#include <string>
#include <atomic>

namespace {
using namespace coral::service;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::uint32_t own_session() {
    DWORD session = 0; require(::ProcessIdToSessionId(::GetCurrentProcessId(),&session) && session, "interactive test session"); return session;
}
struct Actions final : LaunchActions {
    std::atomic<int> launches{0};
    std::atomic<std::uint32_t> last_pid{0}, last_session{0};
    std::atomic<bool> blocked{false}, fail{false};
    bool installation_blocked() override { return blocked; }
    std::filesystem::path registered_settings_path() override { return L"C:/artificial/coralpinyin.exe"; }
    bool is_regular_file(const std::filesystem::path&) override { return true; }
    bool already_running(const std::filesystem::path&, std::uint32_t) override { return false; }
    ProcessAttempt launch_process(const std::filesystem::path&, std::uint32_t session, LaunchTarget) override {
        const auto peer = coral::ipc::current_pipe_peer();
        require(peer && peer->session_id == session, "kernel peer present during actual callback");
        last_pid = peer->process_id; last_session = session; ++launches;
        return { !fail, fail ? ERROR_PRIVILEGE_NOT_HELD : 0U };
    }
};
void call(const std::wstring& name, const wchar_t* method, const std::vector<std::wstring>& params, HRESULT expected, const wchar_t* status) {
    CIpcClient client(name.c_str());
    for (const auto& param : params) client.Add(param.c_str());
    require(client.Call(method) == expected && client.Get(L"status") == status, "RPC status and HRESULT");
}
void child(const std::wstring& name) {
    require(!coral::ipc::current_pipe_peer(), "client thread has no server peer");
    call(name,L"LaunchEngineHost",{std::to_wstring(own_session())},S_OK,L"launched");
}
void run() {
    require(!coral::ipc::current_pipe_peer(), "peer empty outside callback");
    const auto session = own_session();
    const std::wstring name = L"coral_service_launch_test_" + std::to_wstring(::GetCurrentProcessId());
    Actions actions;
    LaunchRpcServer server(actions);
    require(server.start(name.c_str()) == S_OK && server.start(name.c_str()) == S_FALSE, "owned RPC start");
    const auto param = std::to_wstring(session);
    for (const auto method : {L"LaunchMainProcess",L"LaunchEngineHost",L"LaunchRenderer"})
        call(name,method,{param},S_OK,L"launched");
    require(actions.launches == 3 && actions.last_pid == ::GetCurrentProcessId() && actions.last_session == session, "actual same-process identity");
    for (const auto params : {std::vector<std::wstring>{},std::vector<std::wstring>{L""},std::vector<std::wstring>{L"1x"},std::vector<std::wstring>{param,L"extra"},std::vector<std::wstring>{L"0"}})
        call(name,L"LaunchMainProcess",params,E_INVALIDARG,L"invalid_argument");
    call(name,L"LaunchMainProcess",{std::to_wstring(session == 1 ? 2 : 1)},E_ACCESSDENIED,L"access_denied");
    require(actions.launches == 3, "malformed and forged session do not launch");
    actions.blocked = true;
    for (const auto method : {L"LaunchMainProcess",L"LaunchEngineHost",L"LaunchRenderer"}) call(name,method,{param},S_FALSE,L"installing");
    actions.blocked = false; actions.fail = true;
    call(name,L"LaunchRenderer",{param},HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD),L"launch_failed");
    actions.fail = false;

    wchar_t executable[32768]{};
    require(::GetModuleFileNameW(nullptr,executable,32768) != 0, "probe executable");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --child " + name;
    STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
    require(::CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process), "independent RPC client started");
    ::CloseHandle(process.hThread);
    const auto wait = ::WaitForSingleObject(process.hProcess,10000);
    DWORD code = 1; ::GetExitCodeProcess(process.hProcess,&code);
    ::CloseHandle(process.hProcess);
    require(wait == WAIT_OBJECT_0 && code == 0, "independent RPC client finishes");
    require(actions.last_pid == process.dwProcessId && actions.last_pid != ::GetCurrentProcessId() && actions.last_session == session, "independent process authenticated from kernel");
    require(!coral::ipc::current_pipe_peer(), "server identity never leaks to owner thread");
    server.stop();
    require(server.start(name.c_str()) == S_OK, "RPC can restart after worker drain");
    call(name,L"LaunchMainProcess",{param},S_OK,L"launched");
    server.stop();
}
}
int wmain(int argc, wchar_t** argv) {
    try { if (argc == 3 && std::wstring(argv[1]) == L"--child") child(argv[2]); else run(); std::cout << "service RPC passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

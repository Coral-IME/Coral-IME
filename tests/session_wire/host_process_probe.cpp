#include "pipe_session_client.h"
#include "session_endpoint.h"
#include "session_bridge.h"
#include "windows_host_bootstrap.h"
#include "coral/transport/session_wire.h"
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace coral::transport;
using coral::composition::SessionStatus;
constexpr std::uint64_t kClientId = 1234567;
HANDLE open_idle_pipe();

void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

SessionReply frame_call(std::vector<std::uint8_t> frame)
{
    std::vector<std::uint8_t> output(kEnvelopeBytes + kMaxPayloadBytes);
    DWORD bytes = 0;
    require(::CallNamedPipeW(default_session_pipe().c_str(), frame.data(), static_cast<DWORD>(frame.size()),
        output.data(), static_cast<DWORD>(output.size()), &bytes, 2000) != FALSE, "raw pipe transaction");
    SessionReply reply;
    std::string error;
    require(decode_reply(output.data(), bytes, reply, error), "decode real host reply");
    return reply;
}

SessionReply call(SessionRequest request)
{
    static std::uint64_t next_request = 1;
    if (!request.request_id) request.request_id = next_request++;
    if (!request.client_id) request.client_id = kClientId;
    if (!request.deadline_tick_ms) request.deadline_tick_ms = ::GetTickCount64() + 2000;
    std::vector<std::uint8_t> frame;
    std::string error;
    require(encode_request(request, frame, error), "encode raw request");
    const auto reply = frame_call(std::move(frame));
    require(reply.operation == request.operation && reply.client_id == request.client_id &&
        reply.engine.request_id == request.request_id, "real response correlation");
    return reply;
}

std::uint64_t hello()
{
    SessionRequest request;
    request.operation = SessionOperation::kHello;
    const auto result = call(request);
    require(result.engine.status == SessionStatus::kOk && result.engine.server_generation != 0, "real host hello");
    return result.engine.server_generation;
}

SessionRequest request_for(SessionOperation operation, std::uint64_t generation, std::uint64_t token = 0, std::uint64_t client = kClientId)
{
    SessionRequest request;
    request.operation = operation;
    request.server_generation = generation;
    request.client_id = client;
    request.session_id = token;
    request.session_generation = token ? 1 : 0;
    return request;
}

void query(std::uint64_t generation, std::uint64_t token)
{
    auto request = request_for(SessionOperation::kParseFull, generation, token);
    request.pinyin = "nihao"; request.page_size = 7;
    const auto response = call(request);
    require(response.engine.status == SessionStatus::kOk && !response.engine.output.candidates.empty(), "real model query");
    require(response.engine.output.candidates.candidates.front().word == u8"你好", "real demo candidate");
}

void basic()
{
    auto first = make_pipe_session_client(default_session_pipe(), 1000);
    auto second = make_pipe_session_client(default_session_pipe(), 1000);
    require(first->available(), "real client availability");
    const auto a = first->open_session(), b = second->open_session();
    require(a && b && a != b, "distinct remote tokens");
    first->close_session(0); // 公开的零令牌关闭会保留普通会话。
    coral::composition::SessionSettings settings;
    settings.pinyin_correction = false;
    require(static_cast<bool>(first->apply_settings(a, 500, settings)), "first settings");
    settings.traditional_chinese = true;
    require(static_cast<bool>(second->apply_settings(b, 501, settings)), "second settings");
    const auto simplified = first->query_page(a, 502, "", "zhongguo", 0, 7, 0);
    const auto traditional = second->query_page(b, 503, "", "zhongguo", 0, 7, 0);
    require(simplified && traditional && !simplified.output.candidates.empty() && !traditional.output.candidates.empty(), "real two-client query");
    require(simplified.output.candidates.candidates.front().word == u8"中国" &&
        traditional.output.candidates.candidates.front().word == u8"中國", "remote settings isolation");
    first->close_session(a); second->close_session(b);

    const auto generation = hello();
    const auto opened = call(request_for(SessionOperation::kOpenSession, generation));
    const auto token = opened.engine.session_id;
    require(token != 0, "raw session open");
    auto wrong_client = request_for(SessionOperation::kResetSession, generation, token, kClientId + 1);
    require(call(wrong_client).engine.status == SessionStatus::kInvalidSession, "foreign client refused");
    wrong_client.operation = SessionOperation::kCloseSession;
    require(call(wrong_client).engine.status == SessionStatus::kInvalidSession, "foreign close refused");
    auto wrong_generation = request_for(SessionOperation::kResetSession, generation, token);
    wrong_generation.session_generation = 2;
    require(call(wrong_generation).engine.status == SessionStatus::kInvalidSession, "wrong session generation refused");
    wrong_generation.session_generation = 1; wrong_generation.server_generation ^= 1;
    require(call(wrong_generation).engine.status == SessionStatus::kInvalidSession, "wrong host generation refused");
    auto expired = request_for(SessionOperation::kResetSession, generation, token);
    expired.deadline_tick_ms = ::GetTickCount64() - 1;
    require(call(expired).engine.status == SessionStatus::kTimeout, "expired request refused");
    auto malformed_control = request_for(SessionOperation::kOpenSession, generation, token);
    require(call(malformed_control).engine.status == SessionStatus::kProtocolError, "session on control refused");
    query(generation, token);
    require(call(request_for(SessionOperation::kCloseSession, generation, token)).engine.status == SessionStatus::kOk, "owner close succeeds");
    require(call(request_for(SessionOperation::kResetSession, generation, token)).engine.status == SessionStatus::kInvalidSession, "closed token invalid");
    require(frame_call({1, 2, 3}).engine.status == SessionStatus::kProtocolError, "malformed frame rejected");
    require(first->available(), "host remains usable after rejected requests");
    for (int index = 0; index < 32; ++index) ::CloseHandle(open_idle_pipe());
    require(first->available(), "rapid disconnects do not stop the host");
}

void capacity()
{
    const auto generation = hello();
    std::vector<std::pair<std::uint64_t, std::uint64_t>> sessions;
    for (std::uint64_t client = kClientId; client < kClientId + 4; ++client)
    {
        for (int index = 0; index < 64; ++index)
        {
            const auto result = call(request_for(SessionOperation::kOpenSession, generation, 0, client));
            require(result.engine.status == SessionStatus::kOk && result.engine.session_id, "session capacity available after dead-client reaping");
            sessions.emplace_back(client, result.engine.session_id);
        }
        require(call(request_for(SessionOperation::kOpenSession, generation, 0, client)).engine.status == SessionStatus::kUnavailable,
            "per-client capacity enforced");
    }
    require(call(request_for(SessionOperation::kOpenSession, generation, 0, kClientId + 4)).engine.status == SessionStatus::kUnavailable,
        "global capacity enforced");
    const auto freed = sessions.back(); sessions.pop_back();
    call(request_for(SessionOperation::kCloseSession, generation, freed.second, freed.first));
    const auto reused = call(request_for(SessionOperation::kOpenSession, generation, 0, kClientId + 4));
    require(reused.engine.status == SessionStatus::kOk && reused.engine.session_id != freed.second, "capacity released with fresh token");
    sessions.emplace_back(kClientId + 4, reused.engine.session_id);
    for (const auto& session : sessions) call(request_for(SessionOperation::kCloseSession, generation, session.second, session.first));
}

void client_release()
{
    for (int round = 0; round < 6; ++round)
    {
        auto port = make_pipe_session_client(default_session_pipe(), 1000);
        for (int index = 0; index < 64; ++index)
            require(port->open_session() != 0, "replacement port can still open its full capacity");
        // 没有显式关闭也没有进程退出：由析构函数释放该 CID。
        port.reset();
    }
    capacity(); // 本进程内所有 256 个槽位必须保持可用。
}

void hold_owner()
{
    const auto generation = hello();
    std::uint64_t first = 0;
    for (int i = 0; i < 64; ++i)
    {
        const auto result = call(request_for(SessionOperation::kOpenSession, generation));
        require(result.engine.status == SessionStatus::kOk && result.engine.session_id, "owner session opened");
        if (!first) first = result.engine.session_id;
    }
    std::cout << generation << ' ' << first << ' ' << kClientId << std::endl;
    std::string line; std::getline(std::cin, line);
    query(generation, first); // 外部关闭不得改变此所有者。
    // 刻意以正常进程退出且不显式关闭：测试回收逻辑。
}

void attack(std::uint64_t generation, std::uint64_t token, std::uint64_t client)
{
    require(call(request_for(SessionOperation::kResetSession, generation, token, client)).engine.status == SessionStatus::kInvalidSession,
        "same client ID from another process refused");
    require(call(request_for(SessionOperation::kCloseSession, generation, token, client)).engine.status == SessionStatus::kInvalidSession,
        "cross-process owner close refused");
    require(call(request_for(SessionOperation::kCloseSession, generation, 0, client)).engine.status == SessionStatus::kOk,
        "cross-process bulk release is scoped to its own process");
}

HANDLE open_idle_pipe()
{
    const auto name = default_session_pipe();
    const auto deadline = ::GetTickCount64() + 2000;
    do
    {
        const HANDLE pipe = ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) return pipe;
        ::Sleep(2);
    } while (::GetTickCount64() < deadline);
    throw std::runtime_error("Cannot open idle pipe");
}

void idle(const std::string& mode)
{
    const auto generation = hello();
    std::vector<HANDLE> pipes;
    for (int i = 0; i < 8; ++i)
    {
        const auto pipe = open_idle_pipe();
        pipes.push_back(pipe);
        if (mode == "unread")
        {
            auto request = request_for(SessionOperation::kPing, generation);
            request.request_id = 900 + i;
            std::vector<std::uint8_t> bytes;
            std::string error;
            require(encode_request(request, bytes, error), "encode unread transaction");
            DWORD written = 0;
            require(::WriteFile(pipe, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size(), "unread request written");
        }
    }
    if (mode == "timeout")
    {
        ::Sleep(2300);
        require(make_pipe_session_client(default_session_pipe(), 1000)->available(), "idle connections expire and release all worker slots");
    }
    else
    {
        std::cout << "READY" << std::endl;
        std::string line; std::getline(std::cin, line);
    }
    for (const auto pipe : pipes) ::CloseHandle(pipe);
}

void reconnect()
{
    auto port = make_pipe_session_client(default_session_pipe(), 1000);
    const auto old = port->open_session();
    require(old != 0, "initial reconnect session");
    std::cout << "READY" << std::endl;
    std::string line; std::getline(std::cin, line);
    require(port->query_page(old, 700, "", "nihao", 0, 7, 0).status == SessionStatus::kInvalidSession, "old request refused by replacement host");
    require(port->available(), "same client re-handshakes after direct host replacement");
    require(port->query_page(old, 701, "", "nihao", 0, 7, 0).status == SessionStatus::kInvalidSession, "old token stays invalid after handshake");
    const auto current = port->open_session();
    require(current && current != old, "replacement has fresh remote token");
    const auto result = port->query_page(current, 702, "", "nihao", 0, 7, 0);
    require(result && !result.output.candidates.empty(), "replacement query works");
    port->close_session(current);
}

void bridge_reconnect()
{
    using namespace coral::text_service;
    int connections = 0;
    {
        SessionBridge bridge([&](std::uint32_t) {
            ++connections;
            return make_pipe_session_client(default_session_pipe(), 1000);
        });
        CandidateLookup simplified;
        simplified.session_id = bridge.create_session();
        simplified.request_id = 91; simplified.pinyin = "zhongguo";
        CandidateLookup traditional = simplified;
        traditional.session_id = bridge.create_session(); traditional.request_id = 92;
        require(simplified.session_id && traditional.session_id && simplified.session_id != traditional.session_id, "bridge logical sessions");
        coral::composition::SessionSettings config; config.revision = 4;
        require(bridge.apply_settings(simplified.session_id, config) == S_OK, "bridge simplified settings");
        config.traditional_chinese = true;
        require(bridge.apply_settings(traditional.session_id, config) == S_OK, "bridge traditional settings");
        config.revision = 5;
        require(bridge.apply_settings(traditional.session_id, config) == S_OK, "bridge equal-value revision advances");
        CandidateSnapshot snapshot;
        const auto verify = [&] {
            require(bridge.query(simplified, snapshot) == S_OK && !snapshot.words.empty() && snapshot.words.front() == u8"中国", "bridge simplified query");
            require(bridge.query(traditional, snapshot) == S_OK && !snapshot.words.empty() && snapshot.words.front() == u8"中國" && snapshot.request_id == 92, "bridge traditional query and UI identity");
        };
        verify();
        std::cout << "READY" << std::endl;
        std::string line; std::getline(std::cin, line);
        verify();
        require(connections == 2, "one reconnect after actual host replacement");
        config.revision = 4; config.traditional_chinese = false;
        require(bridge.apply_settings(traditional.session_id, config) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "stale revision cannot overwrite restored configuration");
        verify();
        require(connections == 2, "stale configuration does not reconnect");
        bridge.destroy_session(traditional.session_id);
        require(bridge.query(traditional, snapshot) == HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE), "closed logical session cannot resurrect");
        // 让简化会话保持存活，以检查对象持有的端口清理。
    }
    capacity();
}

void cold_bootstrap()
{
    coral::bootstrap::HostLaunchHooks hooks;
    hooks.install_directory = [] { throw std::runtime_error("Adjacent host must avoid registry lookup"); return std::filesystem::path{}; };
    hooks.request_service_launch = [] { throw std::runtime_error("Desktop test must avoid installed service"); return false; };
    hooks.installation_blocked = [] { return true; };
    require(coral::bootstrap::start_session_host(hooks,1000).status == coral::bootstrap::StartupStatus::kInstallationBlocked,
        "native install gate does not start the host");
    hooks.installation_blocked = [] { return false; };
    const auto result = coral::bootstrap::start_session_host(hooks,10000);
    // 启动超时可能遗留新进程：每次失败都要触发其隔离停止事件。
    // 绝不触及已安装的宿主或服务。
    const auto stop_event = ::OpenEventW(EVENT_MODIFY_STATE,FALSE,session_host_stop_event().c_str());
    HANDLE host_process = nullptr;
    try
    {
        require(static_cast<bool>(result) && result.direct_attempted && !result.service_attempted, "native cold startup via adjacent executable");
        const auto pipe = open_idle_pipe();
        ULONG server_pid = 0;
        const bool identified = ::GetNamedPipeServerProcessId(pipe,&server_pid) != FALSE;
        ::CloseHandle(pipe);
        require(identified && server_pid, "identify isolated host process");
        host_process = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,server_pid);
        require(host_process != nullptr, "hold actual host exit handle");
        wchar_t image[32768]{}; DWORD size = 32768;
        require(::QueryFullProcessImageNameW(host_process,0,image,&size) &&
            std::filesystem::path(image).filename() == coral::product::kSessionHostExeWide, "verify isolated server executable");
        hooks.installation_blocked = [] { throw std::runtime_error("Ready host must bypass installation/launch checks"); return true; };
        const auto warm = coral::bootstrap::start_session_host(hooks,0);
        require(static_cast<bool>(warm) && !warm.direct_attempted && !warm.service_attempted, "native zero-budget warm readiness");
        basic();
        require(stop_event && ::SetEvent(stop_event), "normal isolated host stop after native startup");
        require(::WaitForSingleObject(host_process,5000) == WAIT_OBJECT_0, "native-started host exits normally");
        DWORD exit_code = 0;
        require(::GetExitCodeProcess(host_process,&exit_code) && exit_code == 0, "native-started host successful exit");
    }
    catch (...)
    {
        if (stop_event) ::SetEvent(stop_event);
        if (host_process) { ::WaitForSingleObject(host_process,5000); ::CloseHandle(host_process); }
        if (stop_event) ::CloseHandle(stop_event);
        throw;
    }
    ::CloseHandle(host_process);
    ::CloseHandle(stop_event);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || !std::getenv("CORAL_TEST_NAMESPACE") || !std::getenv("CORAL_USER_DATA_DIR")) return 2;
    try
    {
        const std::string mode = argv[1];
        if (mode == "stop")
        {
            const auto event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, coral::transport::session_host_stop_event().c_str());
            require(event != nullptr, "open isolated stop event");
            const auto stopped = ::SetEvent(event); ::CloseHandle(event);
            require(stopped != FALSE, "signal isolated stop event");
        }
        else if (mode == "ping") require(make_pipe_session_client(default_session_pipe(), 1000)->available(), "host startup readiness");
        else if (mode == "basic") basic();
        else if (mode == "capacity") capacity();
        else if (mode == "release") client_release();
        else if (mode == "owner") hold_owner();
        else if (mode == "attack" && argc == 5) attack(std::stoull(argv[2]), std::stoull(argv[3]), std::stoull(argv[4]));
        else if (mode == "idle" || mode == "unread" || mode == "timeout") idle(mode);
        else if (mode == "reconnect") reconnect();
        else if (mode == "bridge-reconnect") bridge_reconnect();
        else if (mode == "bootstrap") cold_bootstrap();
        else throw std::runtime_error("Unknown probe mode");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

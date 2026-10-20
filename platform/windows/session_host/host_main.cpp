#include "coral/transport/session_wire.h"
#include "session_endpoint.h"
#include "pipe_session_client.h"
#include "crash_reporter.h"
#include "text_file_helpers.hpp"
#include "coral/composition/session_port.h"
#include "src/platform/windows/common/search_package_sid.h"
#include "src/platform/windows/common/input_timing.h"

#include <windows.h>
#include <sddl.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace coral::runtime {
namespace {
using composition::SessionPort;
using composition::SessionStatus;
using transport::SessionOperation;
using transport::SessionReply;
using transport::SessionRequest;
constexpr DWORD kWorkerCount = 8;
constexpr DWORD kConnectionTimeoutMs = 2000;
constexpr std::size_t kMaxSessions = 256;
constexpr std::size_t kMaxClientSessions = 64;

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE value = nullptr) : value_(value) {}
    ~UniqueHandle() { if (*this) ::CloseHandle(value_); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE value_;
};

struct LocalMemory {
    HLOCAL value = nullptr;
    ~LocalMemory() { if (value) ::LocalFree(value); }
    LocalMemory() = default;
    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;
};

std::mutex console_mutex;
HANDLE console_stop_event = nullptr;
BOOL WINAPI on_console_control(DWORD)
{
    std::lock_guard<std::mutex> lock(console_mutex);
    if (console_stop_event) ::SetEvent(console_stop_event);
    return TRUE;
}

class ConsoleStopBinding {
public:
    explicit ConsoleStopBinding(HANDLE event)
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        console_stop_event = event;
        ::SetConsoleCtrlHandler(on_console_control, TRUE);
    }
    ~ConsoleStopBinding()
    {
        ::SetConsoleCtrlHandler(on_console_control, FALSE);
        std::lock_guard<std::mutex> lock(console_mutex);
        console_stop_event = nullptr;
    }
};

bool build_security(SECURITY_ATTRIBUTES& attributes, LocalMemory& descriptor, bool pipe_access)
{
    HANDLE raw_token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &raw_token)) return false;
    UniqueHandle token(raw_token);
    DWORD bytes = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
    std::vector<std::uint8_t> buffer(bytes);
    if (!::GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes)) return false;
    auto* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
    LPWSTR sid_text = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &sid_text)) return false;
    LocalMemory sid_owner;
    sid_owner.value = sid_text;
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + std::wstring(sid_text) + L")";
    if (pipe_access)
    {
        // 保留受限 AppContainer 与搜索包的访问路径。
        sddl += L"(A;;GRGW;;;AC)";
        for (const auto& sid : coral::win32::SearchPackageSids()) sddl += L"(A;;GRGW;;;" + sid + L")";
        sddl += L"S:(ML;;NW;;;LW)";
    }
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
        reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value), nullptr)) return false;
    attributes = {sizeof(attributes), descriptor.value, FALSE};
    return true;
}

struct RequestKey {
    DWORD process_id;
    std::uint64_t client_id;
    std::uint64_t request_id;
    bool operator==(const RequestKey& other) const
    {
        return process_id == other.process_id && client_id == other.client_id && request_id == other.request_id;
    }
};
struct RequestKeyHash {
    std::size_t operator()(const RequestKey& key) const
    {
        return std::hash<std::uint64_t>{}(key.client_id) ^
            (std::hash<std::uint64_t>{}(key.request_id) << 1) ^ std::hash<DWORD>{}(key.process_id);
    }
};

class HostRouter {
public:
    HostRouter(SessionPort& port, std::uint64_t generation)
        : port_(port), generation_(generation), next_session_((generation & 0x7fffffffffffffffULL) + 1) {}
    ~HostRouter() { close_all(); }

    SessionReply reply(const SessionRequest& request, SessionStatus status = SessionStatus::kOk, const char* message = "") const
    {
        SessionReply result;
        result.operation = request.operation;
        result.client_id = request.client_id;
        result.engine.request_id = request.request_id;
        result.engine.session_id = request.session_id;
        result.engine.session_generation = request.session_generation;
        result.engine.server_generation = generation_;
        result.engine.status = status;
        result.engine.result = status == SessionStatus::kOk;
        result.engine.message = message;
        return result;
    }

    SessionReply route(const SessionRequest& request, DWORD process_id)
    {
        if (!request.client_id || !request.request_id || !process_id)
            return reply(request, SessionStatus::kProtocolError, "client and request identities are required");
        if (request.operation != SessionOperation::kHello && request.server_generation != generation_)
            return reply(request, SessionStatus::kInvalidSession, "engine host generation changed");
        if (transport::deadline_expired(request.deadline_tick_ms, ::GetTickCount64()))
            return reply(request, SessionStatus::kTimeout, "request deadline expired");
        const RequestKey key{process_id, request.client_id, request.request_id};
        if (request.operation == SessionOperation::kCancel)
        {
            if (request.session_id || request.session_generation)
                return reply(request, SessionStatus::kProtocolError, "control request carries a session");
            std::lock_guard<std::mutex> lock(active_mutex_);
            const auto found = active_.find({process_id, request.client_id, request.cancel_request_id});
            if (found != active_.end()) found->second = true;
            return reply(request);
        }
        {
            std::lock_guard<std::mutex> lock(active_mutex_);
            if (!active_.emplace(key, false).second)
                return reply(request, SessionStatus::kProtocolError, "request is already active");
        }
        // 始终移除活动条目，包括分配/算法异常的情况。
        struct ActiveLease {
            HostRouter& router;
            RequestKey key;
            ~ActiveLease() { std::lock_guard<std::mutex> lock(router.active_mutex_); router.active_.erase(key); }
        } lease{*this, key};
        std::lock_guard<std::mutex> lock(session_mutex_);
        reap_locked();
        if (cancelled(key) || transport::deadline_expired(request.deadline_tick_ms, ::GetTickCount64()))
            return reply(request, SessionStatus::kTimeout, "request expired before execution");
        auto result = reply(request);
        try { result = dispatch_locked(request, process_id); }
        catch (...) { result = reply(request, SessionStatus::kInternalError, "engine operation failed"); }
        result.client_id = request.client_id;
        result.engine.server_generation = generation_;
        if (cancelled(key) || transport::deadline_expired(request.deadline_tick_ms, ::GetTickCount64()))
        {
            // 打开动作没有学习效果；回收未上报的会话。
            if (request.operation == SessionOperation::kOpenSession && result.engine.session_id)
                erase_locked(result.engine.session_id);
            return reply(request, SessionStatus::kTimeout, "late result discarded; effects may have occurred");
        }
        return result;
    }

    void reap()
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        reap_locked();
    }
    void close_all()
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        for (const auto& session : sessions_) port_.close_session(session.second.local_id);
        sessions_.clear();
    }
private:
    struct SessionLease {
        composition::SessionToken local_id;
        DWORD process_id;
        std::uint64_t client_id;
        UniqueHandle process;
    };
    bool cancelled(const RequestKey& key)
    {
        std::lock_guard<std::mutex> lock(active_mutex_);
        return active_.at(key);
    }
    void erase_locked(composition::SessionToken token)
    {
        const auto found = sessions_.find(token);
        if (found == sessions_.end()) return;
        port_.close_session(found->second.local_id);
        sessions_.erase(found);
    }
    void reap_locked()
    {
        for (auto it = sessions_.begin(); it != sessions_.end();)
        {
            if (::WaitForSingleObject(it->second.process.get(), 0) != WAIT_TIMEOUT)
            {
                port_.close_session(it->second.local_id);
                it = sessions_.erase(it);
            }
            else ++it;
        }
    }
    SessionReply dispatch_locked(const SessionRequest& request, DWORD process_id)
    {
        auto response = reply(request);
        const auto operation = request.operation;
        // 令牌零是客户端租约释放控制。替换端口
        // 不得让其旧会话一直存活到整个进程退出。
        if (operation == SessionOperation::kCloseSession && request.session_id == 0)
        {
            if (request.session_generation != 0)
                return reply(request, SessionStatus::kProtocolError, "client release carries a session generation");
            for (auto it = sessions_.begin(); it != sessions_.end();)
            {
                if (it->second.process_id == process_id && it->second.client_id == request.client_id)
                {
                    port_.close_session(it->second.local_id);
                    it = sessions_.erase(it);
                }
                else ++it;
            }
            return response;
        }
        const bool control = operation == SessionOperation::kHello || operation == SessionOperation::kPing ||
            operation == SessionOperation::kOpenSession || operation == SessionOperation::kFlushUserHistory;
        if (control)
        {
            if (request.session_id || request.session_generation)
                return reply(request, SessionStatus::kProtocolError, "control request carries a session");
            if (operation == SessionOperation::kHello || operation == SessionOperation::kPing) return response;
            if (operation == SessionOperation::kFlushUserHistory)
            {
                if (!port_.flush_history()) return reply(request, SessionStatus::kInternalError, "history flush failed");
                return response;
            }
            const auto owned_count = std::count_if(sessions_.begin(), sessions_.end(), [&](const auto& session) {
                return session.second.process_id == process_id && session.second.client_id == request.client_id;
            });
            if (!next_session_ || sessions_.size() >= kMaxSessions || owned_count >= kMaxClientSessions)
                return reply(request, SessionStatus::kUnavailable, "host session capacity reached");
            UniqueHandle process(::OpenProcess(SYNCHRONIZE, FALSE, process_id));
            if (!process || ::WaitForSingleObject(process.get(), 0) != WAIT_TIMEOUT)
                return reply(request, SessionStatus::kUnavailable, "client process is unavailable");
            const auto local_id = port_.open_session();
            if (!local_id) return reply(request, SessionStatus::kUnavailable, "local core is unavailable");
            const auto token = next_session_;
            try { sessions_.emplace(token, SessionLease{local_id, process_id, request.client_id, std::move(process)}); }
            catch (...) { port_.close_session(local_id); throw; }
            next_session_ = token == std::numeric_limits<std::uint64_t>::max() ? 0 : token + 1;
            response.engine.session_id = token;
            response.engine.session_generation = 1;
            return response;
        }
        const auto found = sessions_.find(request.session_id);
        if (found == sessions_.end() || found->second.process_id != process_id ||
            found->second.client_id != request.client_id || request.session_generation != 1)
            return reply(request, SessionStatus::kInvalidSession, "session owner or generation does not match");
        const auto local_id = found->second.local_id;
        switch (operation)
        {
        case SessionOperation::kCloseSession: erase_locked(request.session_id); return response;
        case SessionOperation::kSendKey: response.engine = port_.send_key(local_id, request.request_id, request.key_event, request.input_context); break;
        case SessionOperation::kParseFull: response.engine = port_.query_page(local_id, request.request_id, request.condition, request.pinyin, request.offset, request.page_size, request.mode); break;
        case SessionOperation::kSelectCandidate: response.engine = port_.select_candidate(local_id, request.request_id, request.candidate_index); break;
        case SessionOperation::kResetSession: response.engine = port_.reset_session(local_id, request.request_id); break;
        case SessionOperation::kRecordSelected: response.engine = port_.record_selection(local_id, request.request_id, request.pinyin, request.word, request.condition_id); break;
        case SessionOperation::kRevertLastAction: response.engine = port_.revert_selection(local_id, request.request_id, request.word); break;
        case SessionOperation::kApplyCandidateAction: response.engine = port_.apply_candidate_action(local_id, request.request_id, request.pinyin, request.word, request.candidate_action, request.variant); break;
        case SessionOperation::kClearCache: response.engine = port_.clear_cache(local_id, request.request_id); break;
        case SessionOperation::kUpdateLexicon: response.engine = port_.refresh_lexicon(local_id, request.request_id); break;
        case SessionOperation::kUpdateConfig: response.engine = port_.apply_settings(local_id, request.request_id, request.config); break;
        default: return reply(request, SessionStatus::kProtocolError, "unsupported operation");
        }
        response.engine.session_id = request.session_id;
        response.engine.session_generation = request.session_generation;
        return response;
    }
    SessionPort& port_;
    const std::uint64_t generation_;
    std::uint64_t next_session_;
    std::mutex session_mutex_;
    std::unordered_map<composition::SessionToken, SessionLease> sessions_;
    std::mutex active_mutex_;
    std::unordered_map<RequestKey, bool, RequestKeyHash> active_;
};

// 每个未完成的 OVERLAPPED 及其缓冲区在完成前保持存活，
// 包括取消路径。不使用同步管道刷新。
template<class Start>
bool run_io(HANDLE pipe, HANDLE stop, DWORD timeout, DWORD& bytes, Start start,
    DWORD immediate_success = ERROR_SUCCESS, DWORD* io_error = nullptr)
{
    const auto failed = [&](DWORD error) { if (io_error) *io_error = error; return false; };
    if (io_error) *io_error = ERROR_SUCCESS;
    UniqueHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) throw std::runtime_error("Cannot create pipe I/O event");
    OVERLAPPED operation = {};
    operation.hEvent = event.get();
    bytes = 0;
    if (::WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return failed(ERROR_OPERATION_ABORTED);
    if (start(&operation, &bytes)) return true;
    const DWORD error = ::GetLastError();
    if (error == immediate_success) return true;
    if (error != ERROR_IO_PENDING) return failed(error);
    const HANDLE events[] = {stop, event.get()};
    if (::WaitForMultipleObjects(2, events, FALSE, timeout) == WAIT_OBJECT_0 + 1)
    {
        if (::GetOverlappedResult(pipe, &operation, &bytes, FALSE)) return true;
        return failed(::GetLastError());
    }
    ::CancelIoEx(pipe, &operation);
    ::GetOverlappedResult(pipe, &operation, &bytes, TRUE);
    return failed(ERROR_OPERATION_ABORTED);
}

void serve_connection(HANDLE pipe, HANDLE stop, HostRouter& router)
{
    ULONG process_id = 0;
    if (!::GetNamedPipeClientProcessId(pipe, &process_id)) return;
    DWORD peer_session = 0, host_session = 0;
    if (!::ProcessIdToSessionId(process_id, &peer_session) ||
        !::ProcessIdToSessionId(::GetCurrentProcessId(), &host_session) || peer_session != host_session) return;
    std::vector<std::uint8_t> input(transport::kEnvelopeBytes + transport::kMaxPayloadBytes);
    DWORD bytes = 0;
    if (!run_io(pipe, stop, kConnectionTimeoutMs, bytes, [&](OVERLAPPED* operation, DWORD* count) {
        return ::ReadFile(pipe, input.data(), static_cast<DWORD>(input.size()), count, operation);
    })) return;
    if (::WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return;
    SessionRequest request;
    std::string error;
    auto response = router.reply(request, SessionStatus::kProtocolError);
    if (!transport::decode_request(input.data(), bytes, request, error))
        response = router.reply(request, SessionStatus::kProtocolError, error.c_str());
    else
    {
        const auto started = coral::input_timing::nowUs();
        response = router.route(request, process_id);
        coral::input_timing::mark(L"host.dispatch", started, request.request_id, static_cast<unsigned>(request.operation));
    }
    std::vector<std::uint8_t> output;
    if (!transport::encode_reply(response, output, error)) return;
    if (!run_io(pipe, stop, kConnectionTimeoutMs, bytes, [&](OVERLAPPED* operation, DWORD* count) {
        return ::WriteFile(pipe, output.data(), static_cast<DWORD>(output.size()), count, operation);
    }) || bytes != output.size()) return;
    // 每个连接一个事务。等待客户端关闭，避免断连丢弃未读回复；
    // 停滞的对端最多占用此槽位 2 秒。
    std::uint8_t extra = 0;
    run_io(pipe, stop, kConnectionTimeoutMs, bytes, [&](OVERLAPPED* operation, DWORD* count) {
        return ::ReadFile(pipe, &extra, 1, count, operation);
    });
}

void worker_loop(HANDLE pipe, HANDLE stop, HostRouter& router, std::atomic<bool>& failure)
{
    try
    {
        while (::WaitForSingleObject(stop, 0) == WAIT_TIMEOUT)
        {
            DWORD bytes = 0;
            DWORD connect_error = ERROR_SUCCESS;
            const bool connected = run_io(pipe, stop, INFINITE, bytes, [&](OVERLAPPED* operation, DWORD*) {
                return ::ConnectNamedPipe(pipe, operation);
            }, ERROR_PIPE_CONNECTED, &connect_error);
            if (connected) serve_connection(pipe, stop, router);
            ::DisconnectNamedPipe(pipe);
            if (!connected && ::WaitForSingleObject(stop, 0) == WAIT_TIMEOUT &&
                connect_error != ERROR_NO_DATA && connect_error != ERROR_PIPE_NOT_CONNECTED)
                throw std::runtime_error("Pipe connection failed");
        }
    }
    catch (...)
    {
        failure.store(true);
        ::SetEvent(stop);
    }
}

class WorkerGroup {
public:
    explicit WorkerGroup(HANDLE stop) : stop_(stop) {}
    ~WorkerGroup() { ::SetEvent(stop_); for (auto& worker : workers_) if (worker.joinable()) worker.join(); }
    void add(HANDLE pipe, HostRouter& router, std::atomic<bool>& failure)
    {
        workers_.emplace_back(worker_loop, pipe, stop_, std::ref(router), std::ref(failure));
    }
private:
    HANDLE stop_;
    std::vector<std::thread> workers_;
};

int run_host()
{
    const auto startup = coral::input_timing::nowUs();
    // 在打开对象或用户数据之前，先解析并净化选择性隔离。
    const auto pipe_name = transport::default_session_pipe();
    const auto mutex_name = transport::session_host_mutex();
    const auto stop_name = transport::session_host_stop_event();
    SECURITY_ATTRIBUTES pipe_security = {}, control_security = {};
    LocalMemory pipe_descriptor, control_descriptor;
    if (!build_security(pipe_security, pipe_descriptor, true) || !build_security(control_security, control_descriptor, false)) return 3;
    UniqueHandle instance(::CreateMutexW(&control_security, FALSE, mutex_name.c_str()));
    if (!instance) return 3;
    const DWORD acquired = ::WaitForSingleObject(instance.get(), 0);
    if (acquired == WAIT_TIMEOUT) return 0;
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return 3;
    struct MutexLease { HANDLE value; ~MutexLease() { ::ReleaseMutex(value); } } instance_lease{instance.get()};
    UniqueHandle stop(::CreateEventW(&control_security, TRUE, FALSE, stop_name.c_str()));
    if (!stop) return 3;
    ::ResetEvent(stop.get());
    ConsoleStopBinding console(stop.get());
    std::uint64_t generation = 0;
    if (::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&generation), sizeof(generation), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || !generation) return 3;
    const auto init_started = coral::input_timing::nowUs();
    auto owner = composition::make_initialized_session_port(hlp::getLocalLowCoralPinyinPath().string(), true);
    coral::input_timing::mark(L"host.init_engine", init_started, 0, owner != nullptr);
    if (!owner) return 2;
    HostRouter router(*owner, generation);
    std::vector<UniqueHandle> pipes;
    for (DWORD slot = 0; slot < kWorkerCount; ++slot)
    {
        UniqueHandle pipe(::CreateNamedPipeW(pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
            (slot == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            kWorkerCount, transport::kEnvelopeBytes + transport::kMaxPayloadBytes,
            transport::kEnvelopeBytes + transport::kMaxPayloadBytes, 0, &pipe_security));
        if (!pipe) return 4;
        pipes.push_back(std::move(pipe));
    }
    std::atomic<bool> failure{false};
    {
        WorkerGroup workers(stop.get());
        for (const auto& pipe : pipes) workers.add(pipe.get(), router, failure);
        coral::input_timing::mark(L"host.pipe_ready", startup);
        DWORD wait = WAIT_TIMEOUT;
        while ((wait = ::WaitForSingleObject(stop.get(), 250)) == WAIT_TIMEOUT) router.reap();
        if (wait != WAIT_OBJECT_0) failure.store(true);
        // WorkerGroup 中断等待并汇合进行中的模型操作。
    }
    router.close_all();
    if (!owner->flush_history()) return 5;
    return failure.load() ? 4 : 0;
    // 路由器先于所有者消亡；所有者析构时停止并销毁核心。
}
} // namespace
} // 命名空间 coral::runtime 结束

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    coral::diagnostics::install_crash_reporter(coral::diagnostics::CrashModule::kSessionHost);
    try { return coral::runtime::run_host(); }
    catch (...) { return 4; }
}

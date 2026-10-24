#include "coral/product/product_identity.h"
#include "pipe_session_client.h"
#include "session_endpoint.h"

#include "coral/transport/session_wire.h"
#include "src/platform/windows/common/input_timing.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace coral::transport
{
namespace
{
composition::SessionStatus status_from_win32(DWORD error)
{
	switch (error)
	{
	case ERROR_SEM_TIMEOUT:
	case ERROR_TIMEOUT:
	case ERROR_PIPE_BUSY:
		return composition::SessionStatus::kTimeout;
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PIPE_NOT_CONNECTED:
	case ERROR_BROKEN_PIPE:
		return composition::SessionStatus::kUnavailable;
	default:
		return composition::SessionStatus::kInternalError;
	}
}

class PipeSessionClient final : public composition::SessionPort
{
public:
	PipeSessionClient(std::wstring pipe_name, std::uint32_t timeout_ms)
		: pipe_name_(std::move(pipe_name)), timeout_ms_(timeout_ms == 0 ? 1 : timeout_ms)
	{
        // 新对象可能在同一时钟刻内复用分配器地址。
        // 每个端口在替换与模块之间使用各自的 nonce。
        if (::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&client_id_), sizeof(client_id_),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || client_id_ == 0)
            throw std::runtime_error("Cannot create session client identity");
	}

	composition::SessionToken open_session() override
	{
		SessionRequest request = make_request(SessionOperation::kOpenSession, next_control_request(), 0);
		const composition::SessionResult response = call(request);
		if (!response || response.session_id == 0)
			return 0;
		std::lock_guard<std::mutex> lock(session_mutex_);
		session_generations_[response.session_id] = {response.session_generation, response.server_generation};
		return response.session_id;
	}

    ~PipeSessionClient() override
    {
        // 一次有界、尽力而为的释放，包括回复丢失的打开操作。
        // 保留原始宿主标识；绝不指向替换者。
        const auto generation = last_known_generation_.load(std::memory_order_acquire);
        if (!generation) return;
        try
        {
            SessionRequest release;
            release.operation = SessionOperation::kCloseSession;
            release.request_id = next_control_request();
            release.client_id = client_id_;
            release.server_generation = generation;
            release.deadline_tick_ms = ::GetTickCount64() + (std::min<std::uint32_t>)(timeout_ms_, 100);
            call_transport(release);
        }
        catch (...) {} // 析构不能失败；进程回收是兜底手段。
    }

	void close_session(composition::SessionToken session_id) override
	{
        if (!session_id) return; // 零令牌租约释放属于传输层内部机制。
		SessionRequest request = make_request(SessionOperation::kCloseSession, next_control_request(), session_id);
		call(request);
		std::lock_guard<std::mutex> lock(session_mutex_);
		session_generations_.erase(session_id);
	}

	composition::SessionResult send_key(
		composition::SessionToken session_id,
		std::uint64_t request_id,
		const composition::KeyEvent& event,
		const composition::InputContext& context) override
	{
		SessionRequest request = make_request(SessionOperation::kSendKey, request_id, session_id);
		request.key_event = event;
		request.input_context = context;
		return call(request);
	}

	composition::SessionResult query_page(
		composition::SessionToken session_id,
		std::uint64_t request_id,
		const std::string& condition,
		const std::string& pinyin,
		int offset,
		int pageSize,
		int mode) override
	{
		SessionRequest request = make_request(SessionOperation::kParseFull, request_id, session_id);
		request.condition = condition;
		request.pinyin = pinyin;
		request.offset = offset;
		request.page_size = pageSize;
		request.mode = mode;
		return call(request);
	}

    composition::SessionResult refine_candidates(composition::SessionToken session_id,
        std::uint64_t request_id, std::uint64_t query_request_id, bool cancel) override
    {
        auto request = make_request(cancel ? SessionOperation::kCancelRefinement : SessionOperation::kRefineCandidates,
            request_id, session_id);
        request.query_request_id = query_request_id;
        return call(request);
    }
	composition::SessionResult select_candidate(
		composition::SessionToken session_id, std::uint64_t request_id, int indexInPage) override
	{
		SessionRequest request = make_request(SessionOperation::kSelectCandidate, request_id, session_id);
		request.candidate_index = indexInPage;
		return call(request);
	}

	composition::SessionResult reset_session(composition::SessionToken session_id, std::uint64_t request_id) override
	{
		return call(make_request(SessionOperation::kResetSession, request_id, session_id));
	}

	composition::SessionResult record_selection(
		composition::SessionToken session_id, std::uint64_t request_id,
		const std::string& pinyin, const std::string& word,
		std::uint32_t conditionId) override
	{
		SessionRequest request = make_request(SessionOperation::kRecordSelected, request_id, session_id);
		request.pinyin = pinyin;
		request.word = word;
		request.condition_id = conditionId;
		return call(request);
	}

	composition::SessionResult revert_selection(
		composition::SessionToken session_id, std::uint64_t request_id, const std::string& word) override
	{
		SessionRequest request = make_request(SessionOperation::kRevertLastAction, request_id, session_id);
		request.word = word;
		return call(request);
	}

	composition::SessionResult apply_candidate_action(
		composition::SessionToken session_id, std::uint64_t request_id,
		const std::string& pinyin, const std::string& word,
		CandidateAction action, LanguageVariant variant) override
	{
		SessionRequest request = make_request(SessionOperation::kApplyCandidateAction, request_id, session_id);
		request.pinyin = pinyin;
		request.word = word;
        request.candidate_action = action;
        request.variant = variant;
		return call(request);
	}

	composition::SessionResult clear_cache(composition::SessionToken session_id, std::uint64_t request_id) override
	{
		return call(make_request(SessionOperation::kClearCache, request_id, session_id));
	}

	composition::SessionResult refresh_lexicon(composition::SessionToken session_id, std::uint64_t request_id) override
	{
		return call(make_request(SessionOperation::kUpdateLexicon, request_id, session_id));
	}

	composition::SessionResult apply_settings(
		composition::SessionToken session_id, std::uint64_t request_id,
		const composition::SessionSettings& config) override
	{
		SessionRequest request = make_request(SessionOperation::kUpdateConfig, request_id, session_id);
		request.config = config;
		return call(request);
	}

	bool flush_history() override
	{
		return static_cast<bool>(call(make_request(
			SessionOperation::kFlushUserHistory, next_control_request(), 0)));
	}

	bool available() const override
	{
		auto* self = const_cast<PipeSessionClient*>(this);
		if (!self->ensure_handshake())
			return false;
		return static_cast<bool>(self->call(
			make_request(SessionOperation::kPing, next_control_request(), 0)));
	}

private:
	bool ensure_handshake()
	{
		if (server_generation_.load(std::memory_order_acquire) != 0)
			return true;
		std::lock_guard<std::mutex> lock(handshake_mutex_);
		if (server_generation_.load(std::memory_order_relaxed) != 0)
			return true;
		SessionRequest hello;
		hello.operation = SessionOperation::kHello;
		hello.request_id = next_control_request();
		hello.client_id = client_id_;
		hello.deadline_tick_ms = ::GetTickCount64() + timeout_ms_;
		const composition::SessionResult response = call_transport(hello);
		if (!response || response.server_generation == 0)
			return false;
		{
            std::lock_guard<std::mutex> sessions_lock(session_mutex_);
            for (auto it = session_generations_.begin(); it != session_generations_.end();)
            {
                if (it->second.server_generation != response.server_generation) it = session_generations_.erase(it);
                else ++it;
            }
        }
        server_generation_.store(response.server_generation, std::memory_order_release);
        last_known_generation_.store(response.server_generation, std::memory_order_release);
		return true;
	}

	bool transact_with_deadline(
		const std::vector<std::uint8_t>& request,
		std::vector<std::uint8_t>& reply,
		std::uint64_t deadline_tick_ms,
		DWORD& reply_bytes,
		DWORD& win32_error) const
	{
		coral::input_timing::Scope timing(L"engine.pipe_transaction");
		reply_bytes = 0;
		win32_error = ERROR_SUCCESS;
		HANDLE pipe = INVALID_HANDLE_VALUE;
		const auto connect_start = coral::input_timing::nowUs();
		while (pipe == INVALID_HANDLE_VALUE)
		{
			pipe = ::CreateFileW(
				pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
				OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
			if (pipe != INVALID_HANDLE_VALUE)
				break;
			win32_error = ::GetLastError();
			const std::uint64_t now = ::GetTickCount64();
			if (now >= deadline_tick_ms)
			{
				win32_error = ERROR_SEM_TIMEOUT;
				return false;
			}
			const DWORD remaining = static_cast<DWORD>((std::min<std::uint64_t>)(deadline_tick_ms - now, MAXDWORD));
			if (win32_error == ERROR_FILE_NOT_FOUND)
			{
				::Sleep((std::min<DWORD>)(remaining, 2));
				continue;
			}
			if (win32_error != ERROR_PIPE_BUSY || !::WaitNamedPipeW(pipe_name_.c_str(), remaining))
			{
				if (win32_error == ERROR_PIPE_BUSY)
					win32_error = ::GetLastError();
				return false;
			}
		}

		coral::input_timing::mark(L"engine.pipe_connect", connect_start);
		DWORD mode = PIPE_READMODE_MESSAGE;
		if (!::SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
		{
			win32_error = ::GetLastError();
			::CloseHandle(pipe);
			return false;
		}

		OVERLAPPED operation = {};
		operation.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!operation.hEvent)
		{
			win32_error = ::GetLastError();
			::CloseHandle(pipe);
			return false;
		}

		BOOL completed = ::TransactNamedPipe(
			pipe,
			const_cast<std::uint8_t*>(request.data()), static_cast<DWORD>(request.size()),
			reply.data(), static_cast<DWORD>(reply.size()),
			&reply_bytes, &operation);
		if (!completed && ::GetLastError() == ERROR_IO_PENDING)
		{
			const std::uint64_t now = ::GetTickCount64();
			const DWORD remaining = now < deadline_tick_ms
				? static_cast<DWORD>((std::min<std::uint64_t>)(deadline_tick_ms - now, MAXDWORD))
				: 0;
			if (::WaitForSingleObject(operation.hEvent, remaining) == WAIT_OBJECT_0)
				completed = ::GetOverlappedResult(pipe, &operation, &reply_bytes, FALSE);
			else
			{
				::CancelIoEx(pipe, &operation);
				::GetOverlappedResult(pipe, &operation, &reply_bytes, TRUE);
				win32_error = ERROR_SEM_TIMEOUT;
				completed = FALSE;
			}
		}
		else if (!completed)
		{
			win32_error = ::GetLastError();
		}

		if (!completed && win32_error == ERROR_SUCCESS)
			win32_error = ::GetLastError();
		::CloseHandle(operation.hEvent);
		::CloseHandle(pipe);
		return completed != FALSE;
	}

	std::uint64_t next_control_request() const
	{
		return next_control_request_.fetch_add(1, std::memory_order_relaxed);
	}

	SessionRequest make_request(SessionOperation type, std::uint64_t request_id, composition::SessionToken session_id) const
	{
		SessionRequest request;
		request.operation = type;
		request.request_id = request_id == 0 ? next_control_request() : request_id;
		request.session_id = session_id;
		request.server_generation = server_generation_.load(std::memory_order_acquire);
		request.client_id = client_id_;
		request.deadline_tick_ms = ::GetTickCount64() + timeout_ms_;
		if (session_id != 0)
		{
			std::lock_guard<std::mutex> lock(session_mutex_);
			auto it = session_generations_.find(session_id);
			if (it != session_generations_.end())
            {
                request.session_generation = it->second.session_generation;
                request.server_generation = it->second.server_generation;
            }
		}
		return request;
	}

	composition::SessionResult call(SessionRequest request)
	{
		if (request.operation != SessionOperation::kHello && !ensure_handshake())
		{
			composition::SessionResult failure;
			failure.status = composition::SessionStatus::kUnavailable;
			failure.request_id = request.request_id;
			failure.session_id = request.session_id;
			failure.message = "engine handshake failed";
			return failure;
		}
        const auto current_generation = server_generation_.load(std::memory_order_acquire);
        if (request.session_id != 0 && (request.session_generation == 0 || request.server_generation != current_generation))
        {
            composition::SessionResult failure;
            failure.status = composition::SessionStatus::kInvalidSession;
            failure.request_id = request.request_id;
            failure.session_id = request.session_id;
            failure.session_generation = request.session_generation;
            failure.message = "session belongs to an unavailable host generation";
            return failure;
        }
        request.server_generation = current_generation;
		return call_transport(request);
	}

	composition::SessionResult call_transport(const SessionRequest& request)
	{
		coral::input_timing::Scope timing(L"engine.transport", request.request_id);
		composition::SessionResult failure;
		failure.request_id = request.request_id;
		failure.session_id = request.session_id;
		failure.session_generation = request.session_generation;

		if (deadline_expired(request.deadline_tick_ms, ::GetTickCount64()))
		{
			failure.status = composition::SessionStatus::kTimeout;
			failure.message = "request deadline expired before send";
			return failure;
		}

		std::vector<std::uint8_t> encoded;
		std::string error;
		if (!encode_request(request, encoded, error))
		{
			failure.status = composition::SessionStatus::kProtocolError;
			failure.message = std::move(error);
			return failure;
		}

		std::vector<std::uint8_t> reply(kEnvelopeBytes + kMaxPayloadBytes);
		DWORD reply_bytes = 0;
		DWORD win32_error = ERROR_SUCCESS;
		const auto transact_start = coral::input_timing::nowUs();
		const bool transacted = transact_with_deadline(encoded, reply, request.deadline_tick_ms, reply_bytes, win32_error);
		coral::input_timing::mark(L"engine.transact", transact_start, request.request_id, win32_error);
		if (!transacted)
		{
			server_generation_.store(0, std::memory_order_release);
			if (win32_error == ERROR_SEM_TIMEOUT && request.operation != SessionOperation::kCancel)
				send_cancel_best_effort(request.request_id, request.server_generation);
			failure.status = status_from_win32(win32_error);
			failure.message = "engine pipe call failed";
			return failure;
		}

		SessionReply response;
		if (!decode_reply(reply.data(), reply_bytes, response, error) ||
			response.operation != request.operation ||
			response.client_id != request.client_id ||
			response.engine.request_id != request.request_id ||
			(request.session_id != 0 && response.engine.session_id != request.session_id) ||
			(request.session_generation != 0 &&
				response.engine.session_generation != request.session_generation))
		{
			failure.status = composition::SessionStatus::kProtocolError;
			failure.message = error.empty() ? "response correlation mismatch" : std::move(error);
			return failure;
		}
        if (request.operation != SessionOperation::kHello && response.engine.server_generation != request.server_generation)
        {
            server_generation_.store(0, std::memory_order_release);
            failure.status = response.engine.status == composition::SessionStatus::kInvalidSession
                ? composition::SessionStatus::kInvalidSession : composition::SessionStatus::kProtocolError;
            failure.message = "engine host generation changed; handshake required";
            return failure;
        }
		if (deadline_expired(request.deadline_tick_ms, ::GetTickCount64()))
		{
			failure.status = composition::SessionStatus::kTimeout;
			failure.message = "response arrived after deadline";
			return failure;
		}
		return response.engine;
	}

	void send_cancel_best_effort(std::uint64_t target_request_id, std::uint64_t generation)
	{
		SessionRequest cancel;
		cancel.operation = SessionOperation::kCancel;
		cancel.request_id = next_control_request();
		cancel.cancel_request_id = target_request_id;
		cancel.server_generation = generation;
		cancel.client_id = client_id_;
		cancel.deadline_tick_ms = ::GetTickCount64() + 15;
		std::vector<std::uint8_t> encoded;
		std::string error;
		if (!encode_request(cancel, encoded, error)) return;
		std::vector<std::uint8_t> reply(kEnvelopeBytes + kMaxPayloadBytes);
		DWORD bytes = 0, win32_error = ERROR_SUCCESS;
		transact_with_deadline(encoded, reply, cancel.deadline_tick_ms, bytes, win32_error);
	}

	std::wstring pipe_name_;
	std::uint32_t timeout_ms_ = 40;
	std::uint64_t client_id_ = 0;
	mutable std::atomic<std::uint64_t> next_control_request_{ 1 };
	mutable std::atomic<std::uint64_t> server_generation_{ 0 };
    std::atomic<std::uint64_t> last_known_generation_{ 0 };
	mutable std::mutex handshake_mutex_;
	mutable std::mutex session_mutex_;
	struct SessionIdentity {
        std::uint64_t session_generation;
        std::uint64_t server_generation;
    };
    std::unordered_map<composition::SessionToken, SessionIdentity> session_generations_;
};
}

std::wstring default_session_pipe()
{
    return L"\\\\.\\pipe\\" + session_object_name(coral::product::kSessionPipePrefix);
}

std::unique_ptr<composition::SessionPort> make_pipe_session_client(std::wstring pipe_name, std::uint32_t timeout_ms)
{
	return std::make_unique<PipeSessionClient>(std::move(pipe_name), timeout_ms);
}

} // 命名空间 coral::transport 结束

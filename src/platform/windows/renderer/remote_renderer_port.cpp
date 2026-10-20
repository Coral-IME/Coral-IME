#include "remote_renderer_port.h"
#include "../common/input_timing.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace coral::renderer_ipc
{
namespace
{
std::uint64_t NewClientId()
{
	LARGE_INTEGER counter{};
	::QueryPerformanceCounter(&counter);
	return (static_cast<std::uint64_t>(::GetCurrentProcessId()) << 32) ^
		static_cast<std::uint64_t>(counter.QuadPart) ^ reinterpret_cast<std::uintptr_t>(&counter);
}

bool OverlappedIo(HANDLE file, bool write, void* buffer, DWORD size, DWORD& transferred, DWORD timeoutMs)
{
	OVERLAPPED operation{};
	operation.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!operation.hEvent)
		return false;
	BOOL started = write
		? ::WriteFile(file, buffer, size, &transferred, &operation)
		: ::ReadFile(file, buffer, size, &transferred, &operation);
	bool ok = started != FALSE;
	if (!ok && ::GetLastError() == ERROR_IO_PENDING)
	{
		const DWORD wait = ::WaitForSingleObject(operation.hEvent, timeoutMs);
		if (wait == WAIT_OBJECT_0)
			ok = ::GetOverlappedResult(file, &operation, &transferred, FALSE) != FALSE;
		else
		{
			::CancelIoEx(file, &operation);
			// OVERLAPPED 与缓冲区必须比被取消的操作存活更久。
			::GetOverlappedResult(file, &operation, &transferred, TRUE);
			ok = false;
		}
	}
	::CloseHandle(operation.hEvent);
	return ok;
}

std::uint64_t ContextKey(const composition::RenderContextToken& token)
{
	return token.serviceInstanceId ^ (token.contextId * 0x9E3779B185EBCA87ULL);
}

std::uint64_t CandidateListSignature(const composition::CandidateRenderSnapshot& snapshot)
{
	std::uint64_t hash = 1469598103934665603ULL;
	for (const auto& candidate : snapshot.candidates)
	{
		hash ^= candidate.id;
		hash *= 1099511628211ULL;
	}
	return hash ? hash : 1;
}

class RemoteRendererPort final : public composition::RendererPort
{
public:
	RemoteRendererPort(std::wstring pipeName, std::uint32_t timeoutMs,
		std::function<bool()> ensureRunning)
		: m_pipeName(std::move(pipeName)), m_timeoutMs(timeoutMs),
		  m_clientId(NewClientId()), m_ensureRunning(std::move(ensureRunning))
	{
		m_worker = std::thread([this]() { run(); });
	}

	~RemoteRendererPort() override
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_cv.notify_all();
		if (m_worker.joinable())
			m_worker.join();
		// 最终清除之后，任何工作线程都不能再发布过期快照。
		if (m_rendererGeneration.load(std::memory_order_acquire) != 0)
		{
			Message clear;
			clear.type = SessionOperation::ClearClient;
			send(std::move(clear));
		}
	}

	bool available() const override { return m_healthy.load(std::memory_order_acquire); }

	void SetActionHandler(ActionHandler handler) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_actionHandler = std::move(handler);
	}

	void PublishCandidate(composition::CandidateRenderSnapshot snapshot) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_latestCandidate = snapshot;
		m_pendingCandidate = std::move(snapshot);
		m_cv.notify_one();
	}

	void PublishToolbar(composition::ToolbarRenderSnapshot snapshot) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_latestToolbar = snapshot;
		m_pendingToolbar = std::move(snapshot);
		m_cv.notify_one();
	}

	void HideContext(composition::RenderContextToken context, std::uint64_t version) override
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_latestCandidate &&
			ContextKey(m_latestCandidate->context) == ContextKey(context) &&
			m_latestCandidate->context.contextGeneration == context.contextGeneration &&
			version >= m_latestCandidate->version)
		{
			m_latestCandidate->visible = false;
			m_latestCandidate->version = version;
		}
		m_pendingHide = std::make_pair(context, version);
		m_cv.notify_one();
	}

	void ClearClient() override
	{
		if (!available()) return;
		Message message;
		message.type = SessionOperation::ClearClient;
		if (!send(std::move(message)))
			m_healthy.store(false, std::memory_order_release);
	}

private:
	bool exchange(const Message& request, Message& response)
	{
		std::lock_guard<std::mutex> ioLock(m_ioMutex);
		const ULONGLONG deadline = ::GetTickCount64() + m_timeoutMs;
		HANDLE pipe = INVALID_HANDLE_VALUE;
		do
		{
			const ULONGLONG now = ::GetTickCount64();
			const DWORD remaining = now >= deadline ? 0 : static_cast<DWORD>(deadline - now);
			if (!::WaitNamedPipeW(m_pipeName.c_str(), remaining))
			{
				const DWORD error = ::GetLastError();
				if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY &&
					error != ERROR_SEM_TIMEOUT)
					return false;
			}
			else
			{
				pipe = ::CreateFileW(m_pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
					nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
				if (pipe != INVALID_HANDLE_VALUE) break;
				const DWORD error = ::GetLastError();
				if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY)
					return false;
			}
			::Sleep(2);
		} while (::GetTickCount64() < deadline);
		if (pipe == INVALID_HANDLE_VALUE) return false;
		DWORD mode = PIPE_READMODE_MESSAGE;
		::SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
		std::vector<std::uint8_t> frame;
		std::string error;
		if (!EncodeMessage(request, frame, error))
		{
			::CloseHandle(pipe);
			return false;
		}
		DWORD transferred = 0;
		bool ok = OverlappedIo(pipe, true, frame.data(), static_cast<DWORD>(frame.size()),
			transferred, m_timeoutMs) && transferred == frame.size();
		std::vector<std::uint8_t> input(sizeof(FrameHeader) + kMaxPayloadBytes);
		if (ok)
			ok = OverlappedIo(pipe, false, input.data(), static_cast<DWORD>(input.size()),
				transferred, m_timeoutMs);
		::CloseHandle(pipe);
		return ok && DecodeMessage(input.data(), transferred, response, error) &&
			response.clientId == m_clientId && response.type == request.type;
	}

	bool handshake()
	{
		Message request;
		request.type = SessionOperation::Hello;
		request.clientId = m_clientId;
		Message response;
		if (!exchange(request, response) || response.rendererGeneration == 0)
			return false;
		const std::uint64_t previousGeneration = m_rendererGeneration.load(std::memory_order_acquire);
		m_rendererGeneration.store(response.rendererGeneration, std::memory_order_release);
		if (previousGeneration != 0 && previousGeneration != response.rendererGeneration)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastCandidates.clear();
			m_lastToolbar = {};
		}
		return true;
	}

	bool send(Message message, Message* responseOut = nullptr)
	{
		message.clientId = m_clientId;
		const auto generation = m_rendererGeneration.load(std::memory_order_acquire);
		message.rendererGeneration = generation;
		Message response;
		const auto start = coral::input_timing::nowUs();
		if (!exchange(message, response))
		{
			coral::input_timing::mark(L"renderer.worker_exchange_failed", start,
				static_cast<std::uint64_t>(message.type));
			return false;
		}
		if (response.rendererGeneration != generation)
			return false;
		if (responseOut)
			*responseOut = std::move(response);
		return true;
	}

	void deliverIfCurrent(const composition::RendererAction& action)
	{
		ActionHandler handler;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (action.type == composition::RendererActionType::None)
				return;
			const bool candidateAction =
				action.type == composition::RendererActionType::select_candidate ||
				action.type == composition::RendererActionType::PinCandidate ||
				action.type == composition::RendererActionType::DeleteCandidate ||
				action.type == composition::RendererActionType::AddPhrase ||
				action.type == composition::RendererActionType::ToggleExpansion ||
				action.type == composition::RendererActionType::RequestPrefetch ||
				action.type == composition::RendererActionType::CandidateLayout;
			if (candidateAction)
			{
				const auto it = m_lastCandidates.find(ContextKey(action.context));
				if (it == m_lastCandidates.end()) return;
				const auto& snapshot = it->second;
				if (snapshot.context.contextGeneration != action.context.contextGeneration)
					return;
				const bool itemAction = action.type == composition::RendererActionType::select_candidate ||
					action.type == composition::RendererActionType::PinCandidate ||
					action.type == composition::RendererActionType::DeleteCandidate ||
					action.type == composition::RendererActionType::AddPhrase;
				if (itemAction)
				{
					const auto found = std::find_if(snapshot.candidates.begin(),
						snapshot.candidates.end(), [&action](const auto& candidate) {
							return candidate.id == action.candidateId;
						});
					if (found == snapshot.candidates.end()) return;
				}
				else if (action.type == composition::RendererActionType::CandidateLayout &&
					action.candidateId != CandidateListSignature(snapshot))
					return;
			}
			else if (action.context.serviceInstanceId != m_lastToolbar.context.serviceInstanceId ||
				action.context.contextId != m_lastToolbar.context.contextId ||
				action.context.contextGeneration != m_lastToolbar.context.contextGeneration ||
				action.snapshotVersion != m_lastToolbar.version)
				return;
			handler = m_actionHandler;
		}
		if (handler)
			handler(action);
	}

	bool presentingLocked() const
	{
		return (m_latestCandidate && m_latestCandidate->visible) ||
			(m_latestToolbar && m_latestToolbar->visible);
	}

	void run()
	{
		for (;;)
		{
			std::optional<composition::CandidateRenderSnapshot> candidate;
			std::optional<composition::ToolbarRenderSnapshot> toolbar;
			std::optional<std::pair<composition::RenderContextToken, std::uint64_t>> hide;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				const auto pending = [this]() {
					return m_stopping || m_pendingCandidate || m_pendingToolbar || m_pendingHide;
				};
				// 只有正在显示 UI 的客户端才能接收点击。每个 TSF 线程都持有
				// 一个端口，因此空闲的后台客户端不得轮询唯一的
				// 渲染器管道实例而饿死聚焦客户端。
				if (m_healthy.load(std::memory_order_acquire))
				{
					if (presentingLocked())
						m_cv.wait_for(lock, std::chrono::milliseconds(10), pending);
					else
						m_cv.wait(lock, pending);
				}
				else
				{
					const auto now = ::GetTickCount64();
					const auto delay = now < m_nextRetryTick ? m_nextRetryTick - now : 0;
					m_cv.wait_for(lock, std::chrono::milliseconds(delay), [this]() {
						return m_stopping;
					});
				}
				if (m_stopping) return;
			}
			if (!m_healthy.load(std::memory_order_acquire))
			{
				const auto now = ::GetTickCount64();
				if (now < m_nextRetryTick) continue;
				// 对活跃渲染器的单次管道超时不得让本客户端的 UI
				// 冻结整个启动间隔；先重连，只有渲染器确实消失时
				// 才回退到（限速的）启动流程。
				bool connected = false;
				if (m_rendererGeneration.load(std::memory_order_acquire) != 0)
				{
					const auto connectStart = coral::input_timing::nowUs();
					connected = handshake();
					coral::input_timing::mark(L"renderer.worker_reconnect", connectStart, 0, connected);
				}
				if (!connected && now >= m_nextLaunchTick)
				{
					m_nextLaunchTick = now + 1000;
					const auto launchStart = coral::input_timing::nowUs();
					const bool running = !m_ensureRunning || m_ensureRunning();
					coral::input_timing::mark(L"renderer.worker_launch", launchStart, 0, running);
					if (running)
					{
						const auto connectStart = coral::input_timing::nowUs();
						connected = handshake();
						coral::input_timing::mark(L"renderer.worker_handshake", connectStart, 0, connected);
					}
				}
				if (!connected)
				{
					m_retryDelayMs = (std::min<std::uint64_t>)(m_retryDelayMs * 2, 1000);
					m_nextRetryTick = ::GetTickCount64() + m_retryDelayMs;
					continue;
				}
				m_retryDelayMs = kInitialRetryDelayMs;
				m_healthy.store(true, std::memory_order_release);
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_pendingCandidate && m_latestCandidate)
					m_pendingCandidate = m_latestCandidate;
				if (!m_pendingToolbar && m_latestToolbar)
					m_pendingToolbar = m_latestToolbar;
			}
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				candidate.swap(m_pendingCandidate);
				toolbar.swap(m_pendingToolbar);
				hide.swap(m_pendingHide);
			}
			bool ok = m_healthy.load(std::memory_order_acquire);
			bool candidateSent = !candidate.has_value();
			bool toolbarSent = !toolbar.has_value();
			bool hideSent = !hide.has_value();
			if (candidate && ok)
			{
				Message message;
				message.type = SessionOperation::CandidateSnapshot;
				message.candidate = *candidate;
				ok = send(std::move(message));
				candidateSent = ok;
				if (ok)
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_lastCandidates[ContextKey(candidate->context)] = *candidate;
				}
			}
			if (toolbar && ok)
			{
				Message message;
				message.type = SessionOperation::ToolbarSnapshot;
				message.toolbar = *toolbar;
				ok = send(std::move(message));
				toolbarSent = ok;
				if (ok)
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_lastToolbar = *toolbar;
				}
			}
			if (hide && ok)
			{
				Message message;
				message.type = SessionOperation::HideContext;
				message.context = hide->first;
				message.version = hide->second;
				ok = send(std::move(message));
				hideSent = ok;
			}
			if (ok)
			{
				Message poll;
				poll.type = SessionOperation::PollAction;
				Message response;
				ok = send(std::move(poll), &response);
				if (ok) deliverIfCurrent(response.action);
			}
			if (!candidateSent || !toolbarSent || !hideSent)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				// 保留 I/O 期间可能已到达的更新发布；
				// 否则重试刚刚失败的快照。
				if (!candidateSent && !m_pendingCandidate)
					m_pendingCandidate = std::move(candidate);
				if (!toolbarSent && !m_pendingToolbar)
					m_pendingToolbar = std::move(toolbar);
				if (!hideSent && !m_pendingHide)
					m_pendingHide = std::move(hide);
			}
			m_healthy.store(ok, std::memory_order_release);
		}
	}

	std::wstring m_pipeName;
	std::uint32_t m_timeoutMs = 75;
	std::uint64_t m_clientId = 0;
	std::atomic<std::uint64_t> m_rendererGeneration{ 0 };
	static constexpr std::uint64_t kInitialRetryDelayMs = 25;
	std::uint64_t m_nextRetryTick = 0;
	std::uint64_t m_retryDelayMs = kInitialRetryDelayMs;
	std::uint64_t m_nextLaunchTick = 0;
	std::function<bool()> m_ensureRunning;
	std::atomic<bool> m_healthy{ false };
	std::mutex m_mutex;
	std::mutex m_ioMutex;
	std::condition_variable m_cv;
	bool m_stopping = false;
	std::optional<composition::CandidateRenderSnapshot> m_pendingCandidate;
	std::optional<composition::ToolbarRenderSnapshot> m_pendingToolbar;
	std::optional<composition::CandidateRenderSnapshot> m_latestCandidate;
	std::optional<composition::ToolbarRenderSnapshot> m_latestToolbar;
	std::optional<std::pair<composition::RenderContextToken, std::uint64_t>> m_pendingHide;
	std::unordered_map<std::uint64_t, composition::CandidateRenderSnapshot> m_lastCandidates;
	composition::ToolbarRenderSnapshot m_lastToolbar;
	ActionHandler m_actionHandler;
	std::thread m_worker;
};
}

std::unique_ptr<composition::RendererPort> CreateRemoteRendererPort(
	std::wstring pipeName, std::uint32_t timeoutMs,
	std::function<bool()> ensureRunning)
{
	return std::make_unique<RemoteRendererPort>(
		std::move(pipeName), timeoutMs, std::move(ensureRunning));
}

} // 命名空间 coral::renderer_ipc 结束

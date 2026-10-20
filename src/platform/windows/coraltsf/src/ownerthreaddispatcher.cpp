#include "pch.h"
#include "ownerthreaddispatcher.h"

namespace coral::tsf
{
namespace
{
constexpr wchar_t kDispatcherWindowClass[] = L"CoralPinyin.TsfOwnerDispatcher";
constexpr UINT kDispatchMessage = WM_APP + 0x35A;
}

OwnerThreadDispatcher::~OwnerThreadDispatcher()
{
	shutdown();
}

HRESULT OwnerThreadDispatcher::initialize(ContextStore* contexts, HINSTANCE instance)
{
	if (!contexts || !instance)
		return E_INVALIDARG;
	if (m_window)
		return S_FALSE;

	m_ownerThreadId = ::GetCurrentThreadId();
	m_contexts = contexts;
	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = &OwnerThreadDispatcher::WindowProc;
	windowClass.hInstance = instance;
	windowClass.lpszClassName = kDispatcherWindowClass;
	if (!::RegisterClassW(&windowClass) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return HRESULT_FROM_WIN32(::GetLastError());

	m_window = ::CreateWindowExW(
		0, kDispatcherWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
		nullptr, instance, this);
	return m_window ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
}

void OwnerThreadDispatcher::shutdown()
{
	HWND window = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_queue.clear();
		window = m_window;
		if (window)
			for (const auto& timer : m_timers)
				::KillTimer(window, timer.first);
		m_timers.clear();
		m_window = nullptr;
		m_contexts = nullptr;
	}
	if (window && ::GetCurrentThreadId() == m_ownerThreadId)
		::DestroyWindow(window);
	m_ownerThreadId = 0;
}

bool OwnerThreadDispatcher::post(RequestToken token, std::function<void()> callback)
{
	return enqueue(token, std::move(callback), false);
}

bool OwnerThreadDispatcher::postService(std::uint64_t serviceId, std::uint64_t request_id,
	std::function<void()> callback)
{
	if (!serviceId) return false;
	return enqueue({ { serviceId, 0, 0 }, request_id }, std::move(callback), true);
}

bool OwnerThreadDispatcher::enqueue(RequestToken token, std::function<void()> callback,
	bool serviceScoped)
{
	if (!callback || token.request_id == 0)
		return false;
	HWND window = nullptr;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_window)
			return false;
		m_queue.push_back(WorkItem{ token, std::move(callback), serviceScoped });
		window = m_window;
	}
	if (::PostMessageW(window, kDispatchMessage, 0, 0))
		return true;
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_queue.empty() && m_queue.back().token.request_id == token.request_id)
		m_queue.pop_back();
	return false;
}

bool OwnerThreadDispatcher::postDelayed(
	RequestToken token, std::function<void()> callback, UINT delayMs)
{
	if (!isOwnerThread() || !callback || token.request_id == 0 || !m_window)
		return false;
	const UINT_PTR timerId = m_nextTimerId++;
	m_timers.emplace(timerId, WorkItem{ token, std::move(callback) });
	if (::SetTimer(m_window, timerId, delayMs, nullptr))
		return true;
	m_timers.erase(timerId);
	return false;
}

LRESULT CALLBACK OwnerThreadDispatcher::WindowProc(
	HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
	OwnerThreadDispatcher* self = reinterpret_cast<OwnerThreadDispatcher*>(
		::GetWindowLongPtrW(window, GWLP_USERDATA));
	if (message == WM_NCCREATE)
	{
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		self = static_cast<OwnerThreadDispatcher*>(create->lpCreateParams);
		::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	if (message == kDispatchMessage && self)
	{
		self->drain();
		return 0;
	}
	if (message == WM_TIMER && self)
	{
		self->drainTimer(static_cast<UINT_PTR>(wParam));
		return 0;
	}
	return ::DefWindowProcW(window, message, wParam, lParam);
}

void OwnerThreadDispatcher::drain()
{
	if (!isOwnerThread())
		return;
	for (;;)
	{
		WorkItem item;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queue.empty())
				break;
			item = std::move(m_queue.front());
			m_queue.pop_front();
		}
		if (m_contexts && (item.serviceScoped ?
			item.token.context.serviceInstanceId == m_contexts->serviceInstanceId() :
			m_contexts->isCurrent(item.token)))
			item.callback();
	}
}

void OwnerThreadDispatcher::drainTimer(UINT_PTR timerId)
{
	if (!isOwnerThread()) return;
	auto it = m_timers.find(timerId);
	if (it == m_timers.end()) return;
	::KillTimer(m_window, timerId);
	WorkItem item = std::move(it->second);
	m_timers.erase(it);
	if (m_contexts && m_contexts->isCurrent(item.token))
		item.callback();
}

} // 命名空间 coral::tsf 结束

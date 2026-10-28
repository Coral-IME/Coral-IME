#pragma once

#include "contextstore.h"

#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace coral::tsf
{

// 仅消息窗口派发器。工作线程回调只携带值令牌与
// 数据；回调在 TSF 所有者线程上执行并重新校验。
class OwnerThreadDispatcher final
{
public:
	OwnerThreadDispatcher() = default;
	~OwnerThreadDispatcher();
	OwnerThreadDispatcher(const OwnerThreadDispatcher&) = delete;
	OwnerThreadDispatcher& operator=(const OwnerThreadDispatcher&) = delete;

	HRESULT initialize(ContextStore* contexts, HINSTANCE instance);
	void shutdown();
	bool post(RequestToken token, std::function<void()> callback);
	// 没有可编辑文档（如桌面）时工具栏动作仍保持有效。
	bool postService(std::uint64_t serviceId, std::uint64_t request_id,
		std::function<void()> callback);
	bool postDelayed(RequestToken token, std::function<void()> callback, UINT delayMs);
    bool postServiceDelayed(std::uint64_t service_id, std::uint64_t request_id,
        std::function<void()> callback, UINT delay_ms);
	bool isOwnerThread() const { return m_ownerThreadId == ::GetCurrentThreadId(); }

private:
	struct WorkItem
	{
		RequestToken token;
		std::function<void()> callback;
		bool serviceScoped = false;
	};

	bool enqueue(RequestToken token, std::function<void()> callback, bool serviceScoped);
    bool post_timer(RequestToken token, std::function<void()> callback, UINT delay_ms, bool service_scoped);
	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	void drain();
	void drainTimer(UINT_PTR timerId);

	DWORD m_ownerThreadId = 0;
	HWND m_window = nullptr;
	ContextStore* m_contexts = nullptr;
	std::mutex m_mutex;
	std::deque<WorkItem> m_queue;
	std::unordered_map<UINT_PTR, WorkItem> m_timers;
	UINT_PTR m_nextTimerId = 1;
};

} // 命名空间 coral::tsf 结束

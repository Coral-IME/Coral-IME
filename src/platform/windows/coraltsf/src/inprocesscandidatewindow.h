#pragma once

#include "candidate_view.h"

#include <functional>

namespace coral::tsf
{
// 系统与提权宿主持有此弹窗；布局与绘制位于 CandidateView。
class InProcessCandidateWindow final
{
public:
	using SelectHandler = std::function<void(std::size_t, std::uint64_t)>;
	using ToggleHandler = std::function<void()>;
	using PrefetchHandler = std::function<void()>;

	InProcessCandidateWindow() = default;
	~InProcessCandidateWindow();
	InProcessCandidateWindow(const InProcessCandidateWindow&) = delete;
	InProcessCandidateWindow& operator=(const InProcessCandidateWindow&) = delete;

	bool present(const composition::CandidateRenderSnapshot& snapshot, HWND owner,
		SelectHandler select, ToggleHandler toggle, PrefetchHandler prefetch);
	void hide();

	const std::vector<std::uint32_t>& rowStarts() const { return m_view.rowStarts(); }
	const std::vector<std::uint32_t>& itemCenters() const { return m_view.itemCenters(); }

private:
	static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
	bool ensureWindow(HWND owner);
	void onClick(POINT point);
	void prefetchNearEnd();

	HWND m_window = nullptr;
	HWND m_owner = nullptr;
	win32::CandidateView m_view;
	SelectHandler m_select;
	ToggleHandler m_toggle;
	PrefetchHandler m_prefetch;
	bool m_scrollDragging = false;
	int m_scrollDragOffset = 0;
};
} // 命名空间 coral::tsf 结束

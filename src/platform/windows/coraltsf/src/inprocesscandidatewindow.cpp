#include "pch.h"
#include "inprocesscandidatewindow.h"

#include <algorithm>
#include <windowsx.h>

namespace coral::tsf
{
namespace
{
constexpr wchar_t kWindowClass[] = L"CoralPinyin.TsfCandidate.Search.v1";
}

InProcessCandidateWindow::~InProcessCandidateWindow()
{
	if (m_window) ::DestroyWindow(m_window);
}

bool InProcessCandidateWindow::ensureWindow(HWND owner)
{
	if (!owner || !::IsWindow(owner)) return false;
	owner = ::GetAncestor(owner, GA_ROOT);
	if (!owner) return false;
	DWORD processId = 0;
	::GetWindowThreadProcessId(owner, &processId);
	if (processId != ::GetCurrentProcessId())
	{
		LOG_INFO_W(L"[tsf_local_ui] event=owner_not_local owner_pid=" +
			std::to_wstring(processId));
		return false;
	}
	if (m_window && m_owner == owner && ::IsWindow(m_window)) return true;
	if (m_window)
	{
		::DestroyWindow(m_window);
		m_window = nullptr;
	}
	WNDCLASSEXW cls{ sizeof(cls) };
	cls.lpfnWndProc = WindowProc;
	cls.hInstance = g_hInst;
	cls.lpszClassName = kWindowClass;
	cls.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
	if (!::RegisterClassExW(&cls) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
	{
		LOG_ERROR_W(L"[tsf_local_ui] event=register_failed error=" +
			std::to_wstring(::GetLastError()));
		return false;
	}
	m_window = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
		WS_EX_NOACTIVATE | WS_EX_LAYERED, kWindowClass, L"", WS_POPUP,
		0, 0, 0, 0, owner, nullptr, g_hInst, this);
	if (!m_window)
	{
		LOG_ERROR_W(L"[tsf_local_ui] event=create_failed error=" +
			std::to_wstring(::GetLastError()));
		return false;
	}
	m_owner = owner;
	LOG_INFO_W(L"[tsf_local_ui] event=created pid=" +
		std::to_wstring(::GetCurrentProcessId()) + L" owner_pid=" +
		std::to_wstring(processId));
	return true;
}

void InProcessCandidateWindow::hide()
{
	if (m_window && ::IsWindow(m_window)) ::ShowWindow(m_window, SW_HIDE);
	m_view.clear();
}

bool InProcessCandidateWindow::present(const composition::CandidateRenderSnapshot& snapshot,
	HWND owner, SelectHandler select, ToggleHandler toggle, PrefetchHandler prefetch)
{
	if (!ensureWindow(owner)) return false;
	if (!snapshot.visible || (snapshot.candidates.empty() && snapshot.hint.empty()))
	{
		hide();
		return true;
	}
	m_select = std::move(select);
	m_toggle = std::move(toggle);
	m_prefetch = std::move(prefetch);
	m_view.update(m_window, snapshot);
	const RECT anchor{ snapshot.anchorLeft, snapshot.anchorTop,
		snapshot.anchorRight, snapshot.anchorBottom };
	MONITORINFO monitor{ sizeof(monitor) };
	if (!::GetMonitorInfoW(::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST),
		&monitor)) return false;
	const RECT& work = monitor.rcWork;
	const UINT dpi = g_fGetDpiForWindow ? g_fGetDpiForWindow(m_window) : 96;
	const int gap = ::MulDiv(2, dpi ? static_cast<int>(dpi) : 96, 96);
	const POINT position = m_view.place(anchor, work, gap);
	m_view.present(m_window, position);
	if (!::SetWindowPos(m_window, HWND_TOPMOST, position.x, position.y,
		m_view.width(), m_view.height(), SWP_NOACTIVATE | SWP_SHOWWINDOW))
	{
		LOG_ERROR_W(L"[tsf_local_ui] event=show_failed error=" +
			std::to_wstring(::GetLastError()));
		return false;
	}
	return true;
}

void InProcessCandidateWindow::onClick(POINT point)
{
	const RECT expand = m_view.expandRect();
	if (::PtInRect(&expand, point))
	{
		auto toggle = m_toggle;
		if (toggle) toggle();
		return;
	}
	const int index = m_view.hit(point);
	const auto& candidates = m_view.snapshot().candidates;
	if (index < 0 || static_cast<std::size_t>(index) >= candidates.size()) return;
	auto select = m_select;
	if (select) select(static_cast<std::size_t>(index), candidates[index].id);
}

void InProcessCandidateWindow::prefetchNearEnd()
{
	const auto& snapshot = m_view.snapshot();
	if (!snapshot.expanded || !snapshot.hasMore || !m_view.nearEnd()) return;
	auto prefetch = m_prefetch;
	if (prefetch) prefetch();
}

LRESULT CALLBACK InProcessCandidateWindow::WindowProc(HWND window, UINT message,
	WPARAM wParam, LPARAM lParam)
{
	if (message == WM_NCCREATE)
	{
		auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		auto* self = static_cast<InProcessCandidateWindow*>(create->lpCreateParams);
		::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	auto* self = reinterpret_cast<InProcessCandidateWindow*>(
		::GetWindowLongPtrW(window, GWLP_USERDATA));
	return self ? self->handleMessage(window, message, wParam, lParam)
		: ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT InProcessCandidateWindow::handleMessage(HWND window, UINT message,
	WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_ERASEBKGND:
		return 1;
	case WM_SETCURSOR:
	{
		if (LOWORD(lParam) != HTCLIENT) break;
		POINT point{};
		::GetCursorPos(&point);
		::ScreenToClient(window, &point);
		::SetCursor(::LoadCursorW(nullptr, m_view.clickableAt(point) ? IDC_HAND : IDC_ARROW));
		return TRUE;
	}
	case WM_TIMER:
		if (wParam != win32::CandidateView::kAnimationTimer) break;
		m_view.onTimer(window);
		return 0;
	case WM_PAINT:
	{
		PAINTSTRUCT paint{};
		::BeginPaint(window, &paint);
		::EndPaint(window, &paint);
		m_view.redraw(window);
		return 0;
	}
	case WM_MOUSEMOVE:
	{
		if (m_scrollDragging)
		{
			const RECT track = m_view.scrollTrack();
			const RECT thumb = m_view.scrollThumb();
			const int travel = (track.bottom - track.top) -
				(thumb.bottom - thumb.top);
			if (m_view.maxTopRow() > 0 && travel > 0)
				m_view.setTopRow((GET_Y_LPARAM(lParam) - m_scrollDragOffset -
					track.top) * m_view.maxTopRow() / travel);
			m_view.redraw(window);
			prefetchNearEnd();
		}
		else if (m_view.setPointer({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }))
			m_view.redraw(window);
		TRACKMOUSEEVENT trackMouse{ sizeof(trackMouse), TME_LEAVE, window, 0 };
		::TrackMouseEvent(&trackMouse);
		return 0;
	}
	case WM_MOUSELEAVE:
		m_view.clearPointer();
		m_view.redraw(window);
		return 0;
	case WM_CAPTURECHANGED:
		m_scrollDragging = false;
		m_view.setScrollDragging(false);
		m_view.redraw(window);
		return 0;
	case WM_LBUTTONDOWN:
	{
		const POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		const RECT thumb = m_view.scrollThumb();
		if (!::IsRectEmpty(&thumb) && ::PtInRect(&thumb, point))
		{
			m_scrollDragging = true;
			m_scrollDragOffset = point.y - thumb.top;
			m_view.setScrollDragging(true);
			::SetCapture(window);
			m_view.redraw(window);
			return 0;
		}
		const RECT track = m_view.scrollTrack();
		if (!::IsRectEmpty(&thumb) && ::PtInRect(&track, point))
		{
			m_view.setTopRow((point.y - track.top) * m_view.maxTopRow() /
				(std::max)(1, static_cast<int>(track.bottom - track.top)));
			m_view.redraw(window);
			prefetchNearEnd();
			return 0;
		}
		break;
	}
	case WM_LBUTTONUP:
		if (m_scrollDragging)
		{
			m_scrollDragging = false;
			::ReleaseCapture();
			return 0;
		}
		onClick({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		return 0;
	case WM_RBUTTONUP:
		if (m_view.snapshot().letterSelection || m_view.snapshot().listLayout)
			onClick({ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) });
		return 0;
	case WM_MOUSEWHEEL:
		if (m_view.snapshot().expanded)
		{
			m_view.scrollRows(GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? -1 : 1);
			m_view.redraw(window);
			prefetchNearEnd();
		}
		return 0;
	case WM_NCDESTROY:
		if (m_window == window) m_window = nullptr;
		m_owner = nullptr;
		::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
		break;
	default:
		break;
	}
	return ::DefWindowProcW(window, message, wParam, lParam);
}
} // 命名空间 coral::tsf 结束

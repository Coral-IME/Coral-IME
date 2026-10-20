#pragma once

template <class T>
class CMouseAction
{
public:
	CMouseAction()
		: m_bPressed(FALSE)
		, m_bHover(FALSE)
	{

	}

	~CMouseAction()
	{

	}
public:
	BEGIN_MSG_MAP(CMouseAction)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
	END_MSG_MAP()

	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		ATLASSERT(pT->IsWindow());
		m_bPressed = TRUE;
		POINT ptCursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		bHandled = pT->OnPress(ptCursor);
		return 0;
	}

	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		ATLASSERT(pT->IsWindow());
		
		if (m_bPressed)
		{
			m_bPressed = FALSE;
			POINT ptCursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			bHandled = pT->OnPressRelease(ptCursor);
		}
		else
		{
			bHandled = FALSE;
		}
		return 0;
	}

	LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		ATLASSERT(pT->IsWindow());
		::SendMessage(pT->GetParent(), uMsg, wParam, lParam);
		m_bHover = TRUE;
		POINT ptCursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		bHandled = pT->OnHover(ptCursor);

		// 追踪鼠标离开
		TRACKMOUSEEVENT tme = {0};   
		tme.cbSize = sizeof(tme);   
		tme.hwndTrack = pT->m_hWnd;   
		tme.dwFlags = TME_LEAVE;   
		_TrackMouseEvent(&tme);

		return 0;
	}

	LRESULT OnMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled)
	{
		T* pT = static_cast<T*>(this);
		ATLASSERT(pT->IsWindow());
		m_bHover = FALSE;
		bHandled = pT->OnHoverOut();
		return 0;
	}

public:
	// Overrideables
	BOOL OnPress(POINT /*point*/)
	{
		// 默认：不处理
		return FALSE;
	}

	BOOL OnPressRelease(POINT /*point*/)
	{
		// 默认：不处理
		return FALSE;
	}

	BOOL OnHover(POINT /*point*/)
	{
		// 默认：不处理
		return FALSE;
	}

	BOOL OnHoverOut()
	{
		// 默认：不处理
		return FALSE;
	}

private:
	BOOL m_bPressed;
	BOOL m_bHover;
};
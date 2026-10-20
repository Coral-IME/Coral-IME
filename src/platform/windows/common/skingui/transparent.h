#pragma once

/*
* 说明：协助窗口实现透明
*/

enum
{
	TT_NONCACHE = 0xFFFFFFFF,

	// TT_PRIVATECACHE = 0x00000001,
	// TT_GLOBALCACHE = 0x00000002,

	TT_STATIC = 0x00000100,
	TT_DYNAMIC = 0x00000200,
};

// template <int t_nType>
class Transparent
{
public:
	Transparent()
		: m_nType(TT_STATIC)
	{}

	Transparent(int nType)
		: m_nType(nType)
	{}

	virtual ~Transparent()
	{}

	void SetType(int nType)
	{
		m_nType = nType;
	}

	void DrawParentBackground(CWindow* pWnd, HDC hDC, LPCRECT pRect = NULL)
	{
		ATLASSERT(pWnd != NULL);

		CWindow wParent = pWnd->GetParent();
		ATLASSERT(wParent.m_hWnd != NULL);
		CRect rcParent;
		wParent.GetClientRect(&rcParent);
		CDCHandle dc(hDC);

		_AdjustCache(wParent, rcParent);
		if (TT_DYNAMIC & m_nType)
		{
			_PrintWndToDC(wParent, m_dcCached);	
		}
		CRect rc;
		if (pRect != NULL)
		{
			rc = *pRect;
			pWnd->ClientToScreen(&rc);
		}
		else
			pWnd->GetWindowRect(&rc);
		wParent.ScreenToClient(&rc);
		dc.BitBlt(
			pRect == NULL ? 0 : pRect->left, 
			pRect == NULL ? 0 : pRect->top,
			rc.Width(), rc.Height(), m_dcCached, rc.left, rc.top, SRCCOPY);

		if ((TT_NONCACHE & m_nType) == TT_NONCACHE)
		{
			// 不保留缓存
			_ClearCache();
		}
	}

private:
	void _AdjustCache(CWindow& wnd, CRect& rc)
	{
		if (NULL == m_dcCached.m_hDC)
		{
			// 创建缓存
			_BuildCache(wnd, rc);
		}
		else
		{
			// 如果窗口大小发生变化，重新生成缓存
			SIZE sz;
			m_bmpCached.GetSize(sz);
			if (sz.cx != rc.Width() || sz.cy != rc.Height())
			{
				_ClearCache();
				_BuildCache(wnd, rc);
			}
		}
	}

	void _BuildCache(CWindow& wnd, CRect& rc)
	{
		ATLASSERT(NULL == m_dcCached.m_hDC);
		ATLASSERT(NULL == m_bmpCached.m_hBitmap);

		// 这里hDC必须用原始dc，不能用DrawParentBackground传入的dc
		// 因为传入的可能是内存dc，用它创建m_dcCached以及m_bmpCached
		// 在xp下有时会成为黑白单色图，原因不明
		HDC hDC = ::GetDC(wnd);
		m_dcCached.CreateCompatibleDC(hDC);
		m_bmpCached.CreateCompatibleBitmap(hDC, rc.Width(), rc.Height());
		CBitmap bmpOld = m_dcCached.SelectBitmap(m_bmpCached);
		::ReleaseDC(wnd, hDC);

		if (TT_STATIC & m_nType)
		{
			// 静态方式，只在创建缓存时拷贝一次
			_PrintWndToDC(wnd, m_dcCached);
		}
	}

	void _ClearCache()
	{
		m_dcCached.DeleteDC();
		m_bmpCached.DeleteObject();
	}

	void _PrintWndToDC(CWindow& wnd, HDC hDC)
	{
		wnd.Print(hDC, PRF_CLIENT | PRF_ERASEBKGND);
	}

private:
	CDC m_dcCached;
	CBitmap m_bmpCached;
	int m_nType;
};

class TransparentStatic : public Transparent
{
public:
	TransparentStatic()
		: Transparent(TT_STATIC)
	{}
};

class TransparentDynamic : public Transparent
{
public:
	TransparentDynamic()
		: Transparent(TT_DYNAMIC)
	{}
};
// typedef Transparent<TT_STATIC> TransparentStatic;
// typedef Transparent<TT_DYNAMIC> TransparentDynamic;


template< typename T >
class CFadeinWndImpl
{
public:
	CFadeinWndImpl(){}
	~CFadeinWndImpl(){}


	BEGIN_MSG_MAP_EX(CFadeinWndImpl)
		MSG_WM_TIMER(OnTimer)
	END_MSG_MAP()


	void AnimateWindow(int nTimerId, int nMilliSecond = 2)
	{
		m_nTimerId = nTimerId;
		m_nTimes = 255;
		T* pThis = static_cast< T* >(this);
		::SetWindowLong(pThis->m_hWnd, GWL_EXSTYLE, GetWindowLong(pThis->m_hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
		pThis->SetTimer(m_nTimerId, nMilliSecond / 260);
	}

	void OnTimer(UINT_PTR nIDEvent)
	{
		T* pThis = static_cast< T* >(this);
		SetLayeredWindowAttributes(pThis->m_hWnd, 0, m_nTimes--, LWA_ALPHA);
		if ( m_nTimes == 0 )
		{
			pThis->KillTimer(m_nTimerId);
		}
		SetMsgHandled(FALSE);
	}

private:
	int m_nTimes;
	int m_nTimerId;
};
#pragma once
#include <vector>
#include <map>
#include "mouseaction.h"
#include "transparent.h"
#include "skingui/gdiplusbitmap.h"
#include "wndeffect.h"

#define WM_TAB_FIRST (WM_USER + 10000)
#define WM_TAB_ACTIVE (WM_TAB_FIRST + 1)
#define WM_TAB_KILLACTIVE (WM_TAB_FIRST + 2)
#define WM_TAB_SWITCH (WM_TAB_FIRST + 3)

enum TabItemState
{
	CTIS_NORMAL,
	CTIS_HOVER,
	CTIS_PUSHED,
};

interface ITabItem
{
	virtual ~ITabItem(){}

	virtual void OnPaint(HWND hWnd, CDCHandle& dc, const RECT& rect, TabItemState state) = 0;	
};

class CPNGTabItem : public ITabItem
{
public:
	void SetImages(UINT nResNormal, UINT nResHover = -1, UINT nResPushed = -1)
	{
		m_pngNormal.Load(nResNormal, L"PNG");
		if (-1 != nResHover)
			m_pngHover.Load(nResHover, L"PNG");
		if (-1 != nResPushed)
			m_pngPushed.Load(nResPushed, L"PNG");
	}

	virtual void OnPaint(HWND hWnd, CDCHandle& dc, const RECT& rect, TabItemState state)
	{
		Gdiplus::Image* pImage = m_pngNormal.m_pBitmap;
		if (CTIS_PUSHED == state)
		{
			if (m_pngPushed.m_pBitmap)
				pImage = m_pngPushed.m_pBitmap;
		}
		else if (CTIS_HOVER == state)
		{
			if (m_pngHover.m_pBitmap)
				pImage = m_pngHover.m_pBitmap;
		}
		else
			;
		Gdiplus::Graphics g(dc);
		g.DrawImage(pImage, rect.left, rect.top,
			rect.right - rect.left,
			rect.bottom - rect.top);
	}

private:
	CGdiPlusBitmapResource m_pngNormal;
	CGdiPlusBitmapResource m_pngHover;
	CGdiPlusBitmapResource m_pngPushed;
};

interface ITabHeaderCallback
{
	virtual ~ITabHeaderCallback(){}

	virtual BOOL OnPreDrawTHBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc) = 0; // 返回TRUE就不调用默认绘制
	virtual void OnPostDrawTHBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc) = 0;
};

class CSimpleTabHeaderCallback
{
	virtual BOOL OnPreDrawTHBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc){return FALSE;}
	virtual void OnPostDrawTHBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc){}
};

typedef enum
{
	THT_HORZ = 0,
	THT_VERT = 1
} TH_TYPE;
class CTabHeader
	: public CWindowImpl<CTabHeader, CWindow, CControlWinTraits>
	, public CDoubleBufferImpl<CTabHeader>
	, public CMouseAction<CTabHeader>
	, public TransparentStatic
{
	typedef CWindowImpl<CTabHeader, CWindow, CControlWinTraits> _base;

	typedef struct
	{
		ITabItem* pItem;
		int nStartOffset;
		int nLen;
		CRect rc;
	} _ITEM;
	typedef std::vector<_ITEM> _VEC_ITEM;
	typedef _VEC_ITEM::iterator _VEC_ITEM_IT;
	typedef _VEC_ITEM::const_iterator _VEC_ITEM_CONST_IT;

public:
	CTabHeader()
		: m_nMargin1(0)
		, m_nMargin2(0)
		, m_pCallback(NULL)
		, m_clrBackground(CLR_NONE)
	{
		_Reset();
	}

	~CTabHeader()
	{

	}

public:
	DECLARE_WND_CLASS(_T("TabHeader"))

	BEGIN_MSG_MAP_EX(CTabHeader)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		MSG_WM_SETFONT(OnSetFont)
		MSG_WM_GETFONT(OnGetFont)

		CHAIN_MSG_MAP(CDoubleBufferImpl<CTabHeader>)
		CHAIN_MSG_MAP(CMouseAction<CTabHeader>)
	END_MSG_MAP()

	void OnDestroy()
	{
		_Reset();
	}

	void OnSize(UINT nType, CSize size)
	{
		_UpdateLayout();
	}

	void OnSetFont(CFontHandle font, BOOL bRedraw)
	{
		m_Font = font;
		if (bRedraw)
			Invalidate(FALSE);
	}

	HFONT OnGetFont()
	{
		return m_Font;
	}

	void DoPaint(CDCHandle dc)
	{
		HFONT hFontOld = dc.SelectFont(m_Font);
		_DrawBackground(dc);

		int nSize = m_VecItems.size();
		for (int i = 0; i < nSize; ++i)
		{
			TabItemState state;
			if (m_nSelected == i)
				state = CTIS_PUSHED;
			else if (m_nHover == i)
				state = CTIS_HOVER;
			else
				state = CTIS_NORMAL;
			m_VecItems[i].pItem->OnPaint(m_hWnd, dc, m_VecItems[i].rc, state);
		}
		dc.SelectFont(hFontOld);
	}

public:
	HWND Create(HWND hWndParent, _U_RECT rect  = NULL, TH_TYPE type = THT_HORZ, LPCTSTR szWindowName = NULL, DWORD dwStyle = 0, DWORD dwExStyle = 0, LPVOID lpCreateParam  = NULL)
	{
		m_type = type;
		return _base::Create(hWndParent, rect, szWindowName, dwStyle, dwExStyle, 0U, lpCreateParam);
	}

	int AddItem(int nOffset, int nLen, ITabItem* pItem)
	{
		ATLASSERT(::IsWindow(m_hWnd));

		if (NULL == pItem)
			return -1;

		_ITEM item;
		item.pItem = pItem;
		item.nLen = nLen;
		item.nStartOffset = m_nCurOffset + nOffset;
		CRect rc;
		GetClientRect(&rc);
		_UpdateLayoutRect(&rc, item);
		m_nCurOffset += nOffset + nLen; // 更新偏移记录
		m_VecItems.push_back(item);
		return (m_VecItems.size() - 1);
	}

	void SetMargin(int nMargin1, int nMargin2)
	{
		m_nMargin1 = nMargin1;
		m_nMargin2 = nMargin2;
		if (::IsWindow(m_hWnd))
			_UpdateLayout();
	}

	void SetCallback(ITabHeaderCallback* p)
	{
		m_pCallback = p;
		if (::IsWindow(m_hWnd))
			Invalidate(FALSE);
	}

	void SetBackgroundColor(COLORREF clr)
	{
		m_clrBackground = clr;
		if (::IsWindow(m_hWnd))
			Invalidate(FALSE);
	}

	int GetCurrentSelected()
	{
		return m_nSelected;
	}

	void SetCurrentSelected(int nIndex)
	{
		if (nIndex < -1 || nIndex > ((int)m_VecItems.size() - 1))
			return;
		_DealSelection(nIndex);
	}

public:
	BOOL OnHover(POINT point)
	{
		m_nHover = _HitTest(point);
		if (-1 != m_nHover)
		{
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
		}
		Invalidate(FALSE);
		return TRUE;
	}

	BOOL OnHoverOut()
	{
		m_nHover = -1;
		Invalidate(FALSE);
		return TRUE;
	}

	BOOL OnPress(POINT point)
	{
		int nIndex = _HitTest(point);
		if (-1 != nIndex)
			_DealSelection(nIndex);
		return TRUE;
	}

private:
	void _Reset()
	{
		m_type = THT_HORZ;
		m_nCurOffset = 0;
		m_nHover = -1;
		m_nSelected = -1;
		m_VecItems.clear();
	}

	void _UpdateLayout()
	{
		CRect rc;
		GetClientRect(&rc);

		_VEC_ITEM_IT it = m_VecItems.begin();
		for (; it != m_VecItems.end(); ++it)
		{
			_UpdateLayoutRect(&rc, *it);
		}
		Invalidate(FALSE);
	}

	void _UpdateLayoutRect(LPCRECT pRcClient, _ITEM& item)
	{
		CRect rcItem(pRcClient);
		if (THT_HORZ == m_type)
		{
			rcItem.top += m_nMargin1;
			rcItem.bottom -= m_nMargin2;
			rcItem.left = item.nStartOffset;
			rcItem.right = rcItem.left + item.nLen;
		}
		else
		{
			ATLASSERT(THT_VERT == m_type);
			rcItem.left += m_nMargin1;
			rcItem.right -= m_nMargin2;
			rcItem.top = item.nStartOffset;
			rcItem.bottom = rcItem.top + item.nLen;
		}
		item.rc = rcItem;
	}

	int _HitTest(POINT pt)
	{
		int nRes = -1;
		int nSize = m_VecItems.size();
		for (int i = 0; i < nSize; ++i)
		{
			if (m_VecItems[i].rc.PtInRect(pt))
			{
				nRes = i;
				break;
			}
		}
		return nRes;
	}

	void _DealSelection(int nIndex)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		if (nIndex != m_nSelected)
		{
			// 选中项发生变化
			BOOL bChange = TRUE;
			if (-1 != m_nSelected)
				bChange = !_OnKillActive(m_nSelected);
			if (!bChange)
				return;
			if (-1 != m_nSelected && -1 != nIndex)
				_OnSwitchTab(m_nSelected, nIndex);
			if (-1 != nIndex)
				_OnActive(nIndex);

			m_nSelected = nIndex;
			Invalidate(FALSE);
		}
	}

	void _OnActive(int nIndex)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(GetParent(), WM_TAB_ACTIVE, (WPARAM)nIndex, (LPARAM)0);
	}

	BOOL _OnKillActive(int nIndex)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		return ::SendMessage(GetParent(), WM_TAB_KILLACTIVE, (WPARAM)nIndex, (LPARAM)0);
	}

	void _OnSwitchTab(int nIndex1, int nIndex2)
	{
		ATLASSERT(::IsWindow(m_hWnd));
		::SendMessage(GetParent(), WM_TAB_SWITCH, (WPARAM)nIndex1, (LPARAM)nIndex2);
	}

	void _DrawBackground(CDCHandle& dc)
	{
		CRect rc;
		GetClientRect(&rc);
		if (m_pCallback && m_pCallback->OnPreDrawTHBackground(m_hWnd, dc, &rc))
			return;
		// 默认绘制
		{
			if (CLR_NONE == m_clrBackground)
			{
				// 透明
				DrawParentBackground(this, dc);
			}
			else
				dc.FillSolidRect(&rc, m_clrBackground);
		}
		if (m_pCallback)
			m_pCallback->OnPostDrawTHBackground(m_hWnd, dc, &rc);
	}

private:
	TH_TYPE m_type;
	_VEC_ITEM m_VecItems;
	ITabHeaderCallback* m_pCallback;
	int m_nMargin1;
	int m_nMargin2;
	int m_nCurOffset;
	int m_nHover;
	int m_nSelected;
	COLORREF m_clrBackground;
	CFontHandle m_Font;
};

interface ITabControlCallback
{
	virtual ~ITabControlCallback(){}

	virtual BOOL OnPreDrawTCBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc) = 0; // 返回TRUE就不调用默认绘制
	virtual void OnPostDrawTCBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc) = 0;
	virtual BOOL OnDrawTCViewBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc) = 0;
};

class CSimpleTabControlCallback : public ITabControlCallback
{
public:
	virtual BOOL OnPreDrawTCBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc){return FALSE;}
	virtual void OnPostDrawTCBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc){}
	virtual BOOL OnDrawTCViewBackground(HWND hWnd, CDCHandle& dc, LPCRECT pRc){}
};

#define TCS_TOP 0x00000000
#define TCS_LEFT 0x00000001
#define TCS_BORDER 0x00010000
typedef DWORD TC_STYLE;

class CTabControl
	: public CWindowImpl<CTabControl, CWindow, CControlWinTraits>
	, public CDoubleBufferImpl<CTabControl>
	, public TransparentStatic
{
	typedef CWindowImpl<CTabControl, CWindow, CControlWinTraits> _base;
	

	typedef struct
	{
		HWND hWnd;
		LPVOID pData;
	} _TABPAGE;
	typedef std::map<int, _TABPAGE> _MAP_TABS;
	typedef _MAP_TABS::value_type _MAP_TABS_VAL;
	typedef _MAP_TABS::const_iterator _MAP_TABS_CONST_IT;
	typedef _MAP_TABS::iterator _MAP_TABS_IT;

public:
	CTabControl()
		: m_TcStyle(0)
		, m_nHeaderW(0)
		, m_clrBorder(RGB(125, 162, 206))
		, m_clrBackground(RGB(255, 255, 255))
		, m_pCallback(NULL)
		, m_bEnableSwitchEffect(FALSE)
	{
	}

	~CTabControl()
	{

	}

public:
	DECLARE_WND_CLASS(_T("TabControl"))

	BEGIN_MSG_MAP_EX(CTabControl)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_SIZE(OnSize)
		MSG_WM_SETFONT(OnSetFont)
		MSG_WM_GETFONT(OnGetFont)
		MESSAGE_HANDLER_EX(WM_TAB_ACTIVE, OnTabActive)
		MESSAGE_HANDLER_EX(WM_TAB_KILLACTIVE, OnTabKillActive)
		MESSAGE_HANDLER_EX(WM_TAB_SWITCH, OnTabSwitch)

		CHAIN_MSG_MAP(CDoubleBufferImpl<CTabControl>)
		FORWARD_NOTIFICATIONS()
	END_MSG_MAP()

public:
	int OnCreate(LPCREATESTRUCT lpCreateStruct)
	{
		if (!_CreateHeader())
			return -1;
		ModifyStyle(0, WS_TABSTOP);
		ModifyStyleEx(0, WS_EX_CONTROLPARENT);
		return 0;
	}

	void OnDestroy()
	{
		m_tabs.clear();
	}

	void OnSize(UINT nType, CSize size)
	{
		CRect rc;
		_GetClientRect(&rc);

		CRect rcHeader = _CalHeaderPosRect(&rc);
		m_TabHeader.SetWindowPos(NULL, &rcHeader, SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	void OnSetFont(CFontHandle font, BOOL bRedraw)
	{
		m_Font = font;
		if (bRedraw)
			Invalidate(FALSE);
	}

	HFONT OnGetFont()
	{
		return m_Font;
	}

	LRESULT OnTabActive(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		int nIndex = (int)wParam;
		_MAP_TABS_CONST_IT it = m_tabs.find(nIndex);
		if (it != m_tabs.end())
		{
			_ActivePage(*it);
		}
		return 0;
	}

	LRESULT OnTabKillActive(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		BOOL bRes = FALSE;
		int nIndex = (int)wParam;
		_MAP_TABS_CONST_IT it = m_tabs.find(nIndex);
		if (it != m_tabs.end())
		{
			bRes = _DeActivePage(*it);
		}
		return bRes;
	}

	LRESULT OnTabSwitch(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (m_bEnableSwitchEffect)
		{
			int nIndexFrom = (int)wParam;
			int nIndexTo = (int)lParam;
			_MAP_TABS_CONST_IT itFrom = m_tabs.find(nIndexFrom);
			ATLASSERT(itFrom != m_tabs.end());
			_MAP_TABS_CONST_IT itTo = m_tabs.find(nIndexTo);
			ATLASSERT(itTo != m_tabs.end());
			if (m_tabs.end() != itFrom && m_tabs.end() != itTo)
			{
				_PageSwitchEffect(itFrom->second.hWnd, itTo->second.hWnd, nIndexFrom < nIndexTo);
			}
		}
		return 0;
	}

	void DoPaint(CDCHandle dc)
	{
		HFONT hFontOld = dc.SelectFont(m_Font);

		if (m_TcStyle & TCS_BORDER)
			_DrawBorder(dc);
		
		// 绘制背景
		CRect rc;
		_GetClientRect(&rc);
		_DrawBackground(dc, &rc);

		CRect rcView;
		_GetViewRect(&rcView);
		_DrawViewBackground(dc, &rcView);

		dc.SelectFont(hFontOld);
	}

public:
	HWND Create(HWND hWndParent, ATL::_U_RECT rect = NULL, UINT nHeaderWidth = 0, TC_STYLE style = TCS_TOP | TCS_BORDER,
		LPCTSTR szWindowName = NULL, DWORD dwStyle = 0, DWORD dwExStyle = 0, LPVOID lpCreateParam = NULL)
	{
		m_nHeaderW = nHeaderWidth;
		m_TcStyle = style;
		return _base::Create(hWndParent, rect, szWindowName, dwStyle, dwExStyle, 0U, lpCreateParam);
	}

	void SetBorderColor(COLORREF clr)
	{
		m_clrBorder = clr;
		if (::IsWindow(m_hWnd))
			Invalidate(FALSE);
	}

	void SetBackgroundColor(COLORREF clr)
	{
		m_clrBackground = clr;
		if (::IsWindow(m_hWnd))
			Invalidate(FALSE);
	}

	void SetCallback(ITabControlCallback* p)
	{
		m_pCallback = p;
		if (::IsWindow(m_hWnd))
			Invalidate(FALSE);
	}

	void SetTabHeaderWidth(int nWidth)
	{
		ATLASSERT(::IsWindow(m_hWnd));

		if (m_nHeaderW != nWidth)
		{
			m_nHeaderW = nWidth;
			CRect rc;
			GetClientRect(&rc);
			ResizeClient(rc.Width(), rc.Height());
		}
	}

	int AddTab(int nOffset, int nLen, ITabItem* pItem, HWND hWndPage, LPVOID pData = NULL)
	{
		ATLASSERT(NULL != pItem && ::IsWindow(hWndPage));
		if (NULL == pItem || !::IsWindow(hWndPage))
			return -1;
		int nRes = m_TabHeader.AddItem(nOffset, nLen, pItem);
		if (nRes != -1)
		{
			// 添加成功			
			_InitWin(hWndPage);
			_TABPAGE page;
			page.hWnd = hWndPage;
			page.pData = pData;
			m_tabs[nRes] = page;
		}
		return nRes;
	}

	void SelectedTab(int nTab)
	{
		m_TabHeader.SetCurrentSelected(nTab);
	}

	CTabHeader* GetHeader()
	{
		return &m_TabHeader;
	}

	void BroadcastMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		_MAP_TABS_CONST_IT it = m_tabs.begin();
		for (; it != m_tabs.end(); ++it)
		{
			::SendMessage(it->second.hWnd, uMsg, wParam, lParam);
		}
	}

	void EnableSwitchEffect(BOOL bEnable)
	{
		m_bEnableSwitchEffect = bEnable;
	}

private:
	BOOL _CreateHeader()
	{
		CRect rc;
		_GetClientRect(&rc);

		TH_TYPE THType;
		if (m_TcStyle & TCS_LEFT)
			THType = THT_VERT;
		else
			THType = THT_HORZ; // default;
		CRect rcHeader = _CalHeaderPosRect(&rc);
		HWND hWndHeader = m_TabHeader.Create(m_hWnd, &rcHeader, THType);
		return (NULL != hWndHeader);
	}

	void _GetClientRect(LPRECT pRc)
	{
		CRect rc;
		GetClientRect(&rc);
		if (m_TcStyle & TCS_BORDER)
			rc.DeflateRect(1, 1);
		*pRc = rc;
	}

	void _GetViewRect(LPRECT pRc)
	{
		CRect rc;
		_GetClientRect(&rc);
		CRect rcHeader = _CalHeaderPosRect(&rc);
		::SubtractRect(pRc, &rc, &rcHeader);
	}

	CRect _CalHeaderPosRect(LPCRECT p)
	{
		CRect rc(*p);
		if (m_TcStyle & TCS_LEFT)
		{
			rc.right = rc.left + m_nHeaderW;
		}
		else
		{
			// 默认：TCS_TOP
			rc.bottom = rc.top + m_nHeaderW;
		}
		return rc;
	}

	void _DrawBorder(CDCHandle& dc)
	{
		CRect rc;
		GetClientRect(&rc);

		CPen pen;
		pen.CreatePen(PS_SOLID, 1, m_clrBorder);
		HPEN hPenOld = dc.SelectPen(pen);
		CPoint ptOld;
		dc.MoveTo(rc.left, rc.top, &ptOld);
		dc.LineTo(rc.right - 1, rc.top);
		dc.LineTo(rc.right - 1, rc.bottom - 1);
		dc.MoveTo(rc.left, rc.top + 1);
		dc.LineTo(rc.left, rc.bottom - 1);
		dc.LineTo(rc.right, rc.bottom - 1);
		dc.MoveTo(ptOld.x, ptOld.y);
		dc.SelectPen(hPenOld);
	}

	void _DrawBackground(CDCHandle& dc, LPCRECT pRc)
	{
		if (m_pCallback && m_pCallback->OnPreDrawTCBackground(m_hWnd, dc, pRc))
			return;

		// 默认绘制
		{
			if (CLR_NONE == m_clrBackground)
			{
				// 透明
				DrawParentBackground(this, dc, pRc);
			}
			else
				dc.FillSolidRect(pRc, m_clrBackground);
		}

		if (m_pCallback)
			m_pCallback->OnPostDrawTCBackground(m_hWnd, dc, pRc);
	}

	void _DrawViewBackground(CDCHandle& dc, LPCRECT pRc)
	{
		if (m_pCallback)
			m_pCallback->OnDrawTCViewBackground(m_hWnd, dc, pRc);
	}

	void _ActivePage(const _MAP_TABS_VAL& val)
	{
		int nIndex = val.first;
		const _TABPAGE& page = val.second;
		_UpdatePagePos(page.hWnd);
		::SendMessage(page.hWnd, WM_TAB_ACTIVE, (WPARAM)nIndex, (LPARAM)page.pData);
		::ShowWindow(page.hWnd, SW_SHOWNORMAL);
	}

	BOOL _DeActivePage(const _MAP_TABS_VAL& val)
	{
		int nIndex = val.first;
		const _TABPAGE& page = val.second;
		BOOL bRes = ::SendMessage(page.hWnd, WM_TAB_KILLACTIVE, (WPARAM)nIndex, (LPARAM)page.pData);
		if (!bRes)
			::ShowWindow(page.hWnd, SW_HIDE);
		return bRes;
	}

	void _UpdatePagePos(HWND hWnd)
	{
		CRect rcView;
		_GetViewRect(&rcView);

		CRect rcWin;
		CWindow(hWnd).GetWindowRect(&rcWin);
		ScreenToClient(&rcWin);

		if (!rcView.EqualRect(&rcWin))
		{
			// 窗口大小位置需要调整
			::MoveWindow(hWnd, rcView.left, rcView.top, rcView.Width(), rcView.Height(), TRUE);
		}
	}

	void _InitWin(HWND hWnd)
	{
		CWindow wnd(hWnd);
		wnd.ShowWindow(SW_HIDE);
		wnd.SetParent(m_hWnd);
		wnd.ModifyStyle(0, DS_CONTROL | WS_CLIPCHILDREN);
		wnd.ModifyStyleEx(0, WS_EX_CONTROLPARENT);
	}

	void _PageSwitchEffect(HWND hWndFrom, HWND hWndTo, BOOL bPositive)
	{
		_UpdatePagePos(hWndFrom);
		_UpdatePagePos(hWndTo);

		CRect rcView;
		_GetViewRect(&rcView);

		CWndEffect e;
		e.Slide(m_hWnd, &rcView, 150, hWndFrom, hWndTo, bPositive ? WSSlideLeft : WSSlideRight);
	}

private:
	CTabHeader m_TabHeader;
	_MAP_TABS m_tabs;
	TC_STYLE m_TcStyle;
	UINT m_nHeaderW;
	COLORREF m_clrBorder;
	COLORREF m_clrBackground;
	ITabControlCallback* m_pCallback;
	CFontHandle m_Font;
	BOOL m_bEnableSwitchEffect;
};
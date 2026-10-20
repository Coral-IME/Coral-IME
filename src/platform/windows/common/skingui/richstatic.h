#ifndef __RICHSTATIC_H__
#define __RICHSTATIC_H__

#include "textdrawer.h"
#include "transparent.h"


// 对齐方式
typedef enum
{
	RS_LEFT = 0x01,
	RS_RIGHT = 0x02,
	RS_CENTRE = 0x04,

	RS_VCENTRE = 0x10,
	RS_TOP = 0x20,
	RS_BOTTOM = 0x40,

	RS_DEFAULT = RS_LEFT | RS_VCENTRE,
} RS_TYPE;

class CRichStatic
	: public CWindowImpl<CRichStatic, CStaticT<CWindow>>
	, public CDoubleBufferImpl<CRichStatic>
	, public TransparentStatic
{
	typedef CWindowImpl<CRichStatic, CStaticT<CWindow>> _base;

public:
	BEGIN_MSG_MAP_EX(CRichStatic)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_RBUTTONDOWN(OnRButtonDown)
		MESSAGE_RANGE_HANDLER_EX(WM_MOUSEFIRST, WM_MOUSELAST, OnToolTipEvent)
		CHAIN_MSG_MAP(CDoubleBufferImpl<CRichStatic>)
	END_MSG_MAP()

	void OnRButtonDown(UINT nFlags, CPoint point)
	{
		::SendMessage(GetParent(), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), WM_RBUTTONDOWN), (LPARAM)m_hWnd);
	}

	void OnMouseMove(UINT nFlags, CPoint point)
	{
		::SendMessage(GetParent(), WM_MOUSEMOVE, nFlags, MAKELPARAM(point.x, point.y));
	}

	int OnCreate(LPCREATESTRUCT lpCreateStruct)
	{
		_Init();
		SetMsgHandled(FALSE);
		return TRUE;
	}

	void OnDestroy()
	{
		_UnInit();
		SetMsgHandled(FALSE);
	}

	LRESULT OnToolTipEvent(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if(m_tip.IsWindow())
		{
			MSG msg = { m_hWnd, uMsg, wParam, lParam };
			m_tip.RelayEvent(&msg);
		}

		SetMsgHandled(FALSE);
		return 0;
	}

	BOOL SubclassWindow(HWND hWnd)
	{
		BOOL bRet = _base::SubclassWindow(hWnd);
		if (bRet)
			_Init();
		return bRet;
	}
public:
	CRichStatic()
		: m_clrBg(COLOR_NONE)
		, m_nAlign(RS_DEFAULT)
		, m_nLineIndent(_TD_DEF_INDENT_LINE)
	{}

	void SetText(LPCTSTR szText, int nAlign = RS_DEFAULT, int nLineIndent = _TD_DEF_INDENT_LINE)
	{
		ATLASSERT(szText);
		m_strText = szText;
		m_nAlign = nAlign;
		m_nLineIndent = nLineIndent;
		_Refresh();
	}

	BOOL SetTooltipText(LPCTSTR szText)
	{
		ATLASSERT(IsWindow());
		ATLASSERT(m_tip.IsWindow());
		if (!m_tip.IsWindow())
			return FALSE;

		if (szText == NULL || *szText == 0)
		{
			m_tip.Activate(FALSE);
		}
		else
		{
			m_tip.Activate(TRUE);
			m_tip.UpdateTipText(szText, m_hWnd);
		}
		return TRUE;
	}

	CString GetText()
	{
		return m_strText;
	}

	void SetBackgroundColor(COLORREF clr)
	{
		m_clrBg = clr;
	}

	void DoPaint(CDCHandle dc)
	{
		if (m_clrBg == COLOR_NONE)
		{
			// 透明
			DrawParentBackground(this, dc);
		}
		else
		{
			CRect rc;
			GetClientRect(&rc);
			dc.FillSolidRect(&rc, m_clrBg);
		}
		_DrawText(dc);
	}

private:
	void _Init()
	{
		// 直接杀explorer，云桥界面是收不到destroy消息的,所以这里做一个容错
		if (m_tip.IsWindow())
			m_tip.DestroyWindow();
		m_tip.m_hWnd = NULL;

		m_tip.Create(m_hWnd);
		ATLASSERT(m_tip.IsWindow());
		CToolInfo ti(0, m_hWnd);
		m_tip.Activate(TRUE);
		m_tip.AddTool(&ti);
		m_tip.SetMaxTipWidth(300);

		ModifyStyle(0, SS_NOTIFY);
	}

	void _UnInit()
	{
		if (m_tip.IsWindow())
			m_tip.DestroyWindow();
		m_tip.m_hWnd = NULL;
	}
	void _Refresh()
	{
		if (IsWindow())
			Invalidate(TRUE);
	}

	void _DrawText(CDCHandle dc)
	{
		CRect rc, rcText;
		GetClientRect(&rc);
		rcText = rc;

		HFONT hOldFont = dc.SelectFont(GetFont());
		CTextDrawer drawer;
		drawer.CalculateDrawRect(dc, m_strText, &rcText);

		if (m_nAlign & RS_LEFT)
			rc.left = 0;
		else if (m_nAlign & RS_RIGHT)
			rc.left = rc.Width() - rcText.Width();
		else if (m_nAlign & RS_CENTRE)
			rc.left = (rc.Width() - rcText.Width()) / 2;
		else
			ATLASSERT(FALSE);

		if (m_nAlign & RS_TOP)
			rc.top = 0;
		else if (m_nAlign & RS_BOTTOM)
			rc.top = rc.Height() - rcText.Height();
		else if (m_nAlign & RS_VCENTRE)
			rc.top = (rc.Height() - rcText.Height()) / 2;
		else
			ATLASSERT(FALSE);

		drawer.Draw(dc, m_strText, rc, _TD_DEF_INDENT_CHAR, m_nLineIndent);
		dc.SelectFont(hOldFont);
	}

private:
	CString m_strText;
	COLORREF m_clrBg;
	int m_nAlign;
	int m_nLineIndent;

	CToolTipCtrl m_tip;
};

#endif
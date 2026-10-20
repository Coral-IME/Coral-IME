#pragma once
#include "textdrawer.h"
#include "skingui/skingdi.h"
#include "skingui/gdiplusbitmap.h"
#include "transparent.h"

#define BN_RICH_LBUTTOMUP (WM_USER + 500)
#define BN_ID(id) (BN_RICH_LBUTTOMUP + id)
#define PNG_NONE 0xffffffff
class CRichButton
	: public CWindowImpl<CRichButton, CStatic>
	, public CDoubleBufferImpl<CRichButton>
	, public TransparentStatic
{
	typedef CWindowImpl<CRichButton, CStatic> _base;
	struct BORDER
	{
		int nWidth;
		COLORREF clr;
	};

	struct BG_COLOR
	{
		COLORREF clrBg;
		COLORREF GradientBg;
	};

public:
	CRichButton()
	{
		m_hModule = NULL;
		m_BgColor.clrBg = COLOR_NONE;
		m_BgColor.GradientBg = COLOR_NONE;
		m_Border.nWidth = 0;
		m_Border.clr = RGB(0, 0, 0);

		m_HoverBgColor.clrBg = RGB(253, 254, 255);
		m_HoverBgColor.GradientBg = RGB(244, 250, 255);
		m_HoverBorder.nWidth = 4;
		m_bHover = FALSE;

		m_nLeftMargin = 1;
		m_nMarginBetweenPicAndText = -1;
		m_bHorizontal = TRUE;
		m_nCharMargin = 0;
		m_nLineMargin = 1;

		m_bPress = FALSE;
		m_bDisableHover = FALSE;

		m_bUseHoverText = FALSE;
		m_nStatus = -1;
	}

	void SetModuleInstance(HMODULE hModule)
	{
		m_hModule = hModule;
	}

	void SetCharMargin(int nMargin)
	{
		m_nCharMargin = nMargin;
	}

	void SetLineMargin(int nMargin)
	{
		m_nLineMargin = nMargin;
	}

	void SetPngTextLinage(BOOL bHorizontal)
	{
		m_bHorizontal = bHorizontal;
	}

	// 要在父窗口实现WM_CTLCOLORSTATIC，把背景刷掉, 否则会看到闪烁
	void SetContent(LPCTSTR szText, UINT pngId = PNG_NONE, int nStatus = -1)
	{
		if (pngId != PNG_NONE)
		{
			m_NormalPicture.Empty();
			m_NormalPicture.Load(pngId, _T("PNG"), m_hModule);
		}
		m_strText = szText;
		m_bUseHoverText = FALSE;
		m_nStatus = nStatus;
		_Refresh();
	}

	void SetContentEx(LPCTSTR szText, LPCTSTR szHoverText, UINT pngId = PNG_NONE)
	{
		if (pngId != PNG_NONE)
		{
			m_NormalPicture.Empty();
			m_NormalPicture.Load(pngId, _T("PNG"), m_hModule);
		}
		m_strText = szText;
		m_strHoverText = szHoverText;
		m_bUseHoverText = TRUE;
		_Refresh();
	}
	void SetPng(UINT pngId, BOOL bRepaint = TRUE)
	{
		if (pngId != PNG_NONE)
		{
			m_NormalPicture.Empty();
			m_NormalPicture.Load(pngId, _T("PNG"), m_hModule);
		}
		if (bRepaint)
			_Refresh();
	}

	void SetHoverBgPng(UINT pngId)
	{
		m_HoverBgPicture.Empty();
		m_HoverBgPicture.Load(pngId, _T("PNG"), m_hModule);
	}

	void SetPressBgPng(UINT pngId)
	{
		m_PressBgPicture.Empty();
		m_PressBgPicture.Load(pngId, _T("PNG"), m_hModule);
	}

	void DisableHover()
	{
		m_bDisableHover = TRUE;
	}

	void SetBgColor(COLORREF bgClr, COLORREF bgGradient = COLOR_NONE)
	{
		m_BgColor.clrBg = bgClr;
		m_BgColor.GradientBg = bgGradient;
	}

	void SetBorder(int nWidth, COLORREF clr = COLOR_NONE)
	{
		m_Border.nWidth = nWidth;
		m_Border.clr = clr;
	}

	void SetHoverBgColor(COLORREF clr, COLORREF clrGradient = COLOR_NONE)
	{
		m_HoverBgColor.clrBg = clr;
		m_HoverBgColor.GradientBg = clrGradient;
	}

	void SetHoverBorder(int nWidth, COLORREF clr = COLOR_NONE)
	{
		m_HoverBorder.nWidth = nWidth;
		m_HoverBorder.clr = clr;
	}

	void SetMargin(int nLeftMargin, int nMarginBetweenPicAndText = -1)
	{
		m_nLeftMargin = nLeftMargin;
		m_nMarginBetweenPicAndText = nMarginBetweenPicAndText;
	}

	void GetWindowText(CString& rString) const
	{
		ATLASSERT(FALSE);
	}

	void SetWindowText(LPCTSTR lpszString)
	{
		ATLASSERT(FALSE);
	}

public:
	BEGIN_MSG_MAP_EX(CRichButton)
		MSG_WM_CREATE(OnCreate)
		MSG_WM_SETCURSOR(OnSetCursor)
		MSG_WM_MOUSEMOVE(OnMouseMove)
		MSG_WM_MOUSELEAVE(OnMouseLeave)
		MSG_WM_DESTROY(OnDestroy)
		MSG_WM_LBUTTONDOWN(OnLButtonDown)
		MSG_WM_LBUTTONUP(OnLButtonUp)
		// MESSAGE_HANDLER_EX(WM_SETFOCUS, OnNoRedrawByDefWindowProc)
		// MESSAGE_HANDLER_EX(WM_KILLFOCUS, OnNoRedrawByDefWindowProc)
		// MESSAGE_HANDLER_EX(WM_GETFONT, OnNoRedrawByDefWindowProc)

		CHAIN_MSG_MAP(CDoubleBufferImpl<CRichButton>)
		END_MSG_MAP()

	BOOL SubclassWindow(HWND hWnd)
	{
		BOOL bRes = _base::SubclassWindow(hWnd);
		if (bRes)
			_Init();
		return bRes;
	}

	int OnCreate(LPCREATESTRUCT lpCreateStruct)
	{
		_Init();
		SetMsgHandled(FALSE);
		return 0;
	}

	BOOL OnSetCursor(CWindow wnd, UINT nHitTest, UINT message)
	{
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		return TRUE;
	}

	void OnMouseMove(UINT nFlags, CPoint point)
	{
		if (m_bHover == TRUE)
			return;

		m_bHover = TRUE;
		_Refresh();

		TRACKMOUSEEVENT MouseEvent = { 0 };
		MouseEvent.cbSize = sizeof(TRACKMOUSEEVENT);
		MouseEvent.dwFlags = TME_LEAVE;
		MouseEvent.hwndTrack = m_hWnd;
		_TrackMouseEvent(&MouseEvent);
	}

	void OnMouseLeave()
	{
		m_bHover = FALSE;
		m_bPress = FALSE;
		_Refresh();
	}

	void OnLButtonDown(UINT nFlags, CPoint point)
	{
		m_bPress = TRUE;
		_Refresh();
	}

	void OnLButtonUp(UINT nFlags, CPoint point)
	{
		// WM_LBUTTONDBLCLK会产生4个消息：LBUTTONDOWN，LBUTTONUP，LBUTTONDBCLK，LBUTTONUP
		// 这里判断m_bPress，是为了屏蔽左键双击时出现两次LBUTTONUP
		if (!m_bPress)
			return;

		m_bPress = FALSE;
		CRect rc;
		GetClientRect(&rc);
		if (rc.PtInRect(point))
		{
			int id = GetDlgCtrlID();
			GetParent().PostMessage(BN_ID(id), (WPARAM)m_nStatus, (LPARAM)m_hWnd);
		}
		_Refresh();
		if (::GetCapture() == m_hWnd)
			::ReleaseCapture();
	}

	// LRESULT OnNoRedrawByDefWindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
	// 	{
	// SetRedraw(FALSE);
	// LRESULT lr = DefWindowProc();
	// SetRedraw(TRUE);
	// 		Invalidate();
	// return lr;
	// 	}

	BOOL EnableWindow(BOOL bEnable = TRUE)
	{
		SetRedraw(FALSE);
		BOOL bRet = _base::EnableWindow(bEnable);
		SetRedraw(TRUE);
		Invalidate();
		return bRet;
	}

	void OnDestroy()
	{
		m_PressBgPicture.Empty();
		m_HoverBgPicture.Empty();
		m_NormalPicture.Empty();
	}

	void DoPaint(CDCHandle dc)
	{
		if (m_HoverBgPicture.m_pBitmap)
		{
			_DrawBgPng(dc);
		}
		else
		{
			_DrawBg(dc);
			_DrawBorder(dc);
		}
		if (m_bHorizontal)
			_DrawHorizontalContent(dc);
		else
			_DrawVerticalContent(dc);
	}

private:
	void _DrawBg(CDCHandle& dc)
	{
		BG_COLOR bgColor = (m_bHover && !m_bDisableHover) ? m_HoverBgColor : m_BgColor;
		if (bgColor.clrBg == COLOR_NONE)
		{
			DrawParentBackground(this, dc);
			return;
		}

		CRect rc;
		GetClientRect(&rc);
		if (bgColor.GradientBg == COLOR_NONE)
		{
			dc.FillSolidRect(&rc, bgColor.clrBg);
			return;
		}

		CSkinDCHandle dc3D(dc);
		dc3D.SkinDrawGradualColorRect(rc, bgColor.clrBg, bgColor.GradientBg);
	}

	void _DrawBgPng(CDCHandle& dc)
	{
		CRect rc;
		GetClientRect(&rc);

		if (!m_bHover || m_bDisableHover)
		{
			if (m_BgColor.clrBg == COLOR_NONE)
			{
				DrawParentBackground(this, dc);
				return;
			}

			if (m_BgColor.GradientBg == COLOR_NONE)
			{
				dc.FillSolidRect(&rc, m_BgColor.clrBg);
				return;
			}

			CSkinDCHandle dc3D(dc);
			dc3D.SkinDrawGradualColorRect(rc, m_BgColor.clrBg, m_BgColor.GradientBg);
		}
		else
		{
			Gdiplus::Bitmap* pImage = NULL;
			if (m_bPress && m_PressBgPicture.m_pBitmap)
			{
				pImage = m_PressBgPicture;
			}
			else
			{
				ATLASSERT(m_HoverBgPicture.m_pBitmap);
				pImage = m_HoverBgPicture;
			}

			int nTop = 0;
			if (rc.Height() > (int)pImage->GetHeight())
				nTop = (rc.Height() - pImage->GetHeight()) / 2;

			int nLeft = 0;
			if (rc.Width() > (int)pImage->GetWidth())
				nLeft = (rc.Width() - pImage->GetWidth()) / 2;

			Gdiplus::Graphics g(dc);
			g.DrawImage(pImage, nLeft, nTop, pImage->GetWidth(), pImage->GetHeight());
		}
	}

	void _DrawBorder(CDCHandle& dc)
	{
		CRect rc;
		GetClientRect(&rc);
		CSkinDCHandle dc3D(dc);

		BORDER border = (m_bHover && !m_bDisableHover) ? m_HoverBorder : m_Border;
		if (border.nWidth == 0)
			return;

		dc3D.SkinLine(CPoint(rc.left, rc.top), CPoint(rc.right, rc.top), border.clr, BS_SOLID, border.nWidth);
		dc3D.SkinLine(CPoint(rc.right, rc.top), CPoint(rc.right, rc.bottom), border.clr, BS_SOLID, border.nWidth);
		dc3D.SkinLine(CPoint(rc.right, rc.bottom), CPoint(rc.left, rc.bottom), border.clr, BS_SOLID, border.nWidth);
		dc3D.SkinLine(CPoint(rc.left, rc.bottom), CPoint(rc.left, rc.top), border.clr, BS_SOLID, border.nWidth);
	}

	void _DrawHorizontalContent(CDCHandle& dc)
	{
		CRect rc, rcText;
		GetClientRect(&rc);
		CString strText;
		if (m_bUseHoverText && m_bHover && !m_bDisableHover)
			strText = m_strHoverText;
		else
			strText = m_strText;

		rcText = rc;
		if (strText.IsEmpty() && m_NormalPicture.m_pBitmap == NULL)
			return;

		if (m_NormalPicture.m_pBitmap)
		{
			Gdiplus::Bitmap* pImage = m_NormalPicture;
			Gdiplus::Graphics g(dc);
			int ntop = (rc.Height() - pImage->GetHeight()) / 2;
			g.DrawImage(pImage, rc.left + m_nLeftMargin, ntop, pImage->GetWidth(), pImage->GetHeight());

			int nLeftMargin;
			if (m_nMarginBetweenPicAndText == -1)
				nLeftMargin = m_nLeftMargin;
			else
				nLeftMargin = m_nMarginBetweenPicAndText;

			rc.left += pImage->GetWidth() + m_nLeftMargin + nLeftMargin;
		}

		if (!strText.IsEmpty())
		{
			HFONT hOldFont = dc.SelectFont(GetFont());
			CTextDrawer drawer;
			drawer.CalculateDrawRect(dc, strText, &rcText, m_nCharMargin, m_nLineMargin);
			rc.top = (rc.Height() - rcText.Height()) / 2;
			drawer.Draw(dc, strText, rc, m_nCharMargin, m_nLineMargin);
			dc.SelectFont(hOldFont);
		}
	}

	// 垂直绘制，忽略左边距参数，直接居中对齐
	void _DrawVerticalContent(CDCHandle& dc)
	{
		CRect rc, rcText;
		GetClientRect(&rc);
		CString strText;
		if (m_bUseHoverText && m_bHover && !m_bDisableHover)
			strText = m_strHoverText;
		else
			strText = m_strText;

		if (strText.IsEmpty() && m_NormalPicture.m_pBitmap == NULL)
			return;

		rcText = rc;
		HFONT hOldFont = dc.SelectFont(GetFont());
		CTextDrawer drawer;
		drawer.CalculateDrawRect(dc, strText, &rcText, m_nCharMargin, m_nLineMargin);

		ATLASSERT(m_NormalPicture.m_pBitmap);
		Gdiplus::Bitmap* pImage = m_NormalPicture;
		int nImageHeight = pImage->GetHeight();
		int ntop = 0;
		if (m_nMarginBetweenPicAndText != -1)
			ntop = (rc.Height() - (nImageHeight + rcText.Height() + m_nMarginBetweenPicAndText)) / 2;
		else
			ntop = (rc.Height() - (nImageHeight + rcText.Height() + m_nLeftMargin)) / 2;

		// 绘制图片
		{
			Gdiplus::Graphics g(dc);
			int nLeft = (rc.Width() - pImage->GetWidth()) / 2;
			g.DrawImage(pImage, nLeft, ntop, pImage->GetWidth(), pImage->GetHeight());
		}

		// 绘制文字
		if (!strText.IsEmpty())
		{
			rc.left = (rc.Width() - rcText.Width()) / 2;
			if (m_nMarginBetweenPicAndText != -1)
				rc.top = ntop + pImage->GetHeight() + m_nMarginBetweenPicAndText;
			else
				rc.top = ntop + pImage->GetHeight() + m_nLeftMargin;
			drawer.Draw(dc, strText, rc, m_nCharMargin, m_nLineMargin);
			dc.SelectFont(hOldFont);
		}
	}

private:
	void _Refresh()
	{
		if (IsWindow())
			Invalidate(TRUE);
	}

	void _Init()
	{
		ModifyStyle(0, SS_NOTIFY);
	}

private:
	HMODULE m_hModule;
	int m_nCharMargin;
	int m_nLineMargin;
	int m_nLeftMargin;
	int m_nMarginBetweenPicAndText;
	BOOL m_bHorizontal;
	CString m_strText;
	BG_COLOR m_BgColor;
	BORDER m_Border;
	CString m_strHoverText;
	BOOL m_bUseHoverText;

	BOOL m_bHover;
	BG_COLOR m_HoverBgColor;
	BORDER m_HoverBorder;
	BOOL m_bDisableHover;

	BOOL m_bPress;
	CGdiPlusBitmapResource m_HoverBgPicture;
	CGdiPlusBitmapResource m_PressBgPicture;
	CGdiPlusBitmapResource m_NormalPicture;
	int m_nStatus;
};
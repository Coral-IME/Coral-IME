#include "pch.h"
#include "wndeffect.h"

#define defFPS 100
#define min(a,b) (((a) < (b)) ? (a) : (b))

void CWndEffect::_PrintWindow( HWND hWnd, CBitmap& bmp )
{
	CWindow wnd(hWnd);
	CRect rc;
	wnd.GetWindowRect(&rc);

	// 创建bitmap
	CClientDC dc(hWnd);
	bmp.CreateCompatibleBitmap(dc, rc.Width(), rc.Height());
	ATLASSERT(bmp.m_hBitmap != NULL);

	// 创建临时dc
	CDC dcTemp;
	dcTemp.CreateCompatibleDC(dc);
	HBITMAP hBmpOld = dcTemp.SelectBitmap(bmp);

	// print
	wnd.Print(dcTemp, PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);

	dcTemp.SelectBitmap(hBmpOld);
}

void CWndEffect::Slide( HWND hWndCanvas, LPCRECT pRect, DWORD dwTime, HWND hWndPrev, HWND hWndNext, WESlideDirection direction )
{
	if (dwTime <= 0)
		return;

	CClientDC dc(hWndCanvas);
	CRect rcPaint(pRect);
	CDC dcTemp;
	CDC dcAnimation;
	dcTemp.CreateCompatibleDC(dc);
	dcAnimation.CreateCompatibleDC(dc);
	CBitmap bmpAnimation;
	bmpAnimation.CreateCompatibleBitmap(dc, rcPaint.Width(), rcPaint.Height());
	HBITMAP bmpAniOld = dcAnimation.SelectBitmap(bmpAnimation);

	// 获取窗口截图
	CBitmap bmpWinPrev;
	CBitmap bmpWinNext;
	_PrintWindow(hWndPrev, bmpWinPrev);
	_PrintWindow(hWndNext, bmpWinNext);

	HBITMAP hBmpTempOld = dcTemp.SelectBitmap(bmpWinPrev);
	DWORD dwStartTime = GetTickCount();
	for (;;)
	{
		DWORD dwTimeNow = GetTickCount();
		double db = 0;
		db = min(((double)(dwTimeNow - dwStartTime) / dwTime), 1.0);
		int nOffset = (int)(rcPaint.Width() * db);

		// 合成图片
		if (WSSlideLeft == direction)
		{
			dcTemp.SelectBitmap(bmpWinPrev);
			dcAnimation.BitBlt(0, 0, rcPaint.Width() - nOffset, rcPaint.Height(),
				dcTemp, nOffset, 0, SRCCOPY);
			dcTemp.SelectBitmap(bmpWinNext);
			dcAnimation.BitBlt(rcPaint.Width() - nOffset, 0, nOffset, rcPaint.Height(),
				dcTemp, 0, 0, SRCCOPY);
		}
		else if (WSSlideRight == direction)
		{
			dcTemp.SelectBitmap(bmpWinPrev);
			dcAnimation.BitBlt(nOffset, 0, rcPaint.Width() - nOffset, rcPaint.Height(),
				dcTemp, 0, 0, SRCCOPY);
			dcTemp.SelectBitmap(bmpWinNext);
			dcAnimation.BitBlt(0, 0, nOffset, rcPaint.Height(),
				dcTemp, rcPaint.Width() - nOffset, 0, SRCCOPY);
		}

		// 绘制到画板窗口
		dc.BitBlt(rcPaint.left, rcPaint.top, rcPaint.Width(), rcPaint.Height(),
			dcAnimation, 0, 0, SRCCOPY);

		if (dwTimeNow - dwStartTime >= dwTime)
			break;
		::Sleep(m_nSleep);
	}

	dcTemp.SelectBitmap(hBmpTempOld);
	dcAnimation.SelectBitmap(bmpAniOld);
}

CWndEffect::CWndEffect()
: m_nFPS(defFPS)
{
	_UpdateSleepValue();
}

void CWndEffect::SetFPS( int nFPS )
{
	ATLASSERT(nFPS > 0);
	if (nFPS <= 0)
		return;

	m_nFPS = nFPS;
	_UpdateSleepValue();
}

void CWndEffect::_UpdateSleepValue()
{
	ATLASSERT(m_nFPS > 0);
	m_nSleep = 1000 / m_nFPS;
}

void CWndEffect::Fade( HWND hWndCanvas, LPCRECT pRect, DWORD dwTime, HWND hWndPrev, HWND hWndNext )
{
	if (dwTime <= 0)
		return;

	CClientDC dc(hWndCanvas);
	CRect rcPaint(pRect);
	CDC dcTemp;
	CDC dcAnimation;
	dcTemp.CreateCompatibleDC(dc);
	dcAnimation.CreateCompatibleDC(dc);
	CBitmap bmpAnimation;
	bmpAnimation.CreateCompatibleBitmap(dc, rcPaint.Width(), rcPaint.Height());
	HBITMAP bmpAniOld = dcAnimation.SelectBitmap(bmpAnimation);

	// 获取窗口截图
	CBitmap bmpPrev;
	CBitmap bmpNext;
	_PrintWindow(hWndPrev, bmpPrev);
	_PrintWindow(hWndNext, bmpNext);

	HBITMAP hBmpTempOld = dcTemp.SelectBitmap(bmpPrev);
	BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0, 0};
	DWORD dwStartTime = ::GetTickCount();
	for (;;)
	{
		DWORD dwTimeNow = GetTickCount();
		double db = 0;
		db = min(((double)(dwTimeNow - dwStartTime) / dwTime), 1.0);
		bf.SourceConstantAlpha = (BYTE)(db * 255);

		dcTemp.SelectBitmap(bmpPrev);
		dcAnimation.BitBlt(0, 0, rcPaint.Width(), rcPaint.Height(), dcTemp, 0, 0, SRCCOPY);
		dcTemp.SelectBitmap(bmpNext);
		dcAnimation.AlphaBlend(0, 0, rcPaint.Width(), rcPaint.Height(),
			dcTemp, 0, 0, rcPaint.Width(), rcPaint.Height(), bf);

		// 绘制到画板窗口
		dc.BitBlt(rcPaint.left, rcPaint.top, rcPaint.Width(), rcPaint.Height(),
			dcAnimation, 0, 0, SRCCOPY);

		if (dwTimeNow - dwStartTime >= dwTime)
			break;
		::Sleep(m_nSleep);
	}
	dcTemp.SelectBitmap(hBmpTempOld);
	dcAnimation.SelectBitmap(bmpAniOld);
}

void CWndEffect::_CaptureWindow( HWND hWnd, LPCRECT pRect, CBitmap& bmp )
{
	CClientDC dc(hWnd);
	CRect rc;
	if (NULL == pRect)
		::GetClientRect(hWnd, &rc);
	else
		rc = *pRect;

	// 创建bitmap
	bmp.CreateCompatibleBitmap(dc, rc.Width(), rc.Height());
	ATLASSERT(bmp.m_hBitmap != NULL);

	// 创建临时dc
	CDC dcTemp;
	dcTemp.CreateCompatibleDC(dc);
	HBITMAP hBmpOld = dcTemp.SelectBitmap(bmp);

	// capture
	dcTemp.BitBlt(0, 0, rc.Width(), rc.Height(), dc, rc.left, rc.top, SRCCOPY);

	dcTemp.SelectBitmap(hBmpOld);
}

Gdiplus::Bitmap* CWndEffect::_CaptureWindow( HWND hWnd, LPCRECT pRect )
{
	CRect rc(pRect);
	Gdiplus::Graphics graphics(hWnd);
	Gdiplus::Bitmap* pBmp = new Gdiplus::Bitmap(rc.Width(), rc.Height(), &graphics);
	ATLASSERT(pBmp);
	if (!pBmp)
		return NULL;
	Gdiplus::Graphics gBmp(pBmp);
	HDC hDC = gBmp.GetHDC();
	::BitBlt(hDC, 0, 0, rc.Width(), rc.Height(), CClientDC(hWnd), rc.left, rc.top, SRCCOPY);
	gBmp.ReleaseHDC(hDC);
	return pBmp;
}

void CWndEffect::Flash( HWND hWnd, LPCRECT pRect, const Gdiplus::Color& clrFlash, DWORD dwTime )
{
	if (dwTime <= 0)
		return;

	CRect rcPaint(pRect);
	Gdiplus::Bitmap* pBmpWnd = _CaptureWindow(hWnd, pRect);
	ATLASSERT(pBmpWnd != NULL);
	Gdiplus::Graphics g(hWnd);
	Gdiplus::Bitmap* pBmpOverlay = new Gdiplus::Bitmap(rcPaint.Width(), rcPaint.Height(), &g);
	ATLASSERT(pBmpOverlay != NULL);
	{
		Gdiplus::Graphics gOverlay(pBmpOverlay);
		gOverlay.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		gOverlay.DrawImage(pBmpWnd, 0, 0);
		gOverlay.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
		Gdiplus::SolidBrush brushOverlay(clrFlash);
		gOverlay.FillRectangle(&brushOverlay, 0, 0, rcPaint.Width(), rcPaint.Height());
	}

	BOOL bOverlay = TRUE;
	DWORD dwStartTime = ::GetTickCount();
	for (;;)
	{
		DWORD dwTimeNow = GetTickCount();
 		if (bOverlay)
 			g.DrawImage(pBmpOverlay, rcPaint.left, rcPaint.top, rcPaint.Width(), rcPaint.Height());
 		else
			g.DrawImage(pBmpWnd, rcPaint.left, rcPaint.top, rcPaint.Width(), rcPaint.Height());
		bOverlay = !bOverlay;
		if (dwTimeNow - dwStartTime >= dwTime)
			break;
		::Sleep(m_nSleep);
	}
	delete pBmpWnd;
	delete pBmpOverlay;
	::InvalidateRect(hWnd, pRect, TRUE);
}

void CWndEffect::FadeOut( HWND hWnd, LPCRECT pRect, COLORREF clrBackground, DWORD dwTime )
{
	if (dwTime <= 0)
		return;

	CRect rcPaint(pRect);
	Gdiplus::Graphics gWnd(hWnd);
	gWnd.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
	Gdiplus::Bitmap* pBmpWnd = _CaptureWindow(hWnd, pRect);
	ATLASSERT(pBmpWnd != NULL);
	Gdiplus::Bitmap* pBmpAni = new Gdiplus::Bitmap(rcPaint.Width(), rcPaint.Height(), &gWnd);
	ATLASSERT(pBmpAni != NULL);

	Gdiplus::Color colorBackground;
	colorBackground.SetFromCOLORREF(clrBackground);
	Gdiplus::Graphics gAni(pBmpAni);
	Gdiplus::ColorMatrix colorMatrix = {
		1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.8f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
	DWORD dwStartTime = ::GetTickCount();
	for (;;)
	{
		DWORD dwTimeNow = GetTickCount();
		float db = 1 - min(((float)(dwTimeNow - dwStartTime) / dwTime), 1.0f);

		gAni.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		Gdiplus::SolidBrush br(colorBackground);
		gAni.FillRectangle(&br, 0, 0, rcPaint.Width(), rcPaint.Height());

		gAni.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
		colorMatrix.m[3][3] = db;
		Gdiplus::ImageAttributes attr;
		attr.SetColorMatrix(&colorMatrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
		gAni.DrawImage(pBmpWnd, Gdiplus::Rect(0, 0, rcPaint.Width(), rcPaint.Height()),
			0, 0, rcPaint.Width(), rcPaint.Height(), Gdiplus::UnitPixel, &attr);

		gWnd.DrawImage(pBmpAni, rcPaint.left, rcPaint.top, rcPaint.Width(), rcPaint.Height());
		if (dwTimeNow - dwStartTime >= dwTime)
			break;
		::Sleep(m_nSleep);
	}
	delete pBmpAni;
	delete pBmpWnd;
	::InvalidateRect(hWnd, pRect, TRUE);
}

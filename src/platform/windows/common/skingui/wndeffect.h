#pragma once

typedef enum
{
	WSSlideLeft,
	WSSlideRight,
} WESlideDirection;

class CWndEffect
{
public:
	CWndEffect();

public:
	void SetFPS(int nFPS);
	void Slide(HWND hWndCanvas, LPCRECT pRect, DWORD dwTime, HWND hWndPrev, HWND hWndNext, WESlideDirection direction);
	void Fade(HWND hWndCanvas, LPCRECT pRect, DWORD dwTime, HWND hWndPrev, HWND hWndNext);
	void Flash(HWND hWnd, LPCRECT pRect, const Gdiplus::Color& clrFlash, DWORD dwTime);
	void FadeOut(HWND hWnd, LPCRECT pRect, COLORREF clrBackground, DWORD dwTime);

private:
	void _PrintWindow(HWND hWnd, CBitmap& bmp);
	void _CaptureWindow(HWND hWnd, LPCRECT pRect, CBitmap& bmp);
	Gdiplus::Bitmap* _CaptureWindow(HWND hWnd, LPCRECT pRect);
	void _UpdateSleepValue();

private:
	int m_nFPS;
	int m_nSleep = 0;
};
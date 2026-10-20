#pragma once
#include <map>

class CUniformUI
{
public:
	static CUniformUI& Instance()
	{
		static CUniformUI obj;
		return obj;
	}

private:
	CUniformUI()
	{
		_Init();
	}

	~CUniformUI()
	{
		MAP_FONT_Iter it = m_mapFonts.begin();
		for (; it != m_mapFonts.end(); ++it)
		{
			::DeleteObject(it->second);
		}
	}
public:
	void InitUIParameters(HWND hWnd, int nPPI, bool bResizeWnd)
	{
		ATLASSERT(hWnd != NULL);
		double _ppiX;
		double _ppiY;
		HDC hDC = ::GetDC(hWnd);
        // todo 
		_ppiX = ::GetDeviceCaps(hDC, LOGPIXELSX);
		_ppiY = ::GetDeviceCaps(hDC, LOGPIXELSY);
		::ReleaseDC(hWnd, hDC);	
		double dScaleX = nPPI / _ppiX;
		double dScaleY = nPPI / _ppiY;
		LONG lFontHeight = (LONG)((nPPI / _ppiY) * m_Logfont.lfHeight);
		_UniformWindow(hWnd, dScaleX, dScaleY, lFontHeight, bResizeWnd);
	}

	HFONT GetDefaultFont(HWND hWnd, int nPPI)
	{
		ATLASSERT(hWnd != NULL);
		double _ppiY;
		HDC hDC = ::GetDC(hWnd);
		_ppiY = ::GetDeviceCaps(hDC, LOGPIXELSY);
		::ReleaseDC(hWnd, hDC);	
		LONG lFontHeight = (LONG)((nPPI / _ppiY) * m_Logfont.lfHeight);
		return _GetFont(lFontHeight);
	}
private:
	void _Init()
	{
		::SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &m_Logfont, 0);
	}

	typedef struct
	{
		HWND hWnd;
		double dScaleX;
		double dScaleY;
		HFONT hFont;
		bool bResizeWnd;
	} _EnumChildParam;
	void _UniformWindow(HWND hWnd, double dScaleX, double dScaleY, LONG lFontHeight, bool bResizeWnd)
	{
		if (bResizeWnd)
		{
			CRect rcWnd;
			::GetWindowRect(hWnd, &rcWnd);

			HWND hWndParent = ::GetParent(hWnd);
			if (hWndParent != NULL)
			{
				::ScreenToClient(hWndParent, &rcWnd.TopLeft());
				::ScreenToClient(hWndParent, &rcWnd.BottomRight());
			}
			::MoveWindow(
				hWnd,
				(int)(rcWnd.left * dScaleX),
				(int)(rcWnd.top * dScaleY),
				(int)(rcWnd.Width() * dScaleX),
				(int)(rcWnd.Height() * dScaleY),
				TRUE);
		}
		HFONT hFont = _GetFont(lFontHeight);
		::SendMessage(hWnd, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
		_EnumChildParam param;
		param.hWnd = hWnd;
		param.dScaleX = dScaleX;
		param.dScaleY = dScaleY;
		param.hFont = hFont;
		param.bResizeWnd = bResizeWnd;
		::EnumChildWindows(hWnd, _UniformWindowCallback, (LPARAM)&param);
	}

	static BOOL CALLBACK _UniformWindowCallback(HWND hWnd, LPARAM lParam)
	{
		_EnumChildParam* pParam = (_EnumChildParam*)lParam;
		HWND hWndParent = ::GetParent(hWnd);
		ATLASSERT(hWndParent);
		if (hWndParent == pParam->hWnd)
		{
			if (pParam->bResizeWnd)
			{
				CRect rc;
				::GetWindowRect(hWnd, &rc);
				::ScreenToClient(hWndParent, &rc.TopLeft());
				::ScreenToClient(hWndParent, &rc.BottomRight());
				::MoveWindow(
					hWnd,
					(int)(rc.left * pParam->dScaleX),
					(int)(rc.top * pParam->dScaleY),
					(int)(rc.Width() * pParam->dScaleX),
					(int)(rc.Height() * pParam->dScaleY),
					TRUE);
			}
		}
		::SendMessage(hWnd, WM_SETFONT, (WPARAM)pParam->hFont, MAKELPARAM(TRUE, 0));
		return TRUE;
	}

	HFONT _GetFont(LONG lHeight)
	{
		MAP_FONT_Iter it = m_mapFonts.find(lHeight);
		if (it != m_mapFonts.end())
			return it->second;
		LOGFONT lf = m_Logfont;
		lf.lfHeight = lHeight;
		HFONT hFont = ::CreateFontIndirect(&lf);
		ATLASSERT(hFont != NULL);
		m_mapFonts.insert(MAP_FONT_VAL(lHeight, hFont));
		return hFont;
	}
private:
	LOGFONT m_Logfont;

	typedef std::map<LONG, HFONT> MAP_FONT;
	typedef MAP_FONT::iterator MAP_FONT_Iter;
	typedef MAP_FONT::value_type MAP_FONT_VAL;

	MAP_FONT m_mapFonts;
};



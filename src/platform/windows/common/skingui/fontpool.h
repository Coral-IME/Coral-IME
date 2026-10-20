#pragma

#include <map>

class CFontPool
{
public:
	CFontPool()
	{

	}

	~CFontPool()
	{
		for(MapHFontIter it = m_HFonts.begin();
			it != m_HFonts.end(); ++it)
		{
			::DeleteObject(it->second);
		}
		m_HFonts.clear();
	}

public:
	static int Pt2Em(int nPt, int nPPI)
	{
		return MulDiv(nPt, nPPI, 72);
	}

	static int Em2Pt(int nEm, int nPPI)
	{
		return MulDiv(nEm, 72, nPPI);
	}

	static int GetDevicePPI(HDC hDC)
	{
		return ::GetDeviceCaps(hDC, LOGPIXELSY);
	}

public:
	HFONT GetFont(
		HDC dc,
		const WCHAR* szFontFace,
		int nSize,
		bool bBold, 
		bool bItalic, 
		bool bUnderLine,
		bool bStrikeOut)
	{
		ATLASSERT(szFontFace && szFontFace[0] != 0);

		HFONT hFont = NULL;
		CString strKey = _MakeKey(szFontFace, nSize, bBold, bItalic, bUnderLine, bStrikeOut);
		MapHFontIter it = m_HFonts.find(strKey);
		if (it == m_HFonts.end())
		{
			hFont = _CreateFont(dc, szFontFace, nSize, bBold, bItalic, bUnderLine, bStrikeOut);
			if (hFont != NULL)
				m_HFonts.insert(MapHFontItem(strKey, hFont));
		}
		else
			hFont = it->second;

		return hFont;
	}

private:
	HFONT _CreateFont(
		HDC dc,
		const WCHAR* szFontFace,
		int nSize,
		bool bBold, 
		bool bItalic, 
		bool bUnderLine,
		bool bStrikeOut)
	{
		LOGFONTW lf;
		::GetObject(::GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
		wcscpy_s(lf.lfFaceName, szFontFace);
		lf.lfHeight = -nSize;
		lf.lfWeight = bBold ? FW_BOLD : FW_NORMAL;
		lf.lfItalic = bItalic;
		lf.lfUnderline = bUnderLine;
		lf.lfStrikeOut = bStrikeOut;
// 		lf.lfQuality = ANTIALIASED_QUALITY;

		return ::CreateFontIndirect(&lf);
	}

	CString _MakeKey(
		const WCHAR* szFontFace, 
		int nSize, 
		bool bBold, 
		bool bItalic, 
		bool bUnderLine, 
		bool bStrikeOut)
	{
		CString strFont = szFontFace;
		CString strKey;
		strKey.Format(L"%s;%d;%d;%d;%d;%d", strFont.MakeLower(), nSize, bBold, bItalic, bUnderLine, bStrikeOut);
		return strKey;
	}

private:
	typedef std::map<CString, HFONT> MapHFont;
	typedef MapHFont::iterator MapHFontIter;
	typedef MapHFont::value_type MapHFontItem;

	MapHFont m_HFonts;
};
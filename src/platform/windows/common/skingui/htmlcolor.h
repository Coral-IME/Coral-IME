#pragma once
#include <gdiplus.h>

class CHtmlColor
{
public:
	CHtmlColor(LPCTSTR szColor) // 形如"red" 或 "#FF0000"
	{
		_SetColorFromString(szColor);
	}

	~CHtmlColor()
	{

	}

	BYTE GetR()
	{
		return GetRValue(m_clr);
	}

	BYTE GetG()
	{
		return GetGValue(m_clr);
	}

	BYTE GetB()
	{
		return GetBValue(m_clr);
	}

	operator COLORREF() const
	{
		return m_clr;
	}
	operator Gdiplus::Color() const
	{
		return m_gdiplus_clr;
	}
	Gdiplus::ARGB getARGB() const
	{
		return m_gdiplus_clr;
	}

private:
	void _SetColorFromString(LPCTSTR szColor)
	{
		_ASSERTE(szColor);
		if (!szColor)
			return;

		m_clr = RGB(0, 0, 0); //初始化为黑色
		if (szColor[0] == _T('#'))
		{
			size_t nlen = _tcslen(szColor);
			// 例如 #FF0000
			if (nlen == 7)
			{
				TCHAR szVal[3] = {0};
				LPCTSTR p = ++szColor;
				
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE r = (BYTE)_tcstoul(szVal, NULL, 16);
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE g = (BYTE)_tcstoul(szVal, NULL, 16);
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE b = (BYTE)_tcstoul(szVal, NULL, 16);
				m_clr = RGB(r, g, b);
				m_gdiplus_clr = Gdiplus::Color::MakeARGB(255, r, g, b);
			}
			// 例如 #00FF0000
			else if (nlen == 9)
			{
				TCHAR szVal[3] = { 0 };
				LPCTSTR p = ++szColor;
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE a = (BYTE)_tcstoul(szVal, NULL, 16);
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE r = (BYTE)_tcstoul(szVal, NULL, 16);
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE g = (BYTE)_tcstoul(szVal, NULL, 16);
				szVal[0] = *p++;
				szVal[1] = *p++;
				BYTE b = (BYTE)_tcstoul(szVal, NULL, 16);
				m_gdiplus_clr = Gdiplus::Color::MakeARGB(a, r, g, b);
			}
			else
				_ASSERTE(false);
		}
		else
		{
			// 例如 red
			
			for (int i = 0; i < m_nNamedColors; ++i)
			{
				if (0 == _tcsicmp(szColor, m_NamedColors[i].szName))
				{
					m_clr = m_NamedColors[i].clr;
					break;
				}
			}
		}
	}

private:
	COLORREF m_clr;
	Gdiplus::ARGB m_gdiplus_clr;
	
	typedef struct _NAMEDCOLOR
	{
		COLORREF clr;
		LPCTSTR szName;
	} NAMEDCOLOR;

	static const NAMEDCOLOR m_NamedColors[];
	static const int m_nNamedColors;
};

const __declspec(selectany) CHtmlColor::NAMEDCOLOR CHtmlColor::m_NamedColors[] = 
{
	// 具名 HTML 颜色
	{ RGB(240,248,255),			_T("AliceBlue") },
	{ RGB(250,235,215),			_T("AntiqueWhite") },
	{ RGB(  0,255,255),			_T("Aqua") },
	{ RGB(127,255,212),			_T("Aquamarine") },
	{ RGB(240,255,255),			_T("Azure") },
	{ RGB(245,245,220),			_T("Beige") },
	{ RGB(255,228,196),			_T("Bisque") },
	{ RGB(  0,  0,  0),			_T("Black") },
	{ RGB(255,255,205),			_T("BlanchedAlmond") },
	{ RGB(  0,  0,255),			_T("Blue") },
	{ RGB(138, 43,226),			_T("BlueViolet") },
	{ RGB(165, 42, 42),			_T("Brown") },
	{ RGB(222,184,135),			_T("Burlywood") },
	{ RGB( 95,158,160),			_T("CadetBlue") },
	{ RGB(127,255,  0),			_T("Chartreuse") },
	{ RGB(210,105, 30),			_T("Chocolate") },
	{ RGB(255,127, 80),			_T("Coral") },
	{ RGB(100,149,237),			_T("CornflowerBlue") },
	{ RGB(255,248,220),			_T("Cornsilk") },
	{ RGB(220, 20, 60),			_T("Crimson") },
	{ RGB(  0,255,255),			_T("Cyan") },
	{ RGB(  0,  0,139),			_T("DarkBlue") },
	{ RGB(  0,139,139),			_T("DarkCyan") },
	{ RGB(184,134, 11),			_T("DarkGoldenRod") },
	{ RGB(169,169,169),			_T("DarkGray") },
	{ RGB(  0,100,  0),			_T("DarkGreen") },
	{ RGB(189,183,107),			_T("DarkKhaki") },
	{ RGB(139,  0,139),			_T("DarkMagenta") },
	{ RGB( 85,107, 47),			_T("DarkOliveGreen") },
	{ RGB(255,140,  0),			_T("DarkOrange") },
	{ RGB(153, 50,204),			_T("DarkOrchid") },
	{ RGB(139,  0,  0),			_T("DarkRed") },
	{ RGB(233,150,122),			_T("DarkSalmon") },
	{ RGB(143,188,143),			_T("DarkSeaGreen") },
	{ RGB( 72, 61,139),			_T("DarkSlateBlue") },
	{ RGB( 47, 79, 79),			_T("DarkSlateGray") },
	{ RGB(  0,206,209),			_T("DarkTurquoise") },
	{ RGB(148,  0,211),			_T("DarkViolet") },
	{ RGB(255, 20,147),			_T("DeepPink") },
	{ RGB(  0,191,255),			_T("DeepSkyBlue") },
	{ RGB(105,105,105),			_T("DimGray") },
	{ RGB( 30,144,255),			_T("DodgerBlue") },
	{ RGB(178, 34, 34),			_T("FireBrick") },
	{ RGB(255,250,240),			_T("FloralWhite") },
	{ RGB( 34,139, 34),			_T("ForestGreen") },
	{ RGB(255,  0,255),			_T("Fuchsia") },
	{ RGB(220,220,220),			_T("Gainsboro") },
	{ RGB(248,248,255),			_T("GhostWhite") },
	{ RGB(255,215,  0),			_T("Gold") },
	{ RGB(218,165, 32),			_T("GoldenRod") },
	{ RGB(127,127,127),			_T("Gray") },
	{ RGB(  0,128,  0),			_T("Green") },
	{ RGB(173,255, 47),			_T("GreenYellow") },
	{ RGB(240,255,240),			_T("HoneyDew") },
	{ RGB(255,105,180),			_T("HotPink") },
	{ RGB(205, 92, 92),			_T("IndianRed") },
	{ RGB( 75,  0,130),			_T("Indigo") },
	{ RGB(255,255,240),			_T("Ivory") },
	{ RGB(240,230,140),			_T("Khaki") },
	{ RGB(230,230,250),			_T("Lavender") },
	{ RGB(255,240,245),			_T("LavenderBlush") },
	{ RGB(124,252,  0),			_T("Lawngreen") },
	{ RGB(255,250,205),			_T("LemonChiffon") },
	{ RGB(173,216,230),			_T("LightBlue") },
	{ RGB(240,128,128),			_T("LightCoral") },
	{ RGB(224,255,255),			_T("LightCyan") },
	{ RGB(250,250,210),			_T("LightGoldenRodYellow")},
	{ RGB(144,238,144),			_T("LightGreen") },
	{ RGB(211,211,211),			_T("LightGrey") },
	{ RGB(255,182,193),			_T("LightPink") },
	{ RGB(255,160,122),			_T("LightSalmon") },
	{ RGB( 32,178,170),			_T("LightSeaGreen") },
	{ RGB(135,206,250),			_T("LightSkyBlue") },
	{ RGB(119,136,153),			_T("LightSlateGray") },
	{ RGB(176,196,222),			_T("LightSteelBlue") },
	{ RGB(255,255,224),			_T("LightYellow") },
	{ RGB(  0,255,  0),			_T("Lime") },
	{ RGB( 50,205, 50),			_T("LimeGreen") },
	{ RGB(250,240,230),			_T("Linen") },
	{ RGB(255,  0,255),			_T("Magenta") },
	{ RGB(128,  0,  0),			_T("Maroon") },
	{ RGB(102,205,170),			_T("MediumAquamarine") },
	{ RGB(  0,  0,205),			_T("MediumBlue") },
	{ RGB(186, 85,211),			_T("MediumOrchid") },
	{ RGB(147,112,219),			_T("MediumPurple") },
	{ RGB( 60,179,113),			_T("MediumSeaGreen") },
	{ RGB(123,104,238),			_T("MediumSlateBlue") },
	{ RGB(  0,250,154),			_T("MediumSpringGreen") },
	{ RGB( 72,209,204),			_T("MediumTurquoise") },
	{ RGB(199, 21,133),			_T("MediumVioletRed") },
	{ RGB( 25, 25,112),			_T("MidnightBlue") },
	{ RGB(245,255,250),			_T("MintCream") },
	{ RGB(255,228,225),			_T("MistyRose") },
	{ RGB(255,228,181),			_T("Moccasin") },
	{ RGB(255,222,173),			_T("NavajoWhite") },
	{ RGB(  0,  0,128),			_T("Navy") },
	{ RGB(253,245,230),			_T("OldLace") },
	{ RGB(128,128,  0),			_T("Olive") },
	{ RGB(107,142, 35),			_T("OliveDrab") },
	{ RGB(255,165,  0),			_T("Orange") },
	{ RGB(255, 69,  0),			_T("OrangeRed") },
	{ RGB(218,112,214),			_T("Orchid") },
	{ RGB(238,232,170),			_T("PaleGoldenRod") },
	{ RGB(152,251,152),			_T("PaleGreen") },
	{ RGB(175,238,238),			_T("PaleTurquoise") },
	{ RGB(219,112,147),			_T("PaleVioletRed") },
	{ RGB(255,239,213),			_T("PapayaWhip") },
	{ RGB(255,218,185),			_T("PeachPuff") },
	{ RGB(205,133, 63),			_T("Peru") },
	{ RGB(255,192,203),			_T("Pink") },
	{ RGB(221,160,221),			_T("Plum") },
	{ RGB(176,224,230),			_T("PowderBlue") },
	{ RGB(128,  0,128),			_T("Purple") },
	{ RGB(255,  0,  0),			_T("Red") },
	{ RGB(188,143,143),			_T("RosyBrown") },
	{ RGB( 65,105,225),			_T("RoyalBlue") },
	{ RGB(139, 69, 19),			_T("SaddleBrown") },
	{ RGB(250,128,114),			_T("Salmon") },
	{ RGB(244,164, 96),			_T("SandyBrown") },
	{ RGB( 46,139, 87),			_T("SeaGreen") },
	{ RGB(255,245,238),			_T("SeaShell") },
	{ RGB(160, 82, 45),			_T("Sienna") },
	{ RGB(192,192,192),			_T("Silver") },
	{ RGB(135,206,235),			_T("SkyBlue") },
	{ RGB(106, 90,205),			_T("SlateBlue") },
	{ RGB(112,128,144),			_T("SlateGray") },
	{ RGB(255,250,250),			_T("Snow") },
	{ RGB(  0,255,127),			_T("SpringGreen") },
	{ RGB( 70,130,180),			_T("SteelBlue") },
	{ RGB(210,180,140),			_T("Tan") },
	{ RGB(  0,128,128),			_T("Teal") },
	{ RGB(216,191,216),			_T("Thistle") },
	{ RGB(255, 99, 71),			_T("Tomato") },
	{ RGB( 64,224,208),			_T("Turquoise") },
	{ RGB(238,130,238),			_T("Violet") },
	{ RGB(245,222,179),			_T("Wheat") },
	{ RGB(255,255,255),			_T("White") },
	{ RGB(245,245,245),			_T("WhiteSmoke") },
	{ RGB(255,255,  0),			_T("Yellow") },
	{ RGB(139,205, 50),			_T("YellowGreen") }
};

const __declspec(selectany) int CHtmlColor::m_nNamedColors = sizeof(CHtmlColor::m_NamedColors) / sizeof(CHtmlColor::m_NamedColors[0]);
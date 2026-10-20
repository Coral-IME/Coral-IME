// 创建人	:	黄伯荣
// 创建时间	:	2012:2:1 10:16:33
// 功能描述:	用于绘制仿html格式的文字
// 标准标签：	加粗<b>, 斜体<i>，下划线<u>，换行<br>，上标<sup>
//				下标<sub>，删除线<strike>，
//				字体<font>, font标签支持的属性有大小size，字体名face，颜色color
// 自定义标签: <overflow>,属性有nowrap和ellipsis
//				<spacing>, 属性有line和char，用于指定某段文本的间距设置，覆盖全局设定

// Example:
// CTextDrawer drawer;
// TCHAR szText[] = _T("<font face='simsun' size='16' color='#FF0000'>test<b><i>123456</b></i><sub>subtext</sub></font><br><u>newline</u><sup>suptext</sup>");
// drawer.Draw(dc, szText, CRect(0, 0, 200, 200);
//
// 
// -----------------------------------------------------------------------
#ifndef CORAL_SRC_PLATFORM_WINDOWS_COMMON_SKINGUI_TEXTDRAWER_H_INCLUDED
#define CORAL_SRC_PLATFORM_WINDOWS_COMMON_SKINGUI_TEXTDRAWER_H_INCLUDED

#include "htmlcolor.h"
#include "fontpool.h"
#include <vector>
#include <algorithm>

#define COLOR_NONE ((DWORD)-1)
#define LENGTH_NONE ((int)-1)

enum
{
	TD_LEFT = 0x0001,
	TD_RIGHT = 0x0002,
	TD_CENTER = 0x0004,

	TD_TOP = 0x0010,
	TD_BOTTOM = 0x0020,
	TD_VCENTER = 0x0040,

	TD_CALCRECT = 0x0100,
	TD_SINGLELINE = 0x0200,
};
typedef int TD_FLAG;

#define _TD_DEF_INDENT_CHAR 0
#define _TD_DEF_INDENT_LINE 1

class CTextParser
{
public:
	typedef struct _TextParam
	{
		bool bBold;
		bool bUnderline;
		bool bItalic;
		bool bStrike;
		int nSub;
		int nSup;
		int nOffset;
		CString strFontSize;
		CString strFontFace;
		COLORREF clrText;
		bool bNoWrap;
		bool bEllipsis;
		int nSpaceLine;
		int nSpaceChar;

		_TextParam()
			: bBold(false)
			, bUnderline(false)
			, bItalic(false)
			, nSub(0)
			, nSup(0)
			, nOffset(0)
			, bStrike(false)
			, clrText(COLOR_NONE)
			, bNoWrap(false)
			, bEllipsis(false)
			, nSpaceChar(LENGTH_NONE)
			, nSpaceLine(LENGTH_NONE)
		{

		}
	} TextParam;

	typedef enum
	{
		EleText,
		EleLineBreak,
	} ElementType;

	typedef struct _Element
	{
		ElementType type;
		CString strText;
		TextParam param;
	} Element;

	typedef std::vector<Element> Elements;
	typedef Elements::iterator Elements_Iter;

	typedef std::map<CString, CString> AttrMap;
	typedef AttrMap::iterator AttrMap_Iter;
	typedef AttrMap::value_type Attr;
	typedef struct _Tag
	{
		CString Name;
		AttrMap Attrs;
	} HtmlTag;

public:
	CTextParser()
	{

	}

	~CTextParser()
	{

	}

public:
	void ParseText(LPCTSTR szText)
	{
#define _TAG_MATCH(tag, t) (0 == _tcsicmp(t, tag.Name))
#define _TAG_HAS_ATTRS(tag) (!tag.Attrs.empty())
#define _TAG_NOATTR_MATCH(tag, t) (_TAG_MATCH(tag, t) && !_TAG_HAS_ATTRS(tag))

		m_elements.clear();
		size_t nLen = _tcslen(szText);
		TextParam param;
		while(nLen != 0)
		{
			size_t nTokenLen = _GetToken(szText, nLen);
			HtmlTag tag;
			bool bAsPlain = false;
			if (_ParseTag(szText, nTokenLen, tag))
			{
				// 分析tag
				if (_TAG_NOATTR_MATCH(tag, _T("b")))
				{
					param.bBold = true;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/b")))
				{
					param.bBold = false;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("i")))
				{
					param.bItalic = true;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/i")))
				{
					param.bItalic = false;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("u")))
				{
					param.bUnderline = true;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/u")))
				{
					param.bUnderline = false;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("br")))
				{
					Element e;
					e.param = param;
					e.type = EleLineBreak;
					m_elements.push_back(e);
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("sub")))
				{
					++param.nSub;
					if (param.nOffset <= 0)
						--param.nOffset;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/sub")))
				{
					if (param.nSub > 0)
						--param.nSub;
					if (param.nOffset < 0 && ++param.nOffset == 0)
					{
						param.nSub = param.nSup = 0;
					}
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("sup")))
				{
					++param.nSup;
					if (param.nOffset >= 0)
						++param.nOffset;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/sup")))
				{
					if (param.nSub > 0)
						--param.nSup;
					if (param.nOffset > 0 && --param.nOffset == 0)
					{
						param.nSub = param.nSup = 0;
					}
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("strike")))
				{
					param.bStrike = true;
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/strike")))
				{
					param.bStrike = false;
				}
				else if (_TAG_MATCH(tag, _T("font")))
				{
					CString strVal;
					if (_GetAttrVal(tag, _T("size"), strVal))
					{
						param.strFontSize = strVal;
					}
					if (_GetAttrVal(tag, _T("color"), strVal))
					{
						param.clrText = CHtmlColor(strVal);
					}
					if (_GetAttrVal(tag, _T("face"), strVal))
					{
						param.strFontFace = strVal;
					}
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/font")))
				{
					param.strFontSize = _T("");
					param.clrText = COLOR_NONE;
					param.strFontFace = _T("");
				}
				else if (_TAG_MATCH(tag, _T("overflow")))
				{
					CString strVal;
					if (_GetAttrVal(tag, _T("ellipsis"), strVal))
					{
						if (0 == _tcsicmp(strVal, _T("true")))
							param.bEllipsis = true;
						else if (0 == _tcsicmp(strVal, _T("false")))
							param.bEllipsis = false;
					}
					if (_GetAttrVal(tag, _T("nowrap"), strVal))
					{
						if (0 == _tcsicmp(strVal, _T("true")))
							param.bNoWrap = true;
						else if (0 == _tcsicmp(strVal, _T("false")))
							param.bNoWrap = false;
					}
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/overflow")))
				{
					param.bNoWrap = false;
					param.bEllipsis = false;
				}
				else if (_TAG_MATCH(tag, _T("spacing")))
				{
					CString strVal;
					if (_GetAttrVal(tag, _T("char"), strVal))
					{
						param.nSpaceChar = _ttoi(strVal);
					}
					if (_GetAttrVal(tag, _T("line"), strVal))
					{
						param.nSpaceLine = _ttoi(strVal);
					}
				}
				else if (_TAG_NOATTR_MATCH(tag, _T("/spacing")))
				{
					param.nSpaceChar = LENGTH_NONE;
					param.nSpaceLine = LENGTH_NONE;
				}
				else
					bAsPlain = true; // 不支持的tag
			}
			else
				bAsPlain = true; // 不是tag或tag格式不对

			if (bAsPlain)
			{
				// 纯文本
				Element e;
				e.type = EleText;
				e.param = param;
				LPTSTR p = e.strText.GetBuffer((int)nTokenLen + 1);
				_tcsncpy(p, szText, nTokenLen);
				p[nTokenLen] = _T('\0');
				e.strText.ReleaseBuffer();
				m_elements.push_back(e);
			}
			nLen -= nTokenLen;
			szText += nTokenLen;
		}	
	}

private:
	size_t _GetToken(LPCTSTR szString, size_t nSize)
	{
		bool bStartTag = false;
		LPCTSTR p1 = szString;
		LPCTSTR p2 = szString + nSize;
		while(p1 < p2)
		{
			if (*p1 == _T('<'))
			{
				if (p1 == szString)
				{
					// 第一个字符就是 <
					bStartTag = true;
				}
				else
					break;
			}
			else if (*p1 == _T('>'))
			{
				if (bStartTag)
				{
					++p1;
					break; // 标签结束
				}
			}
			++p1;
		}
		return (p1 - szString);
	}

	bool _ParseTag(LPCTSTR szTag, size_t nSize, HtmlTag& tag)
	{
		if (nSize < 3 || szTag[0] != _T('<') || szTag[nSize - 1] != _T('>'))
			return false;

		LPCTSTR p1 = szTag + 1;
		LPCTSTR p2 = szTag + nSize - 1;

		// 先取标签名
		while(p1 < p2)
		{
			if (_istspace(*p1))
				break;
			tag.Name.AppendChar(*p1);
			++p1;
		}
		if (tag.Name.IsEmpty())
			return false;
		tag.Name.MakeLower();

		// 逐个取属性
		while(p1 < p2)
		{
			if (!_istspace(*p1))
				return false;
			CString strAttr, strVal;
			// 去掉前导空格
			while(p1 < p2 && _istspace(*p1)) 
				++p1;
			while(p1 < p2 && *p1 != _T('='))
			{
				if (_istspace(*p1))
					return false; // 属性名中不应有空格
				strAttr.AppendChar(*p1++);
			}
			if (strAttr.IsEmpty())
				return false; // 没有属性名
			if (++p1 >= p2 || !_IsQuote(*p1++))
				return false; // 属性值不是以"开始
			while(p1 < p2 && !_IsQuote(*p1))
				strVal.AppendChar(*p1++);
			if (strVal.IsEmpty())
				return false; // 没有属性值
			if (p1 >= p2)
				return false; // 属性值没有以"结束
			strVal.Trim();
			tag.Attrs.insert(Attr(strAttr.MakeLower(), strVal.MakeLower()));
			++p1;		             
		}
		return true;
	}

	bool _GetAttrVal(HtmlTag& tag, LPCTSTR szAttr, CString& strVal)
	{
		AttrMap_Iter it = tag.Attrs.find(CString(szAttr).MakeLower());
		if (it == tag.Attrs.end())
			return false;

		strVal = it->second;
		return true;
	}

	bool _IsQuote(TCHAR ch)
	{
		return (ch == _T('\'') || ch == _T('\"'));
	}

public:
	Elements m_elements;
};

class CTextDrawer
{
private:
	typedef struct tagDrawItem
	{
		HFONT hFont;
		COLORREF clrText;
		CString strText;
		int nFontHeight;
		int nOffset;
		int nSpaceChar;
	} _DrawItem;

	typedef std::vector<_DrawItem> _DrawItems;
	typedef _DrawItems::iterator _DrawItems_Iter;


	typedef struct tagLineItem
	{
		int nLineWidth;			// 行宽
		int nLineHeight;		// 行高
		int nLineBase;			// 行基准线
		int nSupOffset;
		bool bSupFirst;
		int nSpaceLine;
		_DrawItems drawItems;	// 绘画项
	} _LineItem;
	typedef std::vector<_LineItem> _LineItems;
	typedef _LineItems::iterator _LineItems_Iter;

public:
	CTextDrawer()
	{

	}

	~CTextDrawer()                                                                                                                                                                     
	{

	}
public: 
	static void SetDrawerPPI(int nPPI)
	{
		sm_nPPI = nPPI;
	}

public:

	// 绘制文本
	void Draw(
		HDC hDC, 
		LPCTSTR szHtmlText, 
		RECT rect, 
		int nSpaceChar = _TD_DEF_INDENT_CHAR, 
		int nSpaceLine = _TD_DEF_INDENT_LINE)
	{
		_ASSERT(hDC);
		_ASSERT(szHtmlText);
		if (!hDC || !szHtmlText || (szHtmlText[0] == _T('\0')))
			return;

		CRect rc;
		_DrawText(hDC, szHtmlText, rect, NULL, TD_LEFT | TD_TOP, nSpaceChar, nSpaceLine);
	}

	void Draw(
		HDC hDC,
		LPCTSTR szHtmlText,
		int x,
		int y,
		int nWidth,
		int nHeight,
		int nSpaceChar = _TD_DEF_INDENT_CHAR,
		int nSpaceLine = _TD_DEF_INDENT_LINE)
	{
		CRect rc;
		rc.left = x;
		rc.top = y;
		rc.right = rc.left + nWidth;
		rc.bottom = rc.top + nHeight;
		Draw(hDC, szHtmlText, rc, nSpaceChar, nSpaceLine);
	}

	// 计算绘制文本实际占用空间
	void CalculateDrawRect(
		HDC hDC, 
		LPCTSTR szHtmlText, 
		IN OUT LPRECT pRect, 
		int nSpaceChar = _TD_DEF_INDENT_CHAR, 
		int nSpaceLine = _TD_DEF_INDENT_LINE)
	{
		_ASSERT(hDC);
		_ASSERT(szHtmlText);
		_ASSERT(pRect);
		if (!hDC || !pRect || !szHtmlText || (szHtmlText[0] == _T('\0')))
			return;

		_DrawText(hDC, szHtmlText, *pRect, pRect, TD_LEFT | TD_TOP | TD_CALCRECT, nSpaceChar, nSpaceLine);
	}

	void DrawEx(
		HDC hDC, 
		LPCTSTR szHtmlText, 
		RECT rect, 
		TD_FLAG flags = TD_LEFT | TD_TOP,
		OUT LPRECT pRectOut = NULL,
		int nSpaceChar = _TD_DEF_INDENT_CHAR, 
		int nSpaceLine = _TD_DEF_INDENT_LINE)
	{
		_ASSERT(hDC);
		_ASSERT(szHtmlText);
		if (!hDC || !szHtmlText || (szHtmlText[0] == _T('\0')))
			return;
		_DrawText(hDC, szHtmlText, rect, pRectOut, flags, nSpaceChar, nSpaceLine);
	}

private:
	void _DrawText(
		HDC hDC, 
		LPCTSTR szText, 
		RECT rect,
		PRECT pRectOut,
		TD_FLAG flags,
		int nSpaceChar, 
		int nSpaceLine)
	{
		CRect rcIn(rect);
		CRect rcOut;
		CDCHandle dc(hDC);
		CTextParser parser;
		parser.ParseText(szText);
		_LineItems lines;
		_GenerateTextLayout(dc, rect, parser, lines, nSpaceLine, nSpaceChar);
		
		if (flags & TD_SINGLELINE)
		{
			// 仅绘制一行
			if (lines.size() != 0)
			{
				_LineItem item = lines.front();
				lines.clear();
				lines.push_back(item);
			}
		}

		if (flags & TD_CALCRECT)
			_CalcLayoutRect(lines, TRUE, &rcIn, &rcOut);
		else
			_CalcLayoutRect(lines, FALSE, &rcIn, &rcOut);

		// 判断水平标记
		if (flags & TD_LEFT)
			;
		else if (flags & TD_RIGHT)
			rcOut.OffsetRect(rcIn.Width() - rcOut.Width(), 0);
		else
			rcOut.OffsetRect((rcIn.Width() - rcOut.Width()) / 2, 0);

		if (rcOut.Height() <= rcIn.Height())
		{
			// 判断垂直标记
			if (flags & TD_TOP)
				;
			else if (flags & TD_BOTTOM)
				rcOut.OffsetRect(0, rcIn.Height() - rcOut.Height());
			else
				rcOut.OffsetRect(0, (rcIn.Height() - rcOut.Height()) / 2);
		}
		if (pRectOut)
			*pRectOut = rcOut;
		if (flags & TD_CALCRECT)
		{
			_ASSERTE(pRectOut != NULL);
			return;
		}
		_DrawLines(dc, rcOut, lines);
	};

	void _CalcLayoutRect(_LineItems& lines, BOOL bFull, LPRECT pRectIn, LPRECT pRectOut)
	{
		int nBottom = pRectIn->bottom;
		CRect rcOut(pRectIn->left, pRectIn->top, pRectIn->left, pRectIn->top);

		int nLines = (int)lines.size();
		for (int i = 0; i < nLines; ++i)
		{
			int nIndent = (i == 0) ? 0 : lines[i - 1].nSpaceLine;
			if (!bFull && (rcOut.bottom + nIndent >= nBottom))
				break; // 超出范围
			rcOut.bottom += nIndent + lines[i].nLineHeight;
			rcOut.right = std::max(rcOut.right, rcOut.left + lines[i].nLineWidth);			
		}
		*pRectOut = rcOut;
	}

	void _GenerateTextLayout(
		CDCHandle& dc, 
		RECT& rect, 
		CTextParser& parser, 
		_LineItems& lines,
		int nDefSpaceLine,
		int nSpaceChar)
	{
		int nSavedDC = dc.SaveDC();

		// 初始化
		CRect rc(rect);
		m_clrDef = dc.GetTextColor();
		m_nDefFontHeight = _GetFontHeight(dc);
		m_strDefFace = _GetFontFace(dc);
		dc.SetBkMode(TRANSPARENT);

		// 布局分析
		_LineItem line;
		line.nSpaceLine = nDefSpaceLine;
		int nLineWidth = rc.Width();
		int nLastLineHeight = m_nDefFontHeight;
		int nLastItemBase = 0;
		int x = 0;
		
		CTextParser::Elements_Iter it = parser.m_elements.begin();
		for (; it != parser.m_elements.end(); ++it)
		{
			if (it->param.nSpaceLine < 0)
				line.nSpaceLine = nDefSpaceLine; // 恢复默认
			else
				line.nSpaceLine = it->param.nSpaceLine;

			if (it->type == CTextParser::EleText)
				_DealTextLine(dc, *it, lines, line, x, nLineWidth, nLastLineHeight, nLastItemBase, nSpaceChar);
			else if (it->type == CTextParser::EleLineBreak)
				_AddLine(lines, line, x, nLastLineHeight);
		}
		if (!line.drawItems.empty())
			_AddLine(lines, line, x, nLastLineHeight);

		dc.RestoreDC(nSavedDC);
	}

	void _DealTextLine(
		CDCHandle& dc, 
		const CTextParser::Element& ele, 
		_LineItems& lines, 
		_LineItem& line, 
		int& x, 
		int nLineWidth, 
		int& nLastLineHeight, 
		int& nLastItemBase, 
		int nSpaceChar)
	{
		_DrawItem drawItem;
		int nHeight = _StrSize2Pixel(ele.param.strFontSize);
		if(nHeight < 0)
			nHeight = m_nDefFontHeight; // 取默认字体高度
		if (ele.param.nOffset != 0)
		{
			int nScaleCnt = ele.param.nSub + ele.param.nSup;
			for (int i = 0; i < nScaleCnt; ++i)
				nHeight = (int)(nHeight * 7.0 / 10.0);
		}
		drawItem.nOffset = ele.param.nOffset;
		drawItem.clrText = (ele.param.clrText == COLOR_NONE) ? m_clrDef : ele.param.clrText;
		drawItem.hFont = m_fontPool.GetFont(
			dc,
			ele.param.strFontFace.IsEmpty() ? m_strDefFace : ele.param.strFontFace,
			nHeight, 
			ele.param.bBold,
			ele.param.bItalic,
			ele.param.bUnderline,
			ele.param.bStrike);
		dc.SelectFont(drawItem.hFont);
		drawItem.nFontHeight = 0; //reset
		drawItem.nSpaceChar = (ele.param.nSpaceChar < 0) ? nSpaceChar : ele.param.nSpaceChar;

		int nLen = ele.strText.GetLength();
		SIZE szTotal;
		dc.GetTextExtent(ele.strText, nLen, &szTotal);
		SIZE szDot;
		dc.GetTextExtent(_T("."), 1, &szDot);

		bool bWrap = true;
 		if (ele.param.bNoWrap || ele.param.bEllipsis)
			bWrap = false;
		int nPre = (x == 0 ? 0 : drawItem.nSpaceChar);
		int x1 = x;
		if ((nPre + szTotal.cx + drawItem.nSpaceChar * (nLen - 1)) > (nLineWidth - x))
		{
 			// 文字超出范围
 			if (ele.param.bEllipsis)
 				x1 += (szDot.cx + drawItem.nSpaceChar) * 3;
		}
		for (int i = 0; i < nLen; ++i)
		{
			SIZE sz;
			dc.GetTextExtent((LPCTSTR)ele.strText + i, 1, &sz);
			int nWid = (x != 0) ? (drawItem.nSpaceChar + sz.cx) : sz.cx;
			if (nWid + x1 > nLineWidth)
			{
				// 需要换行
				if (bWrap)
				{
					if (x != 0)
					{
						_LineAddItem(line, drawItem, nLastItemBase);
						_AddLine(lines, line, x, nLastLineHeight);
						drawItem.strText.Empty(); //重置
						--i;
						x1 = 0;
						continue;
					}
					else
					{
						// 没有空间写第一个字
						break;
					}
				}
				else
				{
					if (ele.param.bEllipsis)
					{
						drawItem.strText.Append(_T("..."));
						drawItem.nFontHeight = std::max(drawItem.nFontHeight, (int)szDot.cy);
						x = x1;
					}
					break;
				}
	
			}
			else
			{
				drawItem.strText.AppendChar(ele.strText[i]);
				drawItem.nFontHeight = std::max(drawItem.nFontHeight, (int)sz.cy);
				x1 += nWid;
				x += nWid;
			}
		}
 		_LineAddItem(line, drawItem, nLastItemBase);
	}

	CString _GetFontFace(HDC hDC)
	{
		LOGFONT lf;
		CDCHandle dc(hDC);
		HFONT hOldFont = dc.SelectFont((HFONT)::GetStockObject(SYSTEM_FONT));
		::GetObject(hOldFont, sizeof(lf), &lf);
		dc.SelectFont(hOldFont);
		return CString(lf.lfFaceName);
	}

	int _GetFontHeight(HDC hDC)
	{
		TEXTMETRIC tm;
		CDCHandle dc(hDC);
		dc.GetTextMetrics(&tm);
		return (tm.tmHeight - tm.tmInternalLeading);
	}

	int _StrSize2Pixel(LPCTSTR szSize)
	{
		if (!szSize || szSize[0] == 0)
			return -1;

		CString strNum;
		CString strUnit;
		LPCTSTR p = szSize;
		while(*p != 0 && _istdigit(*p))
			strNum.AppendChar(*p++);
		strUnit = p;
		strUnit.Trim();
		
		int nSize = _ttoi(strNum);
		if (nSize < 0)
			return -1; // 不支持负数
		
		if (strUnit.IsEmpty())
			; // 默认为px
		else if (0 == _tcsicmp(strUnit, _T("px")))
			;
		else if (0 == _tcsicmp(strUnit, _T("pt")))
			nSize = CFontPool::Pt2Em(nSize, sm_nPPI); // pt转换为px
		else
			return -1; // 不支持的单位

		return nSize;
	}

	void _LineAddItem(_LineItem& line, _DrawItem& item, int& nLastItemBase)
	{
		if (line.drawItems.empty())
		{
			// 第一个
			line.nLineHeight = item.nFontHeight;
			line.nLineBase = item.nFontHeight;
			line.nSupOffset = 0;
			line.bSupFirst = item.nOffset > 0 ? true : false;
			nLastItemBase = item.nFontHeight;
		}
		else
		{
			_DrawItem& lastItem = line.drawItems.back();
			if (item.nOffset == 0)
			{
				int nDiff = line.nLineBase - item.nFontHeight;
				if (nDiff < 0)
				{
					line.nLineBase += -nDiff;
					line.nLineHeight += -nDiff;
					nLastItemBase = line.nLineBase;
					if (line.bSupFirst && line.nSupOffset == 0)
						line.nSupOffset += -nDiff;
				}
				else
				{
					nLastItemBase = line.nLineBase;
				}
			}
			else if (item.nOffset < 0)
			{
				// 添加下缀项
				int nItemBase = nLastItemBase - (lastItem.nFontHeight / 2) + item.nFontHeight;
				line.nLineHeight = std::max(line.nLineHeight, nItemBase);
				nLastItemBase = nItemBase;
			}
			else
			{
				// 添加上缀项
				int nDiff = nLastItemBase - (lastItem.nFontHeight / 2) - item.nFontHeight;
				if (nDiff < 0)
				{
					line.nLineHeight += -nDiff;
					line.nLineBase += -nDiff;
					nLastItemBase = item.nFontHeight;
				}
				else
				{
					nLastItemBase = nDiff + item.nFontHeight;
				}
			}
		}
		line.drawItems.push_back(item);
	}


	void _AddLine(_LineItems& lines, _LineItem& line, int& nWidth, int& nLastLineHeight)
	{
		if (line.drawItems.empty())
		{
			// 空行
			line.nLineWidth = 0;
			line.nLineHeight = line.nLineBase = nLastLineHeight;
			// nLastLineHeight; 不变
		}
		else
		{
			line.nLineWidth = nWidth;
			nLastLineHeight = line.nLineHeight;
		}
		lines.push_back(line);


		// 重置数据
		nWidth = 0;
		_ResetLine(line);
	}

	void _ResetLine(_LineItem& line)
	{
		line.drawItems.clear();
		line.bSupFirst = FALSE;
		line.nLineBase = line.nLineHeight = line.nLineWidth = line.nSupOffset = 0;
		//line.nSpaceLine; 不重置
	}

	void _DrawLines(CDCHandle& dc, RECT rect, _LineItems& lines)
	{
		CRect rc(rect.left, rect.top, rect.right, rect.top);
		int nLines = (int)lines.size();
		for (int i = 0; i < nLines; ++i)
		{
			int nAdd = (i == 0) ? 0 : lines[i - 1].nSpaceLine;
			rc.top = rc.bottom + nAdd;
			rc.bottom = rc.top + lines[i].nLineHeight;
			if (rc.top >= rect.bottom)
				break;
			_DrawLine(dc, rc, lines[i]);
		}
	}

	void _DrawLine(CDCHandle& dc, CRect& rc, _LineItem& line)
	{
		if (line.nLineWidth > 0)
		{
			int x = 0;
			int nLastItemBase = 0;
			int nLastFontHeight = 0;
			int nSpaceChar = 0;
			_DrawItems_Iter it = line.drawItems.begin();
			for (; it != line.drawItems.end(); ++it)
			{
				// 计算绘制区域
				int nItemBase = 0;
				if (x == 0)
				{
					// 第一项
					nItemBase = line.nLineBase;
					if (line.bSupFirst)
						nItemBase -= line.nSupOffset; 
				}
				else
				{
					if (it->nOffset < 0)
						nItemBase = nLastItemBase - (nLastFontHeight / 2) + it->nFontHeight;
					else if (it->nOffset > 0)
						nItemBase = nLastItemBase - (nLastFontHeight / 2);
					else
						nItemBase = line.nLineBase;
				}
				nLastFontHeight = it->nFontHeight;
				nLastItemBase = nItemBase;
				nSpaceChar = it->nSpaceChar;
				CRect rcItem(rc.left + x, rc.top + nItemBase - it->nFontHeight, rc.right, rc.top + nItemBase);

				// 开始绘制
				HFONT hFontOld = dc.SelectFont(it->hFont);
				COLORREF clrOld = dc.SetTextColor(it->clrText);
				int nModeOld = dc.SetBkMode(TRANSPARENT);
				
				CRect r;
				dc.DrawText(_T(" "), 1, &r, DT_LEFT | DT_CALCRECT);
				int nIndentSpaceCnt = nSpaceChar / r.Width();
				if (nSpaceChar % r.Width())
					++nIndentSpaceCnt;
				CString strIndent;
				for (int i = 0; i < nIndentSpaceCnt; ++i)
					strIndent.AppendChar(_T(' '));

				int nLen = it->strText.GetLength();
				for (int i = 0; i < nLen; ++i)
				{
					if (x != 0)
					{
						// 加入间距空格，不能简单留空，因为间距也要有下划线or删除线
						// 所以以空格字符串来填充间距
						CRect rcTemp(rcItem);
						rcTemp.right = rcItem.left + nSpaceChar;
						if (i != 0)
							dc.DrawText(strIndent, nIndentSpaceCnt, &rcTemp, DT_LEFT);
						x += nSpaceChar;
						rcItem.left += nSpaceChar;
					}
					CRect r;
					dc.DrawText((LPCTSTR)it->strText + i, 1, &r, DT_LEFT | DT_CALCRECT);
					dc.DrawText((LPCTSTR)it->strText + i, 1, &rcItem, DT_LEFT);
					x += r.Width();
					rcItem.left += r.Width();
				}
				dc.SetBkMode(nModeOld);
				dc.SetTextColor(clrOld);
				dc.SelectFont(hFontOld);
			}
		}
		else
		{
			_ASSERTE(line.nLineWidth == 0);
			// 空行，无需处理
		}
	}
private:
	static int sm_nPPI;

private:
	CFontPool m_fontPool;
	CString m_strDefFace;
	int m_nDefFontHeight;
	COLORREF m_clrDef;
};

int __declspec(selectany) CTextDrawer::sm_nPPI = 96; // 默认：小字号


#endif

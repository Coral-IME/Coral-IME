#pragma once

#include "wtl10/atlgdi.h"
#define Undefine											99999999


//////////////////////////////////////////////////////////////////////////
//
template <bool t_bManaged>
class CSkinDCT : public CDCT<t_bManaged>
{
public:

    CSkinDCT(HDC hDC = NULL) : CDCT<t_bManaged>(hDC)
    {
    }

	BOOL SkinDrawTransparentBitmap(int x, int y, HBITMAP hBmp, COLORREF rMask = RGB(255, 0, 255))
	{
		CBitmapHandle bmp = hBmp;
		BITMAP bm = { 0 };

		if (hBmp == NULL || !bmp.GetBitmap(&bm))
			return FALSE;

		CDCT<true> dcMem;
		dcMem.CreateCompatibleDC(this->m_hDC);

		HBITMAP hOldBmp = dcMem.SelectBitmap(hBmp);
		TransparentBlt(this->m_hDC, x, y, bm.bmWidth, bm.bmHeight, dcMem, 0, 0, bm.bmWidth, bm.bmHeight, rMask);
		dcMem.SelectBitmap(hOldBmp);

		return TRUE;
	}
	BOOL SkinDrawBitmap( POINT pt, HBITMAP hBmp, DWORD dwRop = SRCCOPY)
    {
        CBitmapHandle bmp = hBmp;
        BITMAP bm = { 0 };

        if (hBmp == NULL || !bmp.GetBitmap(&bm))
            return FALSE;
        
        CDCT<true> dcMem;
        dcMem.CreateCompatibleDC(this->m_hDC);

        HBITMAP hOldBmp = dcMem.SelectBitmap(hBmp);

        this->BitBlt(pt.x, pt.y, bm.bmWidth, bm.bmHeight, dcMem, 0, 0, dwRop );

        dcMem.SelectBitmap(hOldBmp);

        return TRUE;
    }

    BOOL SkinDrawBitmap( POINT pt, int nWidth, int nHeight, HBITMAP hBmp, DWORD dwRop = SRCCOPY)
    {
        CBitmapHandle bmp = hBmp;
        BITMAP bm = { 0 };

        if (hBmp == NULL || !bmp.GetBitmap(&bm))
            return FALSE;

        CDCT<true> dcMem;
        dcMem.CreateCompatibleDC(this->m_hDC);

        HBITMAP hOldBmp = dcMem.SelectBitmap(hBmp);

        this->BitBlt(pt.x, pt.y, nWidth, nHeight, dcMem, 0, 0, dwRop );

        dcMem.SelectBitmap(hOldBmp);

        return TRUE;
    }


    BOOL SkinDrawBitmap( LPRECT lpRect, HBITMAP hBmp, DWORD dwRop = SRCCOPY)
    {
        CBitmapHandle bmp = hBmp;
        BITMAP bm = { 0 };

        if (hBmp == NULL || !bmp.GetBitmap(&bm))
            return FALSE;

        CDCT<true> dcMem;
        dcMem.CreateCompatibleDC(this->m_hDC);

        HBITMAP hOldBmp = dcMem.SelectBitmap(hBmp);
        
        this->StretchBlt( 
            lpRect->left, lpRect->top,
            lpRect->right - lpRect->left, lpRect->bottom - lpRect->top,
            dcMem, 
            0, 0, bm.bmWidth, bm.bmHeight, dwRop);

        dcMem.SelectBitmap(hOldBmp);

        return TRUE;
    }

    BOOL DrawText( LPRECT lpRect, LPCTSTR pszText, 
        DWORD dwFlags = DT_LEFT | DT_TOP, COLORREF clrText = 0x0L, HFONT hFont = NULL )
    {
        HFONT hOldFont = NULL;        

        COLORREF clrOld = this->SetTextColor(clrText);

        if (hFont != NULL)
            hOldFont = this->SelectFont(hFont);

        int nbkmode = this->SetBkMode(TRANSPARENT);

        this->DrawText( pszText, -1, lpRect, dwFlags );

        this->SetBkMode(nbkmode);

        if (hFont != NULL)
            this->SelectFont(hOldFont);

        this->SetTextColor(clrOld);

        return TRUE;
    }

    BOOL DrawText( int nx, int ny, LPCTSTR pszText, 
        COLORREF clrText = 0x0L, HFONT hFont = NULL )
    {
        HFONT hOldFont = NULL;        

        COLORREF clrOld = this->SetTextColor(clrText);

        if (hFont != NULL)
            hOldFont = this->SelectFont(hFont);

        int nbkmode = this->SetBkMode(TRANSPARENT);

        this->TextOut(nx, ny, pszText);

        this->SetBkMode(nbkmode);

        if (hFont != NULL)
            this->SelectFont(hOldFont);

        this->SetTextColor(clrOld);

        return TRUE;
    }


    void SkinDrawGradualColorRect(
        const RECT& drawRC, 
        COLORREF    clrFirst   = RGB(255, 255 ,255) ,   // 
        COLORREF    clrEnd     = RGB(  0,   0,   0) ,   // 
        BOOL        bVertical  = TRUE ) 
    {
        if (bVertical)
        {
            GRADIENT_RECT gRect = {0, 1};

            TRIVERTEX vert[2] = {
                {drawRC.left , drawRC.top   , 0, 0, 0, 0}, 
                {drawRC.right, drawRC.bottom, 0, 0, 0, 0} };

                vert[0].Red     = GetRValue(clrFirst) << 8;
                vert[0].Green   = GetGValue(clrFirst) << 8;
                vert[0].Blue    = GetBValue(clrFirst) << 8;
                vert[1].Red     = GetRValue(clrEnd) << 8;
                vert[1].Green   = GetGValue(clrEnd) << 8;
                vert[1].Blue    = GetBValue(clrEnd) << 8;

            ::GradientFill(this->m_hDC, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
        }
        else
        {
            GRADIENT_RECT gRect = {0, 1};

            TRIVERTEX vert[2] = {
                {drawRC.left , drawRC.top   , 0, 0, 0, 0}, 
                {drawRC.right, drawRC.bottom, 0, 0, 0, 0} };

                vert[0].Red     = GetRValue(clrFirst) << 8;
                vert[0].Green   = GetGValue(clrFirst) << 8;
                vert[0].Blue    = GetBValue(clrFirst) << 8;
                vert[1].Red     = GetRValue(clrEnd) << 8;
                vert[1].Green   = GetGValue(clrEnd) << 8;
                vert[1].Blue    = GetBValue(clrEnd) << 8;

                ::GradientFill(this->m_hDC, vert, 2, &gRect, 1, GRADIENT_FILL_RECT_H);
        }
    }

    void SkinLine(
        POINT pt0, 
        POINT pt1, 
        COLORREF clrColor = RGB(0,0,0) , 
        int nPenStyle     = BS_SOLID   , 
        int nWidth        = 1 )
    {
        SkinLine(pt0.x, pt0.y, pt1.x, pt1.y, clrColor, nPenStyle, nWidth);
    }

    void SkinLine(
        int x0, int y0, 
        int x1, int y1, 
        COLORREF clrColor = RGB(0,0,0) , 
        int nPenStyle     = BS_SOLID   , 
        int nWidth        = 1 )
    {    
        CPen pen;
        pen.CreatePen(nPenStyle, nWidth, clrColor);

        HPEN hOldPen = this->SelectPen(pen);

        this->MoveTo(x0, y0);
        this->LineTo(x1, y1);

        this->SelectPen(hOldPen);
        pen.DeleteObject(); 
    }

	void SkinLines(
		int x0, int y0, 
		int x1, int y1, 
		COLORREF clrColor = RGB(0,0,0) , 
		int nStep		  = 1,
		int nPenStyle     = BS_SOLID   , 
		int nWidth        = 1 )
	{
		CPen pen;
		pen.CreatePen(nPenStyle, nWidth, clrColor);
		HPEN hOldPen = this->SelectPen(pen);

		for (int i = y0; i < y1; i += nStep + 1)
		{
            this->MoveTo(x0, i);
            this->LineTo(x1, i);
		}

		this->SelectPen(hOldPen);
		pen.DeleteObject(); 
	}

    void SkinDrawBorder(
        const RECT& rectBorder, 
        COLORREF clrColor  = RGB(0,0,0) , 
        int nPenStyle      = BS_SOLID   , 
        int nWidth         = 1 )
    {
        SkinDrawBorder(
            rectBorder.left , rectBorder.top, 
            rectBorder.right - 1, rectBorder.bottom - 1,
            clrColor,
            nPenStyle,
            nWidth);
    }
    void SkinDrawBorder(
        int x0, int y0, 
        int x1, int y1, 
        COLORREF clrColor = RGB(0,0,0) , 
        int nPenStyle     = BS_SOLID   , 
        int nWidth        = 1 )
    {
        CPen pen;
        pen.CreatePen(nPenStyle, 1, clrColor);

        HPEN hOldPen = this->SelectPen(pen);

        for (int i = 0; i < nWidth; i++)
        {
            this->MoveTo(x0 + i, y0 + i);

            this->LineTo(x1 - i, y0 + i);
            this->LineTo(x1 - i, y1 - i);

            this->LineTo(x0 + i, y1 - i);
            this->LineTo(x0 + i, y0 + i);
        }

        this->SelectPen(hOldPen);
        pen.DeleteObject(); 

    }

    void SkinDrawRectangle(
        const RECT& rectBorder, 
        COLORREF clrFillColor   = RGB(255, 255, 255) , 
        COLORREF clrBorderColor = Undefine , 
        int HatchStyles         = -1 ,
        int nPenStyle           = BS_SOLID , 
        int nWidth              = 1 )
    {
        SkinDrawRectangle(
            rectBorder.left , rectBorder.top, 
            rectBorder.right, rectBorder.bottom,
            clrFillColor,
            clrBorderColor,
            HatchStyles,
            nPenStyle,
            nWidth);

    }

    void SkinDrawRectangle(
        int x0, int y0, 
        int x1, int y1, 
        COLORREF clrFillColor   = RGB(255, 255, 255) , 
        COLORREF clrBorderColor = Undefine , 
        int HatchStyles         = -1 ,
        int nPenStyle           = BS_SOLID , 
        int nWidth              = 1 )
    {
        CBrush brush;
        CPen   pen;

		pen.CreatePen(nPenStyle, nWidth, clrBorderColor == Undefine ? clrFillColor : clrBorderColor);

        if (HatchStyles >= 0)
            brush.CreateHatchBrush(HatchStyles, clrFillColor);
        else
            brush.CreateSolidBrush(clrFillColor);

        HBRUSH hOldBrush = this->SelectBrush(brush);
        HPEN   hOldPen   = this->SelectPen(pen);

        this->Rectangle(x0, y0, x1, y1);

        this->SelectPen(hOldPen);
        this->SelectBrush(hOldBrush);

        brush.DeleteObject();
        pen.DeleteObject(); 
    }


    void SkinDrawRoundRect(
        const RECT& rectBorder, 
        int nRound,
        COLORREF clrFillColor   = RGB(255, 255, 255) , 
        COLORREF clrBorderColor = RGB(0, 0, 0) , 
        int HatchStyles         = -1 ,
        int nPenStyle           = BS_SOLID , 
        int nWidth              = 1 )
    {
        SkinDrawRoundRect(
            rectBorder.left , rectBorder.top, 
            rectBorder.right, rectBorder.bottom, 
            nRound,
            clrFillColor,
            clrBorderColor,
            HatchStyles,
            nPenStyle,
            nWidth);

    }

    void SkinDrawRoundRect(
        int x0, int y0, 
        int x1, int y1, 
        int nRound,
        COLORREF clrFillColor   = RGB(255, 255, 255) , 
        COLORREF clrBorderColor = RGB(0, 0, 0) , 
        int HatchStyles         = -1 ,
        int nPenStyle           = BS_SOLID , 
        int nWidth              = 1 )
    {
        CBrush brush;
        CPen   pen;

        pen.CreatePen(nPenStyle, nWidth, clrBorderColor);

        if (HatchStyles >= 0)
            brush.CreateHatchBrush(HatchStyles, clrFillColor);
        else
            brush.CreateSolidBrush(clrFillColor);

        HBRUSH hOldBrush = this->SelectBrush(brush);
        HPEN   hOldPen   = this->SelectPen(pen);

        this->RoundRect(x0, y0, x1, y1, nRound, nRound);

        this->SelectPen(hOldPen);
        this->SelectBrush(hOldBrush);

        brush.DeleteObject();
        pen.DeleteObject(); 
    }


    void SkinDraw3DRectangle(
        const RECT& rectBorder, 
        COLORREF clrFillColor   = RGB(255, 255, 255) , 
        COLORREF clrBorderColor = RGB(0, 0, 0)       , 
        COLORREF clrShadowColor = RGB(0, 0, 0)       , 
        int HatchStyles         = -1                 ,
        int nPenStyle           = BS_SOLID           , 
        int nWidth              = 1 )
    {
        SkinDraw3DRectangle(
            rectBorder.left , rectBorder.top, 
            rectBorder.right, rectBorder.bottom,
            clrFillColor,
            clrBorderColor,
            clrShadowColor,
            HatchStyles,
            nPenStyle,
            nWidth);
    }
    void SkinDraw3DRectangle(
        int x0, int y0, 
        int x1, int y1, 
        COLORREF clrFillColor   = RGB(255, 255, 255) , 
        COLORREF clrBorderColor = RGB(0, 0, 0)       , 
        COLORREF clrShadowColor = RGB(0, 0, 0)       , 
        int HatchStyles         = -1                 ,
        int nPenStyle           = BS_SOLID           , 
        int nWidth              = 1   )               
    {
        // int nShadowWidth = y1 - y0 > x1 - x0 ? (x1 - x0) / 15 : (y1 - y0) / 15;
        int nShadowWidth = 3;

        if (nShadowWidth < 1)
            nShadowWidth = 1;

        SkinDrawRectangle(
            x0, y0,
            x1 - nShadowWidth, y1 - nShadowWidth,
            clrFillColor,
            clrBorderColor,
            HatchStyles,
            nPenStyle,
            nWidth);

        CBrush brush;

        if (HatchStyles >= 0)
            brush.CreateHatchBrush(HatchStyles, clrShadowColor);
        else
            brush.CreateSolidBrush(clrShadowColor);


        const RECT rightShadowRect  = {x1 - nShadowWidth, y0 + nShadowWidth, x1, y1 - nShadowWidth};
        this->FillRect(&rightShadowRect , brush);

        const RECT BottomShadowRect = {x0 + nShadowWidth, y1 - nShadowWidth,
            x1               , y1 };
        this->FillRect(&BottomShadowRect, brush);


        brush.DeleteObject();

    }


    void SkinFrameRect(const RECT& rect, COLORREF crColor)
    {
        HBRUSH hBrush = ::CreateSolidBrush(crColor);

        this->FrameRect( &rect, hBrush );

        ::DeleteObject(hBrush);
    }

};

typedef CSkinDCT<false>   CSkinDCHandle;
typedef CSkinDCT<true>    CSkinDC;



///////////////////////////////////////////////////////////////////////////////
typedef DWORD HLSCOLOR;
#define HLS(h,l,s) ((HLSCOLOR)(((BYTE)(h)|((WORD)((BYTE)(l))<<8))|(((DWORD)(BYTE)(s))<<16)))

///////////////////////////////////////////////////////////////////////////////
#define HLS_H(hls) ((BYTE)(hls))
#define HLS_L(hls) ((BYTE)(((WORD)(hls)) >> 8))
#define HLS_S(hls) ((BYTE)((hls)>>16))

inline HLSCOLOR RGB2HLS (COLORREF rgb)
{
    unsigned char minval = Gdiplus::min(GetRValue(rgb), Gdiplus::min(GetGValue(rgb), GetBValue(rgb)));
    unsigned char maxval = Gdiplus::max(GetRValue(rgb), Gdiplus::max(GetGValue(rgb), GetBValue(rgb)));
    float mdiff  = float(maxval) - float(minval);
    float msum   = float(maxval) + float(minval);

    float luminance = msum / 510.0f;
    float saturation = 0.0f;
    float hue = 0.0f; 

    if ( maxval != minval )
    { 
        float rnorm = (maxval - GetRValue(rgb)  ) / mdiff;      
        float gnorm = (maxval - GetGValue(rgb)) / mdiff;
        float bnorm = (maxval - GetBValue(rgb) ) / mdiff;   

        saturation = (luminance <= 0.5f) ? (mdiff / msum) : (mdiff / (510.0f - msum));

        if (GetRValue(rgb) == maxval) hue = 60.0f * (6.0f + bnorm - gnorm);
        if (GetGValue(rgb) == maxval) hue = 60.0f * (2.0f + rnorm - bnorm);
        if (GetBValue(rgb) == maxval) hue = 60.0f * (4.0f + gnorm - rnorm);
        if (hue > 360.0f) hue = hue - 360.0f;
    }
    return HLS ((hue*255)/360, luminance*255, saturation*255);
}

///////////////////////////////////////////////////////////////////////////////
inline BYTE _ToRGB (float rm1, float rm2, float rh)
{
    if      (rh > 360.0f) rh -= 360.0f;
    else if (rh <   0.0f) rh += 360.0f;

    if      (rh <  60.0f) rm1 = rm1 + (rm2 - rm1) * rh / 60.0f;   
    else if (rh < 180.0f) rm1 = rm2;
    else if (rh < 240.0f) rm1 = rm1 + (rm2 - rm1) * (240.0f - rh) / 60.0f;      

    return (BYTE)(rm1 * 255);
}

///////////////////////////////////////////////////////////////////////////////
inline COLORREF HLS2RGB (HLSCOLOR hls)
{
    float hue        = ((int)HLS_H(hls)*360)/255.0f;
    float luminance  = HLS_L(hls)/255.0f;
    float saturation = HLS_S(hls)/255.0f;

    if ( saturation == 0.0f )
    {
        return RGB (HLS_L(hls), HLS_L(hls), HLS_L(hls));
    }
    float rm1, rm2;

    if ( luminance <= 0.5f ) rm2 = luminance + luminance * saturation;  
    else                     rm2 = luminance + saturation - luminance * saturation;
    rm1 = 2.0f * luminance - rm2;   
    BYTE red   = _ToRGB (rm1, rm2, hue + 120.0f);   
    BYTE green = _ToRGB (rm1, rm2, hue);
    BYTE blue  = _ToRGB (rm1, rm2, hue - 120.0f);

    return RGB (red, green, blue);
}

///////////////////////////////////////////////////////////////////////////////
inline COLORREF HLS_TRANSFORM (COLORREF rgb, int percent_L, int percent_S)
{
    HLSCOLOR hls = RGB2HLS (rgb);
    BYTE h = HLS_H(hls);
    BYTE l = HLS_L(hls);
    BYTE s = HLS_S(hls);

    if ( percent_L > 0 )
    {
        l = BYTE(l + ((255 - l) * percent_L) / 100);
    }
    else if ( percent_L < 0 )
    {
        l = BYTE((l * (100+percent_L)) / 100);
    }
    if ( percent_S > 0 )
    {
        s = BYTE(s + ((255 - s) * percent_S) / 100);
    }
    else if ( percent_S < 0 )
    {
        s = BYTE((s * (100+percent_S)) / 100);
    }
    return HLS2RGB (HLS(h, l, s));
}

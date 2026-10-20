#pragma once
#include <gdiplus.h>

static void createRectPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float lineWidth)
{
	const int modifyWidth = static_cast<int>(lineWidth);
	p.StartFigure();
	p.AddLine(x, y, x + w, y);
	p.AddLine(x + w - modifyWidth, y, x + w - modifyWidth, y + h);
	p.AddLine(x + w, y + h - modifyWidth, x, y + h - modifyWidth);
	p.AddLine(x, y + h, x, y);
	p.CloseFigure();
}

static void createLeftRoundRectPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float radius, float lineWidth)
{
	const float d = radius * 2;
	const int modifyWidth = static_cast<int>(lineWidth);
	p.StartFigure();
	//上横线
	p.AddLine(x + radius, y, x + w, y);
	//右竖线
	p.AddLine(x + w - modifyWidth, y, x + w - modifyWidth, y + h);
	//下横线
	p.AddLine(x + w, y + h - modifyWidth, x + radius, y + h - modifyWidth);
	//左下圆角
	p.AddArc(x, y + h - d - modifyWidth, d, d, 90, 90);
	//左竖线
	p.AddLine(x, y + h - radius, x, y + radius);
	//左上圆角
	p.AddArc(x, y, d, d, 180, 90);
	p.CloseFigure();

}
static void createRightRoundRectPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float radius, float lineWidth)
{
	const float d = radius * 2;
	const int modifyWidth = static_cast<int>(lineWidth);
	p.StartFigure();
	//上横线
	p.AddLine(x, y, x + w - radius, y);
	//右上圆角
	p.AddArc(x + w - d - modifyWidth, y, d, d, 270, 90);
	//右竖线
	p.AddLine(x + w - modifyWidth, y + radius, x + w - modifyWidth, y + h - radius);
	//右下圆角
	p.AddArc(x + w - d - modifyWidth, y + h - d - modifyWidth, d, d, 0, 90);
	//下横线
	p.AddLine(x + w - radius, y + h - modifyWidth, x, y + h - modifyWidth);
	//左竖线
	p.AddLine(x, y + h, x, y);
	p.CloseFigure();

}
static void createRoundRectPath(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float radius, float lineWidth)
{
	const float d = radius * 2;
	const float modifyWidth = lineWidth / 2.0f;
	p.StartFigure();
	//上横线
	p.AddLine(x + radius, y + modifyWidth, x + w - radius, y + modifyWidth);
	//右上圆角
	p.AddArc(x + w - d, y + modifyWidth, d, d, 270, 90);
	//右竖线
	p.AddLine(x + w - modifyWidth, y + radius, x + w - modifyWidth, y + h - radius);
	//右下圆角
	p.AddArc(x + w - d - modifyWidth, y + h - d, d, d, 0, 90);
	//下横线
	p.AddLine(x + w - radius, y + h - modifyWidth, x + radius, y + h - modifyWidth);
	//左下圆角
	p.AddArc(x + modifyWidth, y + h - d, d, d, 90, 90);
	//左竖线
	p.AddLine(x + modifyWidth, y + h - radius, x + modifyWidth, y + radius);
	//左上圆角
	p.AddArc(x, y + modifyWidth, d, d, 180, 90);
	p.CloseFigure();
}

// 参考：https://blog.csdn.net/wwkaven/article/details/37575007
enum
{
	ALL_ROUND = 0,
	LEFT_ROUND = 1,
	RIGHT_ROUND = 2
};
typedef int ROUND_TYPE;

static void drawRoundRectange(
	Gdiplus::Graphics& g, 
	float x,
	float y, 
	float w, 
	float h,
	float radius,
	float lineWidth,
	Gdiplus::Color lineColor,
	Gdiplus::Color fillColor,
	ROUND_TYPE type = ALL_ROUND)
{
	g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
	Gdiplus::GraphicsPath p;
	switch (type)
	{
	case LEFT_ROUND:
		createLeftRoundRectPath(p, x, y, w, h, radius, lineWidth);
		break;
	case RIGHT_ROUND:
		createRightRoundRectPath(p, x, y, w, h, radius, lineWidth);
		break;
	case ALL_ROUND:
	default:
		createRoundRectPath(p, x, y, w, h, radius, lineWidth);
		break;
	}
	
	Gdiplus::Pen pen(lineColor, lineWidth);
	g.DrawPath(&pen, &p);
	Gdiplus::SolidBrush br(fillColor);
	g.FillPath(&br, &p);
}

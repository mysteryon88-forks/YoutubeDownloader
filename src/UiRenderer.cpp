#include "UiRenderer.h"

#include <gdiplus.h>

using Gdiplus::Color;
using Gdiplus::Font;
using Gdiplus::FontFamily;
using Gdiplus::Graphics;
using Gdiplus::GraphicsPath;
using Gdiplus::Pen;
using Gdiplus::RectF;
using Gdiplus::SolidBrush;
using Gdiplus::StringAlignment;
using Gdiplus::StringFormat;
using Gdiplus::UnitPoint;

namespace {

const Color kBg(255, 20, 20, 22);
const Color kPanel(255, 28, 28, 31);
const Color kPanel2(255, 35, 35, 38);
const Color kBorder(255, 46, 46, 50);
const Color kInput(255, 25, 25, 28);
const Color kAccent(255, 232, 72, 85);
const Color kAccentPressed(255, 197, 45, 59);
const Color kText(255, 242, 242, 242);

void AddRoundedRect(GraphicsPath& path, const RECT& rect, int radius) {
    const int diameter = radius * 2;
    path.AddArc(rect.left, rect.top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.left, rect.bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

} // namespace

void UiRenderer::DrawBackground(HDC dc, const RECT& rect) {
    Graphics graphics(dc);
    SolidBrush brush(kBg);
    graphics.FillRectangle(
        &brush,
        static_cast<INT>(rect.left),
        static_cast<INT>(rect.top),
        static_cast<INT>(rect.right - rect.left),
        static_cast<INT>(rect.bottom - rect.top)
    );
}

void UiRenderer::DrawPanel(HDC dc, const RECT& rect) {
    Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    GraphicsPath path;
    AddRoundedRect(path, rect, 10);

    SolidBrush fill(kPanel);
    Pen border(kBorder, 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);
}

void UiRenderer::DrawPreviewCard(HDC dc, const RECT& rect) {
    DrawPanel(dc, rect);

    Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    RECT thumb = {rect.left + 14, rect.top + 14, rect.left + 224, rect.top + 132};
    GraphicsPath thumbPath;
    AddRoundedRect(thumbPath, thumb, 8);
    SolidBrush thumbFill(kPanel2);
    graphics.FillPath(&thumbFill, &thumbPath);
}

void UiRenderer::DrawInputFrame(HDC dc, const RECT& rect) {
    Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    RECT inset = {rect.left, rect.top, rect.right, rect.bottom};
    GraphicsPath path;
    AddRoundedRect(path, inset, 8);

    SolidBrush fill(kInput);
    Pen border(kBorder, 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);
}

void UiRenderer::DrawButton(HDC dc, const RECT& rect, const wchar_t* text, bool primary, bool pressed, bool hot, bool onPanel) {
    Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    SolidBrush clearBrush(onPanel ? kPanel : kBg);
    graphics.FillRectangle(
        &clearBrush,
        static_cast<INT>(rect.left),
        static_cast<INT>(rect.top),
        static_cast<INT>(rect.right - rect.left),
        static_cast<INT>(rect.bottom - rect.top)
    );

    GraphicsPath path;
    RECT inset = {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1};
    AddRoundedRect(path, inset, 7);

    const Color fillColor = primary
        ? (pressed ? kAccentPressed : (hot ? Color(255, 242, 88, 101) : kAccent))
        : (pressed ? Color(255, 30, 30, 33) : (hot ? Color(255, 42, 42, 46) : kPanel2));
    SolidBrush fill(fillColor);
    Pen border(primary ? fillColor : kBorder, 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);

    FontFamily family(L"Segoe UI");
    Font font(&family, 10.0f, Gdiplus::FontStyleRegular, UnitPoint);
    SolidBrush textBrush(kText);
    RectF textRect(
        static_cast<Gdiplus::REAL>(rect.left),
        static_cast<Gdiplus::REAL>(rect.top),
        static_cast<Gdiplus::REAL>(rect.right - rect.left),
        static_cast<Gdiplus::REAL>(rect.bottom - rect.top)
    );
    StringFormat format;
    format.SetAlignment(StringAlignment::StringAlignmentCenter);
    format.SetLineAlignment(StringAlignment::StringAlignmentCenter);
    graphics.DrawString(text, -1, &font, textRect, &format, &textBrush);
}

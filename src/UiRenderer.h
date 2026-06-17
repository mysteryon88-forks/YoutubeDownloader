#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

class UiRenderer {
public:
    static void DrawBackground(HDC dc, const RECT& rect);
    static void DrawPanel(HDC dc, const RECT& rect);
    static void DrawPreviewCard(HDC dc, const RECT& rect);
    static void DrawInputFrame(HDC dc, const RECT& rect);
    static void DrawButton(HDC dc, const RECT& rect, const wchar_t* text, bool primary, bool pressed, bool hot, bool onPanel);
};

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>

class Application {
public:
    Application();
    ~Application();

    bool Initialize(HINSTANCE instance, int showCommand);
    int Run();
    void Shutdown();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ButtonWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    bool RegisterButtonClass();
    void CreateControls();
    HWND CreateButton(const wchar_t* text, int id, bool primary, bool onPanel);
    void LayoutControls(int width, int height);
    void DrawControlFrames(HDC dc);
    void SetControlFonts();
    void SetStatus(const std::wstring& text);

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;
    bool m_buttonClassRegistered = false;
    HWND m_urlEdit = nullptr;
    HWND m_pasteButton = nullptr;
    HWND m_previewTitle = nullptr;
    HWND m_folderEdit = nullptr;
    HWND m_chooseFolderButton = nullptr;
    HWND m_downloadButton = nullptr;
    HWND m_clearButton = nullptr;
    HWND m_settingsButton = nullptr;
    HWND m_statusLabel = nullptr;
    HWND m_queueLabel = nullptr;
    HWND m_queuePlaceholder = nullptr;
    RECT m_urlFrameRect = {};
    RECT m_folderFrameRect = {};

    HFONT m_font = nullptr;
    HFONT m_boldFont = nullptr;
    HBRUSH m_backgroundBrush = nullptr;
    HBRUSH m_panelBrush = nullptr;
    ULONG_PTR m_gdiplusToken = 0;
};

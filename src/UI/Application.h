#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"
#include "DownloadQueue.h"
#include "Logger.h"
#include "ToolManagers.h"
#include "YtDlpClient.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
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

    bool HandleMainWindowShortcut(const MSG& message);
    void RelayTooltipMessage(const MSG& message);
    bool RegisterButtonClass();
    void CreateControls();
    HWND CreateButton(const wchar_t* text, int id, bool primary, bool onPanel);
    void LayoutControls(int width, int height);
    void DrawControlFrames(HDC dc);
    void DrawPreviewContent(HDC dc, const RECT& previewRect);
    void DrawQueueContent(HDC dc, const RECT& queueRect);
    bool HandleQueueClick(POINT point);
    bool UpdateQueueHover(POINT point);
    void ClearQueueHover();
    bool ScrollQueue(int wheelDelta, POINT point);
    void SetControlFonts();
    void SetStatus(const std::wstring& text);
    void SetTransientStatus(const std::wstring& text);
    void RestoreStatusText();
    void InitializeBackend();
    void StartToolCheck();
    void StartAppUpdateCheck();
    void StartPreviewFetch();
    void StartPreviewLoadingText();
    void StopPreviewLoadingText();
    void UpdatePreviewLoadingText();
    void EnqueueCurrentUrl();
    void RefreshQueueText();
    std::wstring GetWindowTextString(HWND control) const;

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
    HWND m_clearFinishedButton = nullptr;
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
    HWND m_tooltip = nullptr;
    ULONG_PTR m_gdiplusToken = 0;

    std::unique_ptr<AppPaths> m_paths;
    AppConfig m_config;
    std::unique_ptr<Logger> m_logger;
    FfmpegStatus m_ffmpeg;
    ToolInstallStatus m_ytDlpStatus;
    std::unique_ptr<YtDlpClient> m_ytDlpClient;
    std::unique_ptr<DownloadQueue> m_downloadQueue;
    std::mutex m_previewMutex;
    VideoPreview m_preview;
    std::atomic<unsigned long> m_previewRequestId = 0;
    std::uint64_t m_lastRenderedQueueRevision = static_cast<std::uint64_t>(-1);
    bool m_queuePlaceholderVisible = true;
    bool m_ytDlpReady = false;
    bool m_transientStatusActive = false;
    bool m_previewLoading = false;
    bool m_queueMouseTracking = false;
    int m_previewLoadingDots = 3;
    int m_hotQueueTaskId = 0;
    int m_hotQueueAction = 0;
    int m_queueScrollOffset = 0;
};

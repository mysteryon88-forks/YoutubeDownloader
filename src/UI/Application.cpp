#include "Application.h"

#include "AsyncWait.h"
#include "BackendText.h"
#include "DialogWindows.h"
#include "DownloadQueueStore.h"
#include "KeyboardShortcuts.h"
#include "resource.h"
#include "UiActions.h"
#include "UiRenderer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <system_error>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

constexpr const wchar_t* kWindowClassName = L"YoutubeDownloaderWin32Class";
constexpr const wchar_t* kButtonClassName = L"YoutubeDownloaderButtonClass";
constexpr const wchar_t* kQueueErrorMenuClassName = L"YoutubeDownloaderQueueErrorMenu";
constexpr int kInitialWindowWidth = 1000;
constexpr int kInitialWindowHeight = 640;
constexpr COLORREF kBackgroundColor = RGB(20, 20, 22);
constexpr COLORREF kPanelColor = RGB(28, 28, 31);
constexpr COLORREF kInputColor = RGB(25, 25, 28);
constexpr COLORREF kTextColor = RGB(242, 242, 242);
constexpr COLORREF kMutedTextColor = RGB(172, 172, 178);
constexpr UINT kMsgToolCheckComplete = WM_APP + 1;
constexpr UINT kMsgPreviewComplete = WM_APP + 2;
constexpr UINT kMsgAppUpdateCheckComplete = WM_APP + 3;
constexpr UINT_PTR kQueueRefreshTimer = 1;
constexpr UINT_PTR kStatusRestoreTimer = 2;
constexpr UINT_PTR kPreviewLoadingTimer = 3;
constexpr UINT kStatusRestoreDelayMs = 3000;
constexpr UINT kPreviewLoadingDelayMs = 500;
constexpr int kQueueRowHeight = 92;
constexpr int kQueueRowGap = 10;
constexpr int kQueueActionCancel = 1;
constexpr int kQueueActionRetry = 2;
constexpr int kQueueActionDelete = 3;

enum ControlId {
    IdUrlEdit = 1001,
    IdPasteButton = 1002,
    IdChooseFolderButton = 1003,
    IdDownloadButton = 1004,
    IdClearButton = 1005,
    IdSettingsButton = 1006,
    IdClearFinishedButton = 1007,
    IdLogsButton = 1008
};

struct ButtonState {
    int commandId = 0;
    bool primary = false;
    bool onPanel = false;
    bool hot = false;
    bool pressed = false;
    std::wstring text;
};

struct QueueErrorMenuState {
    HWND owner = nullptr;
    std::wstring errorText;
    bool hot = false;
};

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = nullptr) : m_handle(handle) {}
    ~UniqueHandle() {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    HANDLE get() const { return m_handle; }

private:
    HANDLE m_handle = nullptr;
};

void StopAndJoin(std::jthread& worker) {
    if (!worker.joinable()) {
        return;
    }
    worker.request_stop();
    worker.join();
}

HWND CreateChild(HWND parent, const wchar_t* className, const wchar_t* text, DWORD style, DWORD exStyle, int id) {
    return CreateWindowExW(
        exStyle,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        0,
        0,
        10,
        10,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr
    );
}

void EnableDarkTitleBar(HWND window) {
    BOOL enabled = TRUE;
    constexpr DWORD kDwmUseImmersiveDarkMode = 20;
    if (FAILED(DwmSetWindowAttribute(window, kDwmUseImmersiveDarkMode, &enabled, sizeof(enabled)))) {
        constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
        DwmSetWindowAttribute(window, kDwmUseImmersiveDarkModeBefore20H1, &enabled, sizeof(enabled));
    }
}

std::filesystem::path GetExecutableRoot() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (size == buffer.size()) {
        buffer.resize(buffer.size() * 2, L'\0');
        size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}

std::wstring TaskStateText(DownloadTaskState state) {
    switch (state) {
    case DownloadTaskState::Queued:
        return L"В очереди";
    case DownloadTaskState::Preparing:
        return L"Подготовка";
    case DownloadTaskState::Downloading:
        return L"Скачивание";
    case DownloadTaskState::Completed:
        return L"Готово";
    case DownloadTaskState::Failed:
        return L"Ошибка";
    case DownloadTaskState::Canceled:
        return L"Отменено";
    }
    return L"";
}

bool IsRunningTaskState(DownloadTaskState state) {
    return state == DownloadTaskState::Queued ||
           state == DownloadTaskState::Preparing ||
           state == DownloadTaskState::Downloading;
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const RECT& rect, int radius) {
    const int diameter = radius * 2;
    path.AddArc(rect.left, rect.top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.left, rect.bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void DrawTextBlock(HDC dc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void PaintBuffered(HWND window, const std::function<void(HDC, const RECT&)>& paintContent) {
    PAINTSTRUCT paint = {};
    HDC screenDc = BeginPaint(window, &paint);

    RECT client = {};
    GetClientRect(window, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));

    HDC bufferDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bitmap);

    paintContent(bufferDc, client);
    BitBlt(screenDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);

    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(bufferDc);
    EndPaint(window, &paint);
}

HFONT CreateUiFont(int height, int weight = FW_NORMAL) {
    LOGFONTW font = {};
    font.lfHeight = height;
    font.lfWeight = weight;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&font);
}

std::wstring BuildTaskDetails(const DownloadTaskSnapshot& task) {
    std::vector<std::wstring> parts;
    if (!task.mediaKind.empty()) {
        parts.push_back(task.mediaKind == L"audio" ? L"Аудио" : L"Видео");
    }
    if (!task.extension.empty()) {
        parts.push_back(task.extension);
    }
    if (task.mediaKind == L"video" && !task.resolution.empty()) {
        parts.push_back(task.resolution);
    }
    if (task.totalBytes > 0) {
        parts.push_back(FormatBytes(task.downloadedBytes) + L" / " + FormatBytes(task.totalBytes));
    } else if (task.downloadedBytes > 0) {
        parts.push_back(FormatBytes(task.downloadedBytes));
    }
    if (task.speedBytesPerSecond > 0) {
        parts.push_back(FormatBytes(task.speedBytesPerSecond) + L"/s");
    }
    if (task.etaSeconds > 0) {
        parts.push_back(L"ETA " + FormatDuration(task.etaSeconds));
    }

    std::wstring details;
    for (const std::wstring& part : parts) {
        if (!details.empty()) {
            details += L" · ";
        }
        details += part;
    }
    return details;
}

std::wstring BuildToolReadyStatus(const ToolInstallStatus& ytDlpStatus, const FfmpegStatus& ffmpeg) {
    std::wstring status = ytDlpStatus.installed ? L"youtube-dlp готов" : L"youtube-dlp не найден";
    if (ytDlpStatus.installed && !ytDlpStatus.version.empty()) {
        status += L" (" + ytDlpStatus.version + L")";
    }
    if (ffmpeg.available) {
        status += L" · ffmpeg найден";
    }
    return status;
}

std::wstring CurrentUtcTimestamp() {
    SYSTEMTIME now = {};
    GetSystemTime(&now);
    wchar_t buffer[32] = {};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02uT%02u:%02u:%02uZ",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond
    );
    return buffer;
}

size_t CountTasksInState(const std::vector<DownloadTaskSnapshot>& tasks, DownloadTaskState state) {
    return static_cast<size_t>(std::count_if(tasks.begin(), tasks.end(), [state](const DownloadTaskSnapshot& task) {
        return task.state == state;
    }));
}

RECT QueuePanelRectForClient(const RECT& client) {
    return {20, 334, client.right - 20, client.bottom - 20};
}

RECT QueueRowRectAt(const RECT& queueRect, int index) {
    const int y = queueRect.top + 18 + (index * (kQueueRowHeight + kQueueRowGap));
    return {queueRect.left + 16, y, queueRect.right - 32, y + kQueueRowHeight};
}

int QueueVisibleRowCount(const RECT& queueRect) {
    const int availableHeight = std::max(0, static_cast<int>(queueRect.bottom - 16 - (queueRect.top + 18)));
    return std::max(1, (availableHeight + kQueueRowGap) / (kQueueRowHeight + kQueueRowGap));
}

int QueueMaxScrollOffset(const RECT& queueRect, size_t taskCount) {
    return std::max(0, static_cast<int>(taskCount) - QueueVisibleRowCount(queueRect));
}

RECT QueueScrollbarTrackRect(const RECT& queueRect) {
    return {queueRect.right - 20, queueRect.top + 18, queueRect.right - 12, queueRect.bottom - 16};
}

RECT QueueScrollbarThumbRect(const RECT& queueRect, size_t taskCount, int scrollOffset) {
    const RECT track = QueueScrollbarTrackRect(queueRect);
    const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
    const int visibleRows = QueueVisibleRowCount(queueRect);
    const int totalRows = std::max(1, static_cast<int>(taskCount));
    const int thumbHeight = std::clamp((trackHeight * visibleRows) / totalRows, 28, trackHeight);
    const int maxOffset = std::max(1, totalRows - visibleRows);
    const int maxTravel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = track.top + (std::clamp(scrollOffset, 0, maxOffset) * maxTravel) / maxOffset;
    return {track.left, thumbTop, track.right, thumbTop + thumbHeight};
}

UINT TooltipInfoSize() {
#ifdef TTTOOLINFOW_V2_SIZE
    return TTTOOLINFOW_V2_SIZE;
#else
    return sizeof(TOOLINFOW);
#endif
}

bool IsTooltipRelayMessage(UINT message) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return true;
    default:
        return false;
    }
}

void DrawSmallActionButton(HDC dc, const RECT& rect, const std::wstring& text, bool primary, bool hot) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, 6);
    const Gdiplus::Color primaryFill = hot ? Gdiplus::Color(255, 245, 91, 104) : Gdiplus::Color(255, 232, 72, 85);
    const Gdiplus::Color secondaryFill = hot ? Gdiplus::Color(255, 55, 55, 61) : Gdiplus::Color(255, 42, 42, 46);
    const Gdiplus::Color secondaryBorder = hot ? Gdiplus::Color(255, 86, 86, 94) : Gdiplus::Color(255, 58, 58, 64);
    Gdiplus::SolidBrush fill(primary ? primaryFill : secondaryFill);
    Gdiplus::Pen border(primary ? primaryFill : secondaryBorder, 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);

    HFONT font = CreateUiFont(-12, FW_NORMAL);
    DrawTextBlock(dc, text, rect, kTextColor, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    DeleteObject(font);
}

bool DrawImageCover(HDC dc, const RECT& target, const std::filesystem::path& imagePath, int radius) {
    std::error_code ec;
    if (imagePath.empty() || !std::filesystem::is_regular_file(imagePath, ec)) {
        return false;
    }

    Gdiplus::Image image(imagePath.wstring().c_str());
    if (image.GetLastStatus() != Gdiplus::Ok || image.GetWidth() == 0 || image.GetHeight() == 0) {
        return false;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);

    Gdiplus::GraphicsPath clipPath;
    AddRoundedRect(clipPath, target, radius);
    Gdiplus::Region clipRegion(&clipPath);
    graphics.SetClip(&clipRegion, Gdiplus::CombineModeReplace);

    const double targetWidth = static_cast<double>(target.right - target.left);
    const double targetHeight = static_cast<double>(target.bottom - target.top);
    const double sourceWidth = static_cast<double>(image.GetWidth());
    const double sourceHeight = static_cast<double>(image.GetHeight());
    const double targetRatio = targetWidth / targetHeight;
    const double sourceRatio = sourceWidth / sourceHeight;

    Gdiplus::REAL srcX = 0.0f;
    Gdiplus::REAL srcY = 0.0f;
    Gdiplus::REAL srcWidth = static_cast<Gdiplus::REAL>(sourceWidth);
    Gdiplus::REAL srcHeight = static_cast<Gdiplus::REAL>(sourceHeight);
    if (sourceRatio > targetRatio) {
        srcWidth = static_cast<Gdiplus::REAL>(sourceHeight * targetRatio);
        srcX = static_cast<Gdiplus::REAL>((sourceWidth - srcWidth) / 2.0);
    } else {
        srcHeight = static_cast<Gdiplus::REAL>(sourceWidth / targetRatio);
        srcY = static_cast<Gdiplus::REAL>((sourceHeight - srcHeight) / 2.0);
    }

    graphics.DrawImage(
        &image,
        Gdiplus::RectF(
            static_cast<Gdiplus::REAL>(target.left),
            static_cast<Gdiplus::REAL>(target.top),
            static_cast<Gdiplus::REAL>(target.right - target.left),
            static_cast<Gdiplus::REAL>(target.bottom - target.top)
        ),
        srcX,
        srcY,
        srcWidth,
        srcHeight,
        Gdiplus::UnitPixel
    );
    graphics.ResetClip();
    return true;
}

void DrawThumbnailPlaceholder(HDC dc, const RECT& rect) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, 8);
    Gdiplus::SolidBrush fill(Gdiplus::Color(255, 35, 35, 38));
    Gdiplus::Pen border(Gdiplus::Color(255, 46, 46, 50), 1.0f);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);
}

HWND CreateTooltipWindow(HWND parent) {
    HWND tooltip = CreateWindowExW(
        WS_EX_TOPMOST,
        TOOLTIPS_CLASSW,
        nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        parent,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    if (!tooltip) {
        return nullptr;
    }
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
    SendMessageW(tooltip, TTM_SETTIPBKCOLOR, RGB(35, 35, 38), 0);
    SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, kTextColor, 0);
    SendMessageW(tooltip, TTM_ACTIVATE, TRUE, 0);
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return tooltip;
}

void AddTooltip(HWND tooltip, HWND parent, HWND tool, const wchar_t* text) {
    if (!tooltip || !tool || !text) {
        return;
    }
    TOOLINFOW info = {};
    info.cbSize = TooltipInfoSize();
    info.uFlags = TTF_IDISHWND | TTF_TRANSPARENT;
    info.hwnd = parent;
    info.uId = reinterpret_cast<UINT_PTR>(tool);
    info.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

LRESULT CALLBACK QueueErrorMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    QueueErrorMenuState* state = reinterpret_cast<QueueErrorMenuState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<QueueErrorMenuState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        if (state && !state->hot) {
            state->hot = true;
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (state && state->hot) {
            state->hot = false;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
        PaintBuffered(window, [state](HDC dc, const RECT& client) {
            const std::vector<UiRenderer::PopupMenuItem> items = {
                {1, L"Скопировать ошибку", false}
            };
            UiRenderer::DrawPopupMenu(dc, client, items, state && state->hot ? 1 : 0);
        });
        return 0;

    case WM_LBUTTONUP:
        if (state && !state->errorText.empty()) {
            CopyTextToClipboard(state->owner ? state->owner : window, state->errorText);
        }
        DestroyWindow(window);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            if (state && !state->errorText.empty()) {
                CopyTextToClipboard(state->owner ? state->owner : window, state->errorText);
            }
            DestroyWindow(window);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        DestroyWindow(window);
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

void RegisterQueueErrorMenuClass(HINSTANCE instance) {
    WNDCLASSEXW menuClass = {};
    menuClass.cbSize = sizeof(menuClass);
    menuClass.lpfnWndProc = QueueErrorMenuProc;
    menuClass.hInstance = instance;
    menuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    menuClass.hbrBackground = nullptr;
    menuClass.lpszClassName = kQueueErrorMenuClassName;
    RegisterClassExW(&menuClass);
}

} // namespace

Application::Application() = default;

Application::~Application() {
    Shutdown();
}

bool Application::Initialize(HINSTANCE instance, int showCommand) {
    m_instance = instance;

    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    m_backgroundBrush = CreateSolidBrush(kBackgroundColor);
    m_panelBrush = CreateSolidBrush(kPanelColor);

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"Не удалось инициализировать GDI+.", L"YouTube Downloader", MB_OK | MB_ICONERROR);
        return false;
    }

    if (!RegisterButtonClass()) {
        MessageBoxW(nullptr, L"Не удалось зарегистрировать класс кнопок.", L"YouTube Downloader", MB_OK | MB_ICONERROR);
        return false;
    }
    RegisterQueueErrorMenuClass(m_instance);

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = Application::WindowProc;
    windowClass.hInstance = m_instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = m_backgroundBrush;
    windowClass.lpszClassName = kWindowClassName;
    windowClass.hIcon = LoadIconW(m_instance, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    windowClass.hIconSm = LoadIconW(m_instance, MAKEINTRESOURCEW(IDI_MAIN_ICON));

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Не удалось зарегистрировать класс окна.", L"YouTube Downloader", MB_OK | MB_ICONERROR);
        return false;
    }

    const int width = kInitialWindowWidth;
    const int height = kInitialWindowHeight;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    m_window = CreateWindowExW(
        0,
        kWindowClassName,
        L"YouTube Downloader",
        WS_OVERLAPPEDWINDOW,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        m_instance,
        this
    );
    if (!m_window) {
        MessageBoxW(nullptr, L"Не удалось создать главное окно.", L"YouTube Downloader", MB_OK | MB_ICONERROR);
        return false;
    }
    EnableDarkTitleBar(m_window);

    CreateControls();
    InitializeBackend();

    ShowWindow(m_window, showCommand);
    UpdateWindow(m_window);
    if (m_config.autoUpdateApp) {
        StartAppUpdateCheck();
    }
    return true;
}

int Application::Run() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        RelayTooltipMessage(message);
        if (HandleMainWindowShortcut(message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

bool Application::HandleMainWindowShortcut(const MSG& message) {
    if (message.message != WM_KEYDOWN) {
        return false;
    }

    const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const MainWindowShortcutAction action = ResolveMainWindowShortcut(controlDown, static_cast<unsigned int>(message.wParam));
    switch (action) {
    case MainWindowShortcutAction::PasteUrl:
        PasteReplacingEditText(m_urlEdit);
        return true;
    case MainWindowShortcutAction::Download:
        if (m_downloadButton && IsWindowEnabled(m_downloadButton)) {
            EnqueueCurrentUrl();
            return true;
        }
        return false;
    case MainWindowShortcutAction::None:
        return false;
    }
    return false;
}

void Application::RelayTooltipMessage(const MSG& message) {
    if (m_tooltip && IsTooltipRelayMessage(message.message)) {
        MSG relayMessage = message;
        SendMessageW(m_tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }
}

void Application::Shutdown() {
    if (m_shutdownStarted) {
        return;
    }
    m_shutdownStarted = true;

    if (m_logger) {
        m_logger->Info(L"Application shutdown started");
    }

    StopAndJoin(m_previewWorker);
    StopAndJoin(m_toolCheckWorker);
    StopAndJoin(m_appUpdateWorker);
    if (m_downloadQueue) {
        SaveDownloadQueue(true);
        m_downloadQueue->Shutdown();
    }
    if (m_logger) {
        m_logger->Info(L"Application shutdown completed");
    }

    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
    if (m_boldFont) {
        DeleteObject(m_boldFont);
        m_boldFont = nullptr;
    }
    if (m_panelBrush) {
        DeleteObject(m_panelBrush);
        m_panelBrush = nullptr;
    }
    if (m_tooltip) {
        DestroyWindow(m_tooltip);
        m_tooltip = nullptr;
    }
    if (m_backgroundBrush) {
        DeleteObject(m_backgroundBrush);
        m_backgroundBrush = nullptr;
    }
    if (m_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

LRESULT CALLBACK Application::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        if (app) {
            app->m_window = window;
        }
        return TRUE;
    }
    if (app) {
        return app->HandleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK Application::ButtonWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    ButtonState* state = reinterpret_cast<ButtonState*>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ButtonState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEMOVE:
        if (state && !state->hot) {
            state->hot = true;
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_MOUSELEAVE:
        if (state) {
            state->hot = false;
            state->pressed = false;
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (state) {
            state->pressed = true;
            SetCapture(window);
            SetFocus(window);
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (state) {
            if (GetCapture() == window) {
                ReleaseCapture();
            }
            const bool wasPressed = state->pressed;
            state->pressed = false;
            InvalidateRect(window, nullptr, TRUE);

            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT client = {};
            GetClientRect(window, &client);
            if (wasPressed && PtInRect(&client, point)) {
                HWND parent = GetParent(window);
                SendMessageW(parent, WM_COMMAND, MAKEWPARAM(state->commandId, BN_CLICKED), reinterpret_cast<LPARAM>(window));
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (state && (wParam == VK_SPACE || wParam == VK_RETURN)) {
            HWND parent = GetParent(window);
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(state->commandId, BN_CLICKED), reinterpret_cast<LPARAM>(window));
            return 0;
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(window, &paint);
            RECT client = {};
            GetClientRect(window, &client);
            if (state) {
                UiRenderer::DrawButton(dc, client, state->text.c_str(), state->primary, state->pressed, state->hot, state->onPanel);
            }
            EndPaint(window, &paint);
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Application::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO:
        {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = kInitialWindowWidth;
            minMaxInfo->ptMinTrackSize.y = kInitialWindowHeight;
        }
        return 0;

    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_LBUTTONUP:
        {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (HandleQueueClick(point)) {
                return 0;
            }
        }
        break;

    case WM_RBUTTONUP:
        {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (HandleQueueContextMenu(point)) {
                return 0;
            }
        }
        break;

    case WM_CONTEXTMENU:
        {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x != -1 || point.y != -1) {
                ScreenToClient(m_window, &point);
                if (HandleQueueContextMenu(point)) {
                    return 0;
                }
            }
        }
        break;

    case WM_MOUSEWHEEL:
        {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(m_window, &point);
            if (ScrollQueue(GET_WHEEL_DELTA_WPARAM(wParam), point)) {
                return 0;
            }
        }
        break;

    case WM_MOUSEMOVE:
        {
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            UpdateQueueHover(point);
        }
        break;

    case WM_MOUSELEAVE:
        m_queueMouseTracking = false;
        ClearQueueHover();
        break;

    case WM_PAINT:
        PaintBuffered(m_window, [this](HDC dc, const RECT& client) {
            UiRenderer::DrawBackground(dc, client);

            RECT preview = {20, 72, client.right - 20, 218};
            UiRenderer::DrawPreviewCard(dc, preview);
            DrawPreviewContent(dc, preview);
            RECT queue = QueuePanelRectForClient(client);
            UiRenderer::DrawPanel(dc, queue);
            DrawQueueContent(dc, queue);
            DrawControlFrames(dc);
        });
        return 0;

    case WM_CTLCOLORSTATIC:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kTextColor);
            if (reinterpret_cast<HWND>(lParam) == m_previewTitle ||
                reinterpret_cast<HWND>(lParam) == m_queuePlaceholder) {
                SetBkColor(dc, kPanelColor);
                return reinterpret_cast<INT_PTR>(m_panelBrush);
            }
            return reinterpret_cast<INT_PTR>(m_backgroundBrush);
        }

    case WM_CTLCOLOREDIT:
        {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, kInputColor);
            SetTextColor(dc, kTextColor);
            return reinterpret_cast<INT_PTR>(m_panelBrush);
        }

    case WM_COMMAND:
        if (LOWORD(wParam) == IdUrlEdit && HIWORD(wParam) == EN_CHANGE) {
            StartPreviewFetch();
            return 0;
        }
        if (LOWORD(wParam) == IdPasteButton) {
            PasteReplacingEditText(m_urlEdit);
            return 0;
        }
        if (LOWORD(wParam) == IdChooseFolderButton) {
            IFileOpenDialog* dialog = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                DWORD options = 0;
                if (SUCCEEDED(dialog->GetOptions(&options))) {
                    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                }
                if (SUCCEEDED(dialog->Show(m_window))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dialog->GetResult(&item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            SetWindowTextW(m_folderEdit, path);
                            m_config.downloadDir = std::filesystem::path(path);
                            if (m_paths) {
                                ConfigStore::Save(*m_paths, m_config);
                            }
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dialog->Release();
            }
            return 0;
        }
        if (LOWORD(wParam) == IdDownloadButton) {
            EnqueueCurrentUrl();
            return 0;
        }
        if (LOWORD(wParam) == IdClearButton) {
            if (m_downloadQueue) {
                const size_t removed = m_downloadQueue->ClearQueued();
                if (m_logger) {
                    m_logger->Info(L"Queued tasks cleared: count=" + std::to_wstring(removed));
                }
                SetTransientStatus(L"Очередь очищена: удалено " + std::to_wstring(removed) + L" задач");
                RefreshQueueText();
            }
            return 0;
        }
        if (LOWORD(wParam) == IdClearFinishedButton) {
            if (m_downloadQueue) {
                const size_t removed = m_downloadQueue->ClearFinished();
                if (m_logger) {
                    m_logger->Info(L"Finished tasks cleared: count=" + std::to_wstring(removed));
                }
                SetTransientStatus(L"Завершённые задачи очищены: удалено " + std::to_wstring(removed) + L" задач");
                RefreshQueueText();
            }
            return 0;
        }
        if (LOWORD(wParam) == IdLogsButton) {
            const std::wstring logText = m_logger ? m_logger->ReadAll() : std::wstring{};
            ShowLogsDialog(m_window, m_instance, logText);
            return 0;
        }
        if (LOWORD(wParam) == IdSettingsButton) {
            if (m_paths && ShowSettingsDialog(m_window, m_instance, *m_paths, m_config)) {
                ConfigStore::Save(*m_paths, m_config);
                m_ffmpeg = FfmpegManager::Resolve(*m_paths, m_config);
                if (m_downloadQueue) {
                    m_downloadQueue->SetMaxParallelDownloads(m_config.maxParallelDownloads);
                }
                if (m_logger) {
                    m_logger->Info(
                        L"Settings saved: max_parallel_downloads=" +
                        std::to_wstring(m_config.maxParallelDownloads)
                    );
                }
                SetTransientStatus(L"Настройки сохранены");
            }
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == kQueueRefreshTimer) {
            RefreshQueueText();
            return 0;
        }
        if (wParam == kStatusRestoreTimer) {
            KillTimer(m_window, kStatusRestoreTimer);
            m_transientStatusActive = false;
            RestoreStatusText();
            return 0;
        }
        if (wParam == kPreviewLoadingTimer) {
            UpdatePreviewLoadingText();
            return 0;
        }
        break;

    case kMsgToolCheckComplete:
        {
            ToolCheckResult result;
            {
                std::lock_guard lock(m_asyncResultMutex);
                if (!m_toolCheckResult) {
                    return 0;
                }
                result = std::move(*m_toolCheckResult);
                m_toolCheckResult.reset();
            }
            m_ytDlpReady = result.ready;
            m_ytDlpStatus = result.status;
            if (m_paths && result.latestRelease.found && !result.latestCheckAt.empty()) {
                m_config.lastYtDlpCheckAt = result.latestCheckAt;
                m_config.lastYtDlpVersion = result.latestRelease.version;
                try {
                    ConfigStore::Save(*m_paths, m_config);
                } catch (...) {
                    // The tool check result should still be usable even if config persistence fails.
                }
            }
            SetStatus(result.ready ? BuildToolReadyStatus(result.status, m_ffmpeg) : result.message);
            if (m_logger) {
                if (result.ready) {
                    m_logger->Info(L"yt-dlp tool check completed: version=" + result.status.version);
                } else {
                    m_logger->Error(L"yt-dlp tool check failed: " + result.message);
                }
            }
        }
        return 0;

    case kMsgAppUpdateCheckComplete:
        {
            AppUpdateCheckResult result;
            {
                std::lock_guard lock(m_asyncResultMutex);
                if (!m_appUpdateCheckResult) {
                    return 0;
                }
                result = std::move(*m_appUpdateCheckResult);
                m_appUpdateCheckResult.reset();
            }
            if (m_paths && result.ok && ShouldInstallAppUpdate(result.release)) {
                if (OfferAppUpdate(m_window, m_instance, *m_paths, result.release, false)) {
                    PostMessageW(m_window, WM_CLOSE, 0, 0);
                }
            }
            if (m_logger) {
                if (result.ok) {
                    m_logger->Info(L"Application update check completed: version=" + result.release.version);
                } else {
                    m_logger->Error(L"Application update check failed: " + result.error);
                }
            }
        }
        return 0;

    case kMsgPreviewComplete:
        {
            PreviewFetchResult result;
            {
                std::lock_guard lock(m_asyncResultMutex);
                if (!m_previewFetchResult) {
                    return 0;
                }
                result = std::move(*m_previewFetchResult);
                m_previewFetchResult.reset();
            }
            if (result.requestId != m_previewRequestId.load()) {
                return 0;
            }
            StopPreviewLoadingText();
            if (result.ok) {
                {
                    std::lock_guard lock(m_previewMutex);
                    m_preview = result.preview;
                }
                std::wstring title = result.preview.title.empty() ? L"Видео найдено" : result.preview.title;
                if (result.preview.isPlaylist) {
                    title += L" — плейлист: " + std::to_wstring(result.preview.entries.size()) + L" видео";
                }
                SetWindowTextW(m_previewTitle, title.c_str());
                if (m_downloadQueue) {
                    bool enriched = m_downloadQueue->EnrichMetadata(
                        result.url,
                        result.preview.title,
                        result.preview.cachedThumbnailPath
                    );
                    if (!result.preview.webpageUrl.empty() && result.preview.webpageUrl != result.url) {
                        enriched = m_downloadQueue->EnrichMetadata(
                            result.preview.webpageUrl,
                            result.preview.title,
                            result.preview.cachedThumbnailPath
                        ) || enriched;
                    }
                    if (enriched) {
                        RefreshQueueText();
                    }
                }
                if (m_logger) {
                    m_logger->Info(L"Preview loaded: url=" + result.url);
                }
            } else {
                {
                    std::lock_guard lock(m_previewMutex);
                    m_preview = {};
                }
                SetWindowTextW(m_previewTitle, result.error.empty() ? L"Не удалось получить preview" : result.error.c_str());
                if (m_logger) {
                    m_logger->Error(L"Preview failed: url=" + result.url + L" error=" + result.error);
                }
            }
            InvalidateRect(m_window, nullptr, FALSE);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(m_window, kQueueRefreshTimer);
        KillTimer(m_window, kStatusRestoreTimer);
        KillTimer(m_window, kPreviewLoadingTimer);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_window, message, wParam, lParam);
}

bool Application::RegisterButtonClass() {
    WNDCLASSEXW buttonClass = {};
    buttonClass.cbSize = sizeof(buttonClass);
    buttonClass.lpfnWndProc = Application::ButtonWindowProc;
    buttonClass.hInstance = m_instance;
    buttonClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    buttonClass.hbrBackground = nullptr;
    buttonClass.lpszClassName = kButtonClassName;

    if (!RegisterClassExW(&buttonClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    m_buttonClassRegistered = true;
    return true;
}

HWND Application::CreateButton(const wchar_t* text, int id, bool primary, bool onPanel) {
    auto* state = new ButtonState{};
    state->commandId = id;
    state->primary = primary;
    state->onPanel = onPanel;
    state->text = text;

    HWND button = CreateWindowExW(
        0,
        kButtonClassName,
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        m_window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        m_instance,
        state
    );
    if (!button) {
        delete state;
    }
    return button;
}

void Application::CreateControls() {
    LOGFONTW font = {};
    font.lfHeight = -16;
    font.lfWeight = FW_NORMAL;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    m_font = CreateFontIndirectW(&font);
    font.lfWeight = FW_SEMIBOLD;
    m_boldFont = CreateFontIndirectW(&font);

    m_urlEdit = CreateChild(m_window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0, IdUrlEdit);
    SendMessageW(m_urlEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Ссылка на видео или плейлист"));
    SendMessageW(m_urlEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
    m_pasteButton = CreateButton(L"Вставить", IdPasteButton, false, false);

    m_previewTitle = CreateChild(
        m_window,
        L"STATIC",
        L"Вставьте ссылку на видео или плейлист выше",
        SS_LEFT,
        0,
        0
    );
    m_folderEdit = CreateChild(m_window, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 0, 0);
    SendMessageW(m_folderEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
    SetWindowTextW(m_folderEdit, L"Downloads");
    m_chooseFolderButton = CreateButton(L"Выбрать...", IdChooseFolderButton, false, true);

    m_downloadButton = CreateButton(L"Скачать", IdDownloadButton, true, false);
    m_clearButton = CreateButton(L"Очистить очередь", IdClearButton, false, false);
    m_logsButton = CreateButton(L"Логи", IdLogsButton, false, false);
    m_clearFinishedButton = CreateButton(L"X", IdClearFinishedButton, false, false);
    m_settingsButton = CreateButton(L"Настройки", IdSettingsButton, false, false);
    m_statusLabel = CreateChild(m_window, L"STATIC", L"Подготовка интерфейса", SS_LEFT, 0, 0);
    m_queueLabel = CreateChild(m_window, L"STATIC", L"Очередь загрузок", SS_LEFT, 0, 0);
    m_queuePlaceholder = CreateChild(m_window, L"STATIC", L"Задач пока нет", SS_CENTER, 0, 0);

    SetControlFonts();
    m_tooltip = CreateTooltipWindow(m_window);
    AddTooltip(m_tooltip, m_window, m_urlEdit, L"Вставьте ссылку на видео, плейлист или другой поддерживаемый источник.");
    AddTooltip(m_tooltip, m_window, m_pasteButton, L"Вставляет ссылку из буфера обмена.");
    AddTooltip(m_tooltip, m_window, m_folderEdit, L"Папка, куда будут сохраняться скачанные файлы.");
    AddTooltip(m_tooltip, m_window, m_chooseFolderButton, L"Выберите папку для сохранения загрузок.");
    AddTooltip(m_tooltip, m_window, m_downloadButton, L"Добавляет ссылку в очередь загрузок.");
    AddTooltip(m_tooltip, m_window, m_clearButton, L"Удаляет из очереди задачи, которые ещё не начали загружаться. Активные загрузки не останавливаются.");
    AddTooltip(m_tooltip, m_window, m_logsButton, L"Открывает текущий файл логов.");
    AddTooltip(m_tooltip, m_window, m_clearFinishedButton, L"Очищает из списка завершённые и ошибочные задачи. Файлы на диске не удаляются.");
    AddTooltip(m_tooltip, m_window, m_settingsButton, L"Открывает настройки качества, контейнера, FFmpeg и поведения приложения.");

    RECT client = {};
    GetClientRect(m_window, &client);
    LayoutControls(client.right - client.left, client.bottom - client.top);
}

void Application::LayoutControls(int width, int height) {
    UNREFERENCED_PARAMETER(height);
    constexpr int margin = 20;
    constexpr int inputButtonGap = 10;
    constexpr int inputHeight = 36;
    constexpr int editHeight = 24;
    constexpr int editPaddingX = 12;
    constexpr int actionButtonHeight = 34;
    constexpr int topY = 20;
    constexpr int folderY = 168;

    const int chooseWidth = 112;
    const int chooseRight = width - margin - 12;
    const int inputRight = chooseRight - chooseWidth - inputButtonGap;
    const int pasteRight = width - margin;
    const int buttonX = inputRight + inputButtonGap;
    const int urlEditY = topY + ((inputHeight - editHeight) / 2);

    m_urlFrameRect = {margin, topY, inputRight, topY + inputHeight};
    const int urlEditWidth = std::max(
        120,
        static_cast<int>(m_urlFrameRect.right - m_urlFrameRect.left) - (editPaddingX * 2)
    );
    MoveWindow(
        m_urlEdit,
        static_cast<int>(m_urlFrameRect.left) + editPaddingX,
        urlEditY,
        urlEditWidth,
        editHeight,
        TRUE
    );
    MoveWindow(m_pasteButton, buttonX, topY, std::max(90, pasteRight - buttonX), inputHeight, TRUE);

    MoveWindow(m_previewTitle, margin + 240, 100, width - margin - 280, 40, TRUE);
    const int folderFrameX = margin + 240;
    const int folderEditY = folderY + ((inputHeight - editHeight) / 2);
    m_folderFrameRect = {folderFrameX, folderY, inputRight, folderY + inputHeight};
    const int folderEditWidth = std::max(
        120,
        static_cast<int>(m_folderFrameRect.right - m_folderFrameRect.left) - (editPaddingX * 2)
    );
    MoveWindow(
        m_folderEdit,
        static_cast<int>(m_folderFrameRect.left) + editPaddingX,
        folderEditY,
        folderEditWidth,
        editHeight,
        TRUE
    );
    MoveWindow(m_chooseFolderButton, buttonX, folderY, chooseWidth, inputHeight, TRUE);

    MoveWindow(m_downloadButton, margin, 238, 120, actionButtonHeight, TRUE);
    MoveWindow(m_clearButton, margin + 130, 238, 170, actionButtonHeight, TRUE);
    MoveWindow(m_settingsButton, buttonX, 238, std::max(90, pasteRight - buttonX), actionButtonHeight, TRUE);
    MoveWindow(m_statusLabel, margin + 316, 246, width - (margin * 2) - 456, 22, TRUE);

    MoveWindow(m_queueLabel, margin, 298, 220, 22, TRUE);
    const int clearFinishedLeft = width - margin - 34;
    MoveWindow(m_logsButton, buttonX, 292, std::max(76, clearFinishedLeft - 8 - buttonX), 30, TRUE);
    MoveWindow(m_clearFinishedButton, width - margin - 34, 292, 34, 30, TRUE);
    MoveWindow(m_queuePlaceholder, margin + 24, 360, width - (margin * 2) - 48, 28, TRUE);
    InvalidateRect(m_window, nullptr, TRUE);
}

void Application::DrawControlFrames(HDC dc) {
    if (m_urlFrameRect.right > m_urlFrameRect.left && m_urlFrameRect.bottom > m_urlFrameRect.top) {
        UiRenderer::DrawInputFrame(dc, m_urlFrameRect);
    }

    if (m_folderFrameRect.right > m_folderFrameRect.left && m_folderFrameRect.bottom > m_folderFrameRect.top) {
        UiRenderer::DrawInputFrame(dc, m_folderFrameRect);
    }
}

void Application::DrawPreviewContent(HDC dc, const RECT& previewRect) {
    VideoPreview preview;
    {
        std::lock_guard lock(m_previewMutex);
        preview = m_preview;
    }

    RECT thumb = {previewRect.left + 14, previewRect.top + 14, previewRect.left + 224, previewRect.top + 132};
    if (!DrawImageCover(dc, thumb, preview.cachedThumbnailPath, 8)) {
        return;
    }
}

void Application::DrawQueueContent(HDC dc, const RECT& queueRect) {
    if (!m_downloadQueue) {
        return;
    }

    const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
    if (tasks.empty()) {
        m_queueScrollOffset = 0;
        return;
    }

    HFONT titleFont = CreateUiFont(-15, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-13, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-12, FW_NORMAL);

    const int maxY = queueRect.bottom - 16;
    const int visibleRows = QueueVisibleRowCount(queueRect);
    const int maxOffset = QueueMaxScrollOffset(queueRect, tasks.size());
    m_queueScrollOffset = std::clamp(m_queueScrollOffset, 0, maxOffset);

    for (int visibleIndex = 0; visibleIndex < visibleRows; ++visibleIndex) {
        const int taskIndex = m_queueScrollOffset + visibleIndex;
        if (taskIndex >= static_cast<int>(tasks.size())) {
            break;
        }
        const DownloadTaskSnapshot& task = tasks[static_cast<size_t>(taskIndex)];
        RECT row = QueueRowRectAt(queueRect, visibleIndex);
        if (row.bottom > maxY) {
            break;
        }

        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        Gdiplus::GraphicsPath rowPath;
        AddRoundedRect(rowPath, row, 8);
        Gdiplus::SolidBrush rowBrush(Gdiplus::Color(255, 35, 35, 38));
        Gdiplus::Pen rowBorder(Gdiplus::Color(255, 46, 46, 50), 1.0f);
        graphics.FillPath(&rowBrush, &rowPath);
        graphics.DrawPath(&rowBorder, &rowPath);

        RECT thumb = {row.left + 10, row.top + 10, row.left + 106, row.bottom - 10};
        if (!DrawImageCover(dc, thumb, task.thumbnailPath, 7)) {
            DrawThumbnailPlaceholder(dc, thumb);
        }

        const int textLeft = thumb.right + 14;
        const int buttonRight = row.right - 12;
        const int buttonWidth = 96;
        const int buttonHeight = 26;
        RECT cancelButton = {buttonRight - buttonWidth, row.top + 12, buttonRight, row.top + 12 + buttonHeight};
        RECT retryButton = {buttonRight - (buttonWidth * 2) - 8, row.top + 12, buttonRight - buttonWidth - 8, row.top + 12 + buttonHeight};
        RECT deleteButton = {buttonRight - buttonWidth, row.top + 12, buttonRight, row.top + 12 + buttonHeight};
        const int textRight = IsRunningTaskState(task.state)
            ? cancelButton.left - 12
            : ((task.state == DownloadTaskState::Canceled || task.state == DownloadTaskState::Failed) ? retryButton.left - 12 : deleteButton.left - 12);

        RECT titleRect = {textLeft, row.top + 9, textRight, row.top + 31};
        const std::wstring title = task.title.empty() ? task.request.url : task.title;
        DrawTextBlock(dc, title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        std::wstring status = TaskStateText(task.state);
        if (!task.statusText.empty() && task.statusText != status) {
            status += L" · " + task.statusText;
        }
        if (!task.errorText.empty()) {
            status += L" · " + task.errorText;
        }
        RECT statusRect = {textLeft, row.top + 34, textRight, row.top + 52};
        DrawTextBlock(dc, status, statusRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        const std::wstring details = BuildTaskDetails(task);
        if (!details.empty()) {
            RECT detailsRect = {textLeft, row.top + 54, textRight, row.top + 72};
            DrawTextBlock(dc, details, detailsRect, kMutedTextColor, smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        RECT progressBack = {textLeft, row.bottom - 13, row.right - 84, row.bottom - 5};
        double percent = task.percent;
        if (task.state == DownloadTaskState::Completed) {
            percent = 100.0;
        }
        percent = std::clamp(percent, 0.0, 100.0);
        UiRenderer::DrawProgressBar(dc, progressBack, percent);

        RECT percentRect = {row.right - 74, row.bottom - 19, row.right - 14, row.bottom - 1};
        DrawTextBlock(
            dc,
            std::to_wstring(static_cast<int>(percent)) + L"%",
            percentRect,
            kMutedTextColor,
            smallFont,
            DT_RIGHT | DT_VCENTER | DT_SINGLELINE
        );

        if (IsRunningTaskState(task.state)) {
            DrawSmallActionButton(
                dc,
                cancelButton,
                L"Отменить",
                false,
                m_hotQueueTaskId == task.id && m_hotQueueAction == kQueueActionCancel
            );
        } else if (task.state == DownloadTaskState::Canceled || task.state == DownloadTaskState::Failed) {
            DrawSmallActionButton(
                dc,
                retryButton,
                L"Возобновить",
                true,
                m_hotQueueTaskId == task.id && m_hotQueueAction == kQueueActionRetry
            );
            DrawSmallActionButton(
                dc,
                deleteButton,
                L"Удалить",
                false,
                m_hotQueueTaskId == task.id && m_hotQueueAction == kQueueActionDelete
            );
        } else if (task.state == DownloadTaskState::Completed) {
            DrawSmallActionButton(
                dc,
                deleteButton,
                L"Закрыть",
                false,
                m_hotQueueTaskId == task.id && m_hotQueueAction == kQueueActionDelete
            );
        }

    }

    if (maxOffset > 0) {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        const RECT track = QueueScrollbarTrackRect(queueRect);
        const RECT thumb = QueueScrollbarThumbRect(queueRect, tasks.size(), m_queueScrollOffset);
        Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 39, 39, 43));
        Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 92, 92, 98));
        graphics.FillRectangle(
            &trackBrush,
            static_cast<INT>(track.left),
            static_cast<INT>(track.top),
            static_cast<INT>(track.right - track.left),
            static_cast<INT>(track.bottom - track.top)
        );
        graphics.FillRectangle(
            &thumbBrush,
            static_cast<INT>(thumb.left),
            static_cast<INT>(thumb.top),
            static_cast<INT>(thumb.right - thumb.left),
            static_cast<INT>(thumb.bottom - thumb.top)
        );
    }

    DeleteObject(titleFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

bool Application::HandleQueueClick(POINT point) {
    if (!m_downloadQueue) {
        return false;
    }

    RECT client = {};
    GetClientRect(m_window, &client);
    const RECT queueRect = QueuePanelRectForClient(client);
    if (!PtInRect(&queueRect, point)) {
        return false;
    }

    const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
    const int maxOffset = QueueMaxScrollOffset(queueRect, tasks.size());
    m_queueScrollOffset = std::clamp(m_queueScrollOffset, 0, maxOffset);
    const RECT scrollbarTrack = QueueScrollbarTrackRect(queueRect);
    if (maxOffset > 0 && PtInRect(&scrollbarTrack, point)) {
        const RECT thumb = QueueScrollbarThumbRect(queueRect, tasks.size(), m_queueScrollOffset);
        if (point.y < thumb.top) {
            m_queueScrollOffset = std::max(0, m_queueScrollOffset - QueueVisibleRowCount(queueRect));
        } else if (point.y > thumb.bottom) {
            m_queueScrollOffset = std::min(maxOffset, m_queueScrollOffset + QueueVisibleRowCount(queueRect));
        }
        InvalidateRect(m_window, &queueRect, FALSE);
        return true;
    }
    const int maxY = queueRect.bottom - 16;
    const int visibleRows = QueueVisibleRowCount(queueRect);
    for (int visibleIndex = 0; visibleIndex < visibleRows; ++visibleIndex) {
        const int taskIndex = m_queueScrollOffset + visibleIndex;
        if (taskIndex >= static_cast<int>(tasks.size())) {
            break;
        }
        const RECT row = QueueRowRectAt(queueRect, visibleIndex);
        if (row.bottom > maxY) {
            break;
        }
        if (!PtInRect(&row, point)) {
            continue;
        }

        const int buttonRight = row.right - 12;
        constexpr int buttonWidth = 96;
        constexpr int buttonHeight = 26;
        RECT cancelButton = {buttonRight - buttonWidth, row.top + 12, buttonRight, row.top + 12 + buttonHeight};
        RECT retryButton = {buttonRight - (buttonWidth * 2) - 8, row.top + 12, buttonRight - buttonWidth - 8, row.top + 12 + buttonHeight};
        RECT deleteButton = {buttonRight - buttonWidth, row.top + 12, buttonRight, row.top + 12 + buttonHeight};

        const DownloadTaskSnapshot& task = tasks[static_cast<size_t>(taskIndex)];
        bool handled = false;
        bool removedRow = false;
        if (IsRunningTaskState(task.state) && PtInRect(&cancelButton, point)) {
            handled = m_downloadQueue->Cancel(task.id);
        } else if ((task.state == DownloadTaskState::Canceled || task.state == DownloadTaskState::Failed) &&
                   PtInRect(&retryButton, point)) {
            handled = m_downloadQueue->Retry(task.id);
        } else if ((task.state == DownloadTaskState::Canceled ||
                    task.state == DownloadTaskState::Failed ||
                    task.state == DownloadTaskState::Completed) &&
                    PtInRect(&deleteButton, point)) {
            handled = m_downloadQueue->DeleteFiles(task.id);
            removedRow = handled;
        }

        if (handled) {
            if (removedRow && m_logger) {
                m_logger->Info(L"Task row removed: id=" + std::to_wstring(task.id));
            }
            RefreshQueueText();
            return true;
        }
        return false;
    }

    return false;
}

bool Application::HandleQueueContextMenu(POINT point) {
    if (!m_downloadQueue) {
        return false;
    }

    RECT client = {};
    GetClientRect(m_window, &client);
    const RECT queueRect = QueuePanelRectForClient(client);
    if (!PtInRect(&queueRect, point)) {
        return false;
    }

    const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
    const int maxOffset = QueueMaxScrollOffset(queueRect, tasks.size());
    m_queueScrollOffset = std::clamp(m_queueScrollOffset, 0, maxOffset);
    const int maxY = queueRect.bottom - 16;
    const int visibleRows = QueueVisibleRowCount(queueRect);
    for (int visibleIndex = 0; visibleIndex < visibleRows; ++visibleIndex) {
        const int taskIndex = m_queueScrollOffset + visibleIndex;
        if (taskIndex >= static_cast<int>(tasks.size())) {
            break;
        }
        const RECT row = QueueRowRectAt(queueRect, visibleIndex);
        if (row.bottom > maxY) {
            break;
        }
        if (!PtInRect(&row, point)) {
            continue;
        }

        const DownloadTaskSnapshot& task = tasks[static_cast<size_t>(taskIndex)];
        if (task.errorText.empty()) {
            return false;
        }

        auto* menuState = new QueueErrorMenuState{};
        menuState->owner = m_window;
        menuState->errorText = task.errorText;

        POINT screenPoint = point;
        ClientToScreen(m_window, &screenPoint);
        constexpr int menuWidth = 204;
        constexpr int menuHeight = 36;
        RECT workArea = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const int x = std::min(static_cast<int>(screenPoint.x), static_cast<int>(workArea.right) - menuWidth);
        const int y = std::min(static_cast<int>(screenPoint.y), static_cast<int>(workArea.bottom) - menuHeight);
        HWND menu = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            kQueueErrorMenuClassName,
            L"",
            WS_POPUP,
            std::max(static_cast<int>(workArea.left), x),
            std::max(static_cast<int>(workArea.top), y),
            menuWidth,
            menuHeight,
            m_window,
            nullptr,
            m_instance,
            menuState
        );
        if (!menu) {
            delete menuState;
            return false;
        }
        ShowWindow(menu, SW_SHOW);
        SetFocus(menu);
        return true;
    }

    return false;
}

bool Application::UpdateQueueHover(POINT point) {
    int hotTaskId = 0;
    int hotAction = 0;
    RECT client = {};
    GetClientRect(m_window, &client);
    const RECT queueRect = QueuePanelRectForClient(client);

    if (PtInRect(&queueRect, point) && !m_queueMouseTracking) {
        TRACKMOUSEEVENT track = {};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = m_window;
        if (TrackMouseEvent(&track)) {
            m_queueMouseTracking = true;
        }
    }

    if (m_downloadQueue && PtInRect(&queueRect, point)) {
        const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
        const int maxOffset = QueueMaxScrollOffset(queueRect, tasks.size());
        m_queueScrollOffset = std::clamp(m_queueScrollOffset, 0, maxOffset);
        const int maxY = queueRect.bottom - 16;
        const int visibleRows = QueueVisibleRowCount(queueRect);

        for (int visibleIndex = 0; visibleIndex < visibleRows; ++visibleIndex) {
            const int taskIndex = m_queueScrollOffset + visibleIndex;
            if (taskIndex >= static_cast<int>(tasks.size())) {
                break;
            }

            const RECT row = QueueRowRectAt(queueRect, visibleIndex);
            if (row.bottom > maxY) {
                break;
            }
            if (!PtInRect(&row, point)) {
                continue;
            }

            const int buttonRight = row.right - 12;
            constexpr int buttonWidth = 96;
            constexpr int buttonHeight = 26;
            RECT cancelButton = {buttonRight - buttonWidth, row.top + 12, buttonRight, row.top + 12 + buttonHeight};
            RECT retryButton = {buttonRight - (buttonWidth * 2) - 8, row.top + 12, buttonRight - buttonWidth - 8, row.top + 12 + buttonHeight};
            RECT deleteButton = {buttonRight - buttonWidth, row.top + 12, buttonRight, row.top + 12 + buttonHeight};

            const DownloadTaskSnapshot& task = tasks[static_cast<size_t>(taskIndex)];
            if (IsRunningTaskState(task.state) && PtInRect(&cancelButton, point)) {
                hotTaskId = task.id;
                hotAction = kQueueActionCancel;
            } else if ((task.state == DownloadTaskState::Canceled || task.state == DownloadTaskState::Failed) &&
                       PtInRect(&retryButton, point)) {
                hotTaskId = task.id;
                hotAction = kQueueActionRetry;
            } else if ((task.state == DownloadTaskState::Canceled ||
                        task.state == DownloadTaskState::Failed ||
                        task.state == DownloadTaskState::Completed) &&
                       PtInRect(&deleteButton, point)) {
                hotTaskId = task.id;
                hotAction = kQueueActionDelete;
            }
            break;
        }
    }

    if (m_hotQueueTaskId == hotTaskId && m_hotQueueAction == hotAction) {
        return hotAction != 0;
    }

    m_hotQueueTaskId = hotTaskId;
    m_hotQueueAction = hotAction;
    InvalidateRect(m_window, &queueRect, FALSE);
    return hotAction != 0;
}

void Application::ClearQueueHover() {
    if (m_hotQueueTaskId == 0 && m_hotQueueAction == 0) {
        return;
    }

    m_hotQueueTaskId = 0;
    m_hotQueueAction = 0;
    RECT client = {};
    GetClientRect(m_window, &client);
    const RECT queueRect = QueuePanelRectForClient(client);
    InvalidateRect(m_window, &queueRect, FALSE);
}

bool Application::ScrollQueue(int wheelDelta, POINT point) {
    if (!m_downloadQueue) {
        return false;
    }

    RECT client = {};
    GetClientRect(m_window, &client);
    const RECT queueRect = QueuePanelRectForClient(client);
    if (!PtInRect(&queueRect, point)) {
        return false;
    }

    const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
    const int maxOffset = QueueMaxScrollOffset(queueRect, tasks.size());
    if (maxOffset <= 0) {
        return false;
    }

    const int previousOffset = m_queueScrollOffset;
    const int direction = wheelDelta > 0 ? -1 : 1;
    m_queueScrollOffset = std::clamp(m_queueScrollOffset + direction, 0, maxOffset);
    if (m_queueScrollOffset != previousOffset) {
        InvalidateRect(m_window, &queueRect, FALSE);
    }
    return true;
}

void Application::SetControlFonts() {
    const std::array<HWND, 13> controls = {
        m_urlEdit,
        m_pasteButton,
        m_previewTitle,
        m_folderEdit,
        m_chooseFolderButton,
        m_downloadButton,
        m_clearButton,
        m_logsButton,
        m_clearFinishedButton,
        m_settingsButton,
        m_statusLabel,
        m_queueLabel,
        m_queuePlaceholder
    };
    for (HWND control : controls) {
        if (control) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        }
    }
    if (m_previewTitle && m_boldFont) {
        SendMessageW(m_previewTitle, WM_SETFONT, reinterpret_cast<WPARAM>(m_boldFont), TRUE);
    }
    if (m_queueLabel && m_boldFont) {
        SendMessageW(m_queueLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_boldFont), TRUE);
    }
}

void Application::SetStatus(const std::wstring& text) {
    if (m_statusLabel) {
        SetWindowTextW(m_statusLabel, text.c_str());
    }
}

void Application::SetTransientStatus(const std::wstring& text) {
    m_transientStatusActive = true;
    SetStatus(text);
    if (m_window) {
        SetTimer(m_window, kStatusRestoreTimer, kStatusRestoreDelayMs, nullptr);
    }
}

void Application::RestoreStatusText() {
    if (m_downloadQueue) {
        const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
        const size_t queuedCount = CountTasksInState(tasks, DownloadTaskState::Queued);
        if (queuedCount > 0) {
            SetStatus(L"В очереди осталось " + std::to_wstring(queuedCount) + L" задач");
            return;
        }
    }
    if (m_ytDlpReady) {
        SetStatus(BuildToolReadyStatus(m_ytDlpStatus, m_ffmpeg));
    } else {
        SetStatus(L"Проверка yt-dlp...");
    }
}

void Application::StartPreviewLoadingText() {
    m_previewLoading = true;
    m_previewLoadingDots = 3;
    {
        std::lock_guard lock(m_previewMutex);
        m_preview = {};
    }
    UpdatePreviewLoadingText();
    if (m_window) {
        SetTimer(m_window, kPreviewLoadingTimer, kPreviewLoadingDelayMs, nullptr);
    }
    InvalidateRect(m_window, nullptr, FALSE);
}

void Application::StopPreviewLoadingText() {
    if (m_window) {
        KillTimer(m_window, kPreviewLoadingTimer);
    }
    m_previewLoading = false;
}

void Application::UpdatePreviewLoadingText() {
    if (!m_previewLoading || !m_previewTitle) {
        return;
    }

    std::wstring text = L"Идёт считывание информации, подождите";
    text.append(static_cast<size_t>(m_previewLoadingDots), L'.');
    SetWindowTextW(m_previewTitle, text.c_str());
    m_previewLoadingDots = m_previewLoadingDots <= 1 ? 3 : m_previewLoadingDots - 1;
}

void Application::InitializeBackend() {
    m_comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    m_paths = std::make_unique<AppPaths>(GetExecutableRoot());
    std::error_code ec;
    std::filesystem::create_directories(m_paths->stuffDir(), ec);
    std::filesystem::create_directories(m_paths->thumbCacheDir(), ec);
    std::filesystem::create_directories(m_paths->toolsDir(), ec);

    m_logger = std::make_unique<Logger>(*m_paths);
    m_logger->Info(L"Application started: root=" + m_paths->root().wstring());
    m_config = ConfigStore::Load(*m_paths);
    if (m_folderEdit && !m_config.downloadDir.empty()) {
        SetWindowTextW(m_folderEdit, m_config.downloadDir.wstring().c_str());
    }

    m_ffmpeg = FfmpegManager::Resolve(*m_paths, m_config);
    m_downloadQueue = std::make_unique<DownloadQueue>(m_config.maxParallelDownloads, m_logger.get());
    LoadDownloadQueue();

    SetTimer(m_window, kQueueRefreshTimer, 500, nullptr);
    SetStatus(L"Проверка yt-dlp...");
    StartToolCheck();
}

void Application::LoadDownloadQueue() {
    if (!m_paths || !m_downloadQueue) {
        return;
    }

    try {
        const std::vector<DownloadTaskSnapshot> tasks = DownloadQueueStore::Load(*m_paths);
        if (tasks.empty()) {
            return;
        }
        m_downloadQueue->ImportSnapshots(tasks);
        m_lastSavedQueueRevision = m_downloadQueue->Revision();
        if (m_logger) {
            m_logger->Info(L"Download queue restored: count=" + std::to_wstring(tasks.size()));
        }
    } catch (const std::exception& ex) {
        if (m_logger) {
            m_logger->Error(
                L"Download queue restore failed: " +
                std::wstring(ex.what(), ex.what() + std::strlen(ex.what()))
            );
        }
    } catch (...) {
        if (m_logger) {
            m_logger->Error(L"Download queue restore failed: unknown error");
        }
    }
}

void Application::SaveDownloadQueue(bool forShutdown) {
    if (!m_paths || !m_downloadQueue) {
        return;
    }

    try {
        const std::vector<DownloadTaskSnapshot> tasks = forShutdown
            ? m_downloadQueue->ExportSnapshotsForShutdown()
            : m_downloadQueue->ExportSnapshots();
        DownloadQueueStore::Save(*m_paths, tasks);
        m_lastSavedQueueRevision = m_downloadQueue->Revision();
        if (forShutdown && m_logger) {
            m_logger->Info(L"Download queue saved for shutdown: count=" + std::to_wstring(tasks.size()));
        }
    } catch (const std::exception& ex) {
        if (m_logger) {
            m_logger->Error(
                L"Download queue save failed: " +
                std::wstring(ex.what(), ex.what() + std::strlen(ex.what()))
            );
        }
    } catch (...) {
        if (m_logger) {
            m_logger->Error(L"Download queue save failed: unknown error");
        }
    }
}

void Application::StartToolCheck() {
    if (!m_paths) {
        return;
    }

    StopAndJoin(m_toolCheckWorker);
    if (m_logger) {
        m_logger->Info(L"yt-dlp tool check started");
    }
    const AppPaths paths = *m_paths;
    HWND window = m_window;
    m_toolCheckWorker = std::jthread([this, paths, window](std::stop_token stopToken) {
        ToolCheckResult result;
        UniqueHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        try {
            if (!cancelEvent.get()) {
                throw std::runtime_error("failed to create tool check cancellation event");
            }
            std::stop_callback stopCallback(stopToken, [handle = cancelEvent.get()]() {
                SetEvent(handle);
            });
            YtDlpManager manager(paths);
            result.status = manager.Status();
            try {
                result.latestRelease = manager.CheckLatestRelease(cancelEvent.get());
                result.latestCheckAt = CurrentUtcTimestamp();
                if (ShouldInstallYtDlpUpdate(result.status, result.latestRelease)) {
                    result.status = manager.InstallOrUpdate(cancelEvent.get());
                }
            } catch (...) {
                if (!result.status.installed) {
                    throw;
                }
            }
            result.ready = result.status.installed;
            result.message = result.ready
                ? L"yt-dlp готов" + (result.status.version.empty() ? L"" : L" (" + result.status.version + L")")
                : L"yt-dlp не найден";
        } catch (const std::exception& ex) {
            result.ready = false;
            result.message = L"Ошибка подготовки yt-dlp: " + std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
        } catch (...) {
            result.ready = false;
            result.message = L"Ошибка подготовки yt-dlp";
        }
        if (stopToken.stop_requested()) {
            return;
        }
        {
            std::lock_guard lock(m_asyncResultMutex);
            m_toolCheckResult = std::move(result);
        }
        PostMessageW(window, kMsgToolCheckComplete, 0, 0);
    });
}

void Application::StartAppUpdateCheck() {
    if (!m_paths || !m_window) {
        return;
    }

    StopAndJoin(m_appUpdateWorker);
    if (m_logger) {
        m_logger->Info(L"Application update check started");
    }
    HWND window = m_window;
    m_appUpdateWorker = std::jthread([this, window](std::stop_token stopToken) {
        AppUpdateCheckResult result;
        UniqueHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        try {
            if (!cancelEvent.get()) {
                throw std::runtime_error("failed to create app update cancellation event");
            }
            std::stop_callback stopCallback(stopToken, [handle = cancelEvent.get()]() {
                SetEvent(handle);
            });
            result.release = AppUpdateService::CheckLatestRelease(cancelEvent.get());
            result.ok = true;
        } catch (const std::exception& ex) {
            result.ok = false;
            result.error = std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
        } catch (...) {
            result.ok = false;
            result.error = L"Неизвестная ошибка проверки обновлений";
        }
        if (stopToken.stop_requested()) {
            return;
        }
        {
            std::lock_guard lock(m_asyncResultMutex);
            m_appUpdateCheckResult = std::move(result);
        }
        PostMessageW(window, kMsgAppUpdateCheckComplete, 0, 0);
    });
}

void Application::StartPreviewFetch() {
    StopAndJoin(m_previewWorker);
    {
        std::lock_guard lock(m_asyncResultMutex);
        m_previewFetchResult.reset();
    }
    if (!m_ytDlpReady || !m_paths) {
        return;
    }

    const std::wstring url = GetWindowTextString(m_urlEdit);
    if (url.size() < 8) {
        ++m_previewRequestId;
        StopPreviewLoadingText();
        {
            std::lock_guard lock(m_previewMutex);
            m_preview = {};
        }
        if (m_previewTitle) {
            SetWindowTextW(m_previewTitle, L"Вставьте ссылку, чтобы получить название и превью");
        }
        InvalidateRect(m_window, nullptr, FALSE);
        return;
    }

    const unsigned long requestId = ++m_previewRequestId;
    StartPreviewLoadingText();
    if (m_logger) {
        m_logger->Info(L"Preview scheduled: url=" + url);
    }
    const YtDlpClientOptions options{m_ytDlpStatus.executable, m_paths->thumbCacheDir(), m_config.cookiesPath};
    HWND window = m_window;
    m_previewWorker = std::jthread([this, requestId, url, options, window](std::stop_token stopToken) {
        if (!WaitForDelay(stopToken, std::chrono::milliseconds(300))) {
            return;
        }

        PreviewFetchResult result;
        result.requestId = requestId;
        result.url = url;
        UniqueHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        try {
            if (!cancelEvent.get()) {
                throw std::runtime_error("failed to create preview cancellation event");
            }
            std::stop_callback stopCallback(stopToken, [handle = cancelEvent.get()]() {
                SetEvent(handle);
            });
            YtDlpClient client(options);
            result.preview = client.FetchPreview(url, cancelEvent.get());
            if (!result.preview.thumbnailUrl.empty()) {
                result.preview.cachedThumbnailPath = client.CacheThumbnail(result.preview, cancelEvent.get());
            }
            result.ok = true;
        } catch (const std::exception& ex) {
            result.ok = false;
            result.error = L"Preview недоступен: " + std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
        } catch (...) {
            result.ok = false;
            result.error = L"Preview недоступен";
        }
        if (stopToken.stop_requested()) {
            return;
        }
        {
            std::lock_guard lock(m_asyncResultMutex);
            m_previewFetchResult = std::move(result);
        }
        PostMessageW(window, kMsgPreviewComplete, 0, 0);
    });
}

void Application::EnqueueCurrentUrl() {
    const DownloadAttemptAction action = ResolveDownloadAttempt(m_ytDlpReady);
    if (action == DownloadAttemptAction::ShowYtDlpNotReady || !m_downloadQueue) {
        ShowErrorDialog(
            m_window,
            m_instance,
            L"yt-dlp ещё не готов",
            L"Дождитесь завершения проверки, установки или обновления yt-dlp."
        );
        return;
    }

    const std::wstring url = GetWindowTextString(m_urlEdit);
    if (url.empty()) {
        ShowErrorDialog(m_window, m_instance, L"Нет ссылки", L"Вставьте ссылку на видео или плейлист.");
        return;
    }

    m_config.downloadDir = GetWindowTextString(m_folderEdit);
    if (m_paths) {
        ConfigStore::Save(*m_paths, m_config);
    }

    VideoPreview preview;
    {
        std::lock_guard lock(m_previewMutex);
        preview = m_preview;
    }

    auto makeRequest = [&](const std::wstring& itemUrl) {
        YtDlpDownloadRequest request;
        request.ytDlpExePath = m_ytDlpStatus.executable;
        request.url = itemUrl;
        request.outputDirectory = m_config.downloadDir;
        request.cookiesPath = m_config.cookiesPath;
        request.ffmpegExePath = m_ffmpeg.ffmpegExe;
        request.ffmpegAvailable = m_ffmpeg.available;
        request.quality = m_config.quality;
        request.container = m_config.container;
        return request;
    };

    if (preview.isPlaylist && !preview.entries.empty()) {
        size_t remaining = preview.entries.size();
        for (const VideoPreview& entry : preview.entries) {
            const std::wstring itemUrl = entry.webpageUrl.empty() ? url : entry.webpageUrl;
            m_downloadQueue->Enqueue(makeRequest(itemUrl), entry.title.empty() ? itemUrl : entry.title, entry.cachedThumbnailPath);
            --remaining;
            if (remaining > 0) {
                SetTransientStatus(L"Добавление элементов плейлиста: осталось " + std::to_wstring(remaining));
            }
        }
        KillTimer(m_window, kStatusRestoreTimer);
        m_transientStatusActive = false;
    } else {
        m_downloadQueue->Enqueue(makeRequest(url), preview.title.empty() ? url : preview.title, preview.cachedThumbnailPath);
    }
    RefreshQueueText();
}

void Application::RefreshQueueText() {
    if (!m_downloadQueue || !m_queuePlaceholder) {
        return;
    }
    const std::uint64_t revision = m_downloadQueue->Revision();
    if (revision == m_lastRenderedQueueRevision) {
        return;
    }
    if (revision != m_lastSavedQueueRevision) {
        SaveDownloadQueue(false);
    }
    m_lastRenderedQueueRevision = revision;
    RECT client = {};
    GetClientRect(m_window, &client);
    RECT queueRect = QueuePanelRectForClient(client);

    const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
    const size_t queuedCount = CountTasksInState(tasks, DownloadTaskState::Queued);
    if (tasks.empty()) {
        if (!m_queuePlaceholderVisible) {
            ShowWindow(m_queuePlaceholder, SW_SHOW);
            m_queuePlaceholderVisible = true;
        }
        InvalidateRect(m_window, &queueRect, FALSE);
        return;
    }

    if (m_queuePlaceholderVisible) {
        ShowWindow(m_queuePlaceholder, SW_HIDE);
        m_queuePlaceholderVisible = false;
    }
    if (queuedCount > 0 && !m_transientStatusActive) {
        SetStatus(L"В очереди осталось " + std::to_wstring(queuedCount) + L" задач");
    } else if (queuedCount == 0 && !m_transientStatusActive) {
        RestoreStatusText();
    }
    InvalidateRect(m_window, &queueRect, FALSE);
}

std::wstring Application::GetWindowTextString(HWND control) const {
    if (!control) {
        return {};
    }
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

#include "Application.h"

#include "DialogWindows.h"
#include "resource.h"
#include "UiRenderer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <thread>

#include "ProcessRunner.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

constexpr const wchar_t* kWindowClassName = L"YoutubeDownloaderWin32Class";
constexpr const wchar_t* kButtonClassName = L"YoutubeDownloaderButtonClass";
constexpr int kInitialWindowWidth = 1000;
constexpr int kInitialWindowHeight = 640;
constexpr COLORREF kBackgroundColor = RGB(20, 20, 22);
constexpr COLORREF kPanelColor = RGB(28, 28, 31);
constexpr COLORREF kInputColor = RGB(25, 25, 28);
constexpr COLORREF kTextColor = RGB(242, 242, 242);
constexpr UINT kMsgToolCheckComplete = WM_APP + 1;
constexpr UINT kMsgPreviewComplete = WM_APP + 2;
constexpr UINT_PTR kQueueRefreshTimer = 1;

enum ControlId {
    IdUrlEdit = 1001,
    IdPasteButton = 1002,
    IdChooseFolderButton = 1003,
    IdDownloadButton = 1004,
    IdClearButton = 1005,
    IdSettingsButton = 1006
};

struct ButtonState {
    int commandId = 0;
    bool primary = false;
    bool onPanel = false;
    bool hot = false;
    bool pressed = false;
    std::wstring text;
};

struct ToolCheckResult {
    bool ready = false;
    ToolInstallStatus status;
    std::wstring message;
};

struct PreviewFetchResult {
    unsigned long requestId = 0;
    bool ok = false;
    VideoPreview preview;
    std::wstring error;
};

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
    case DownloadTaskState::PostProcessing:
        return L"Обработка";
    case DownloadTaskState::Completed:
        return L"Готово";
    case DownloadTaskState::Failed:
        return L"Ошибка";
    case DownloadTaskState::Canceled:
        return L"Отменено";
    }
    return L"";
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
    return true;
}

int Application::Run() {
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void Application::Shutdown() {
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
    if (m_backgroundBrush) {
        DeleteObject(m_backgroundBrush);
        m_backgroundBrush = nullptr;
    }
    if (m_gdiplusToken != 0) {
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        m_gdiplusToken = 0;
    }
    if (m_downloadQueue) {
        m_downloadQueue->Shutdown();
    }
    CoUninitialize();
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

    case WM_PAINT:
        {
            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(m_window, &paint);
            RECT client = {};
            GetClientRect(m_window, &client);
            UiRenderer::DrawBackground(dc, client);

            RECT preview = {20, 72, client.right - 20, 218};
            UiRenderer::DrawPreviewCard(dc, preview);
            RECT queue = {20, 334, client.right - 20, client.bottom - 20};
            UiRenderer::DrawPanel(dc, queue);
            DrawControlFrames(dc);
            EndPaint(m_window, &paint);
        }
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
            SendMessageW(m_urlEdit, WM_PASTE, 0, 0);
            StartPreviewFetch();
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
                m_downloadQueue->ClearFinished();
                RefreshQueueText();
            }
            return 0;
        }
        if (LOWORD(wParam) == IdSettingsButton) {
            if (ShowSettingsDialog(m_window, m_instance, m_config) && m_paths) {
                ConfigStore::Save(*m_paths, m_config);
                m_ffmpeg = FfmpegManager::Resolve(*m_paths, m_config);
                SetStatus(L"Настройки сохранены");
            }
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == kQueueRefreshTimer) {
            RefreshQueueText();
            return 0;
        }
        break;

    case kMsgToolCheckComplete:
        {
            std::unique_ptr<ToolCheckResult> result(reinterpret_cast<ToolCheckResult*>(lParam));
            m_ytDlpReady = result->ready;
            m_ytDlpStatus = result->status;
            if (m_ytDlpReady && m_paths) {
                YtDlpClientOptions options;
                options.ytDlpExePath = m_ytDlpStatus.executable;
                options.thumbCacheDir = m_paths->thumbCacheDir();
                options.cookiesPath = m_config.cookiesPath;
                m_ytDlpClient = std::make_unique<YtDlpClient>(std::move(options));
                EnableWindow(m_downloadButton, TRUE);
            } else {
                EnableWindow(m_downloadButton, FALSE);
            }
            SetStatus(result->message);
        }
        return 0;

    case kMsgPreviewComplete:
        {
            std::unique_ptr<PreviewFetchResult> result(reinterpret_cast<PreviewFetchResult*>(lParam));
            if (result->requestId != m_previewRequestId.load()) {
                return 0;
            }
            if (result->ok) {
                {
                    std::lock_guard lock(m_previewMutex);
                    m_preview = result->preview;
                }
                std::wstring title = result->preview.title.empty() ? L"Видео найдено" : result->preview.title;
                if (result->preview.isPlaylist) {
                    title += L" — плейлист: " + std::to_wstring(result->preview.entries.size()) + L" видео";
                }
                SetWindowTextW(m_previewTitle, title.c_str());
            } else {
                SetWindowTextW(m_previewTitle, result->error.empty() ? L"Не удалось получить preview" : result->error.c_str());
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(m_window, kQueueRefreshTimer);
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
    m_settingsButton = CreateButton(L"Настройки", IdSettingsButton, false, false);
    m_statusLabel = CreateChild(m_window, L"STATIC", L"Подготовка интерфейса", SS_LEFT, 0, 0);
    m_queueLabel = CreateChild(m_window, L"STATIC", L"Очередь загрузок", SS_LEFT, 0, 0);
    m_queuePlaceholder = CreateChild(m_window, L"STATIC", L"Задач пока нет", SS_CENTER, 0, 0);
    EnableWindow(m_downloadButton, FALSE);

    SetControlFonts();

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

void Application::SetControlFonts() {
    const std::array<HWND, 11> controls = {
        m_urlEdit,
        m_pasteButton,
        m_previewTitle,
        m_folderEdit,
        m_chooseFolderButton,
        m_downloadButton,
        m_clearButton,
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

void Application::InitializeBackend() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    m_paths = std::make_unique<AppPaths>(GetExecutableRoot());
    std::error_code ec;
    std::filesystem::create_directories(m_paths->stuffDir(), ec);
    std::filesystem::create_directories(m_paths->thumbCacheDir(), ec);
    std::filesystem::create_directories(m_paths->toolsDir(), ec);

    m_logger = std::make_unique<Logger>(*m_paths);
    m_config = ConfigStore::Load(*m_paths);
    if (m_folderEdit && !m_config.downloadDir.empty()) {
        SetWindowTextW(m_folderEdit, m_config.downloadDir.wstring().c_str());
    }

    m_ffmpeg = FfmpegManager::Resolve(*m_paths, m_config);
    m_downloadQueue = std::make_unique<DownloadQueue>(m_config.maxParallelDownloads);
    m_downloadQueue->SetExecutor([this](const DownloadTaskSnapshot& task, const DownloadTaskCallbacks& callbacks) {
        ProcessRunOptions options;
        options.executable = task.request.ytDlpExePath;
        options.arguments = BuildDownloadArguments(task.request);
        options.timeoutMs = INFINITE;

        HANDLE cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::atomic<bool> monitorDone = false;
        std::thread cancelMonitor([&]() {
            while (!monitorDone.load()) {
                if (callbacks.isCanceled && callbacks.isCanceled()) {
                    SetEvent(cancelEvent);
                    break;
                }
                Sleep(50);
            }
        });
        options.cancelEvent = cancelEvent;
        options.onStdoutLine = [callbacks](const std::wstring& line) {
            if (callbacks.onOutputLine) {
                callbacks.onOutputLine(line);
            }
            const YtDlpProgress progress = ParseYtDlpProgressLine(line);
            if (progress.recognized && callbacks.onProgress) {
                callbacks.onProgress(progress.percent, progress.stage);
            }
        };
        options.onStderrLine = callbacks.onOutputLine;

        ProcessRunResult result;
        try {
            result = ProcessRunner::Run(options);
        } catch (const std::exception& ex) {
            monitorDone = true;
            SetEvent(cancelEvent);
            if (cancelMonitor.joinable()) {
                cancelMonitor.join();
            }
            CloseHandle(cancelEvent);
            return DownloadTaskResult{false, std::wstring(ex.what(), ex.what() + std::strlen(ex.what())), {}};
        }

        monitorDone = true;
        SetEvent(cancelEvent);
        if (cancelMonitor.joinable()) {
            cancelMonitor.join();
        }
        CloseHandle(cancelEvent);

        if (result.canceled) {
            return DownloadTaskResult{false, L"Отменено", {}};
        }
        if (result.exitCode != 0) {
            return DownloadTaskResult{false, result.stderrText.empty() ? result.stdoutText : result.stderrText, {}};
        }
        return DownloadTaskResult{true, L"", {}};
    });

    SetTimer(m_window, kQueueRefreshTimer, 500, nullptr);
    SetStatus(L"Проверка yt-dlp...");
    StartToolCheck();
}

void Application::StartToolCheck() {
    if (!m_paths) {
        return;
    }

    const AppPaths paths = *m_paths;
    HWND window = m_window;
    std::thread([paths, window]() {
        auto* result = new ToolCheckResult{};
        try {
            YtDlpManager manager(paths);
            result->status = manager.Status();
            if (!result->status.installed) {
                result->message = L"Установка yt-dlp...";
                result->status = manager.InstallOrUpdate();
            }
            result->ready = result->status.installed;
            result->message = result->ready
                ? L"yt-dlp готов" + (result->status.version.empty() ? L"" : L" (" + result->status.version + L")")
                : L"yt-dlp не найден";
        } catch (const std::exception& ex) {
            result->ready = false;
            result->message = L"Ошибка подготовки yt-dlp: " + std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
        } catch (...) {
            result->ready = false;
            result->message = L"Ошибка подготовки yt-dlp";
        }
        PostMessageW(window, kMsgToolCheckComplete, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void Application::StartPreviewFetch() {
    if (!m_ytDlpReady || !m_ytDlpClient || !m_paths) {
        return;
    }

    const std::wstring url = GetWindowTextString(m_urlEdit);
    if (url.size() < 8) {
        return;
    }

    const unsigned long requestId = ++m_previewRequestId;
    const YtDlpClientOptions options{m_ytDlpStatus.executable, m_paths->thumbCacheDir(), m_config.cookiesPath};
    HWND window = m_window;
    std::thread([requestId, url, options, window]() {
        auto* result = new PreviewFetchResult{};
        result->requestId = requestId;
        try {
            YtDlpClient client(options);
            result->preview = client.FetchPreview(url);
            if (!result->preview.thumbnailUrl.empty()) {
                result->preview.cachedThumbnailPath = client.CacheThumbnail(result->preview);
            }
            result->ok = true;
        } catch (const std::exception& ex) {
            result->ok = false;
            result->error = L"Preview недоступен: " + std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
        } catch (...) {
            result->ok = false;
            result->error = L"Preview недоступен";
        }
        PostMessageW(window, kMsgPreviewComplete, 0, reinterpret_cast<LPARAM>(result));
    }).detach();
}

void Application::EnqueueCurrentUrl() {
    if (!m_ytDlpReady || !m_downloadQueue) {
        ShowErrorDialog(m_window, m_instance, L"yt-dlp ещё не готов", L"Дождитесь завершения проверки или установки yt-dlp.");
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
        for (const VideoPreview& entry : preview.entries) {
            const std::wstring itemUrl = entry.webpageUrl.empty() ? url : entry.webpageUrl;
            m_downloadQueue->Enqueue(makeRequest(itemUrl), entry.title.empty() ? itemUrl : entry.title);
        }
    } else {
        m_downloadQueue->Enqueue(makeRequest(url), preview.title.empty() ? url : preview.title);
    }
    RefreshQueueText();
}

void Application::RefreshQueueText() {
    if (!m_downloadQueue || !m_queuePlaceholder) {
        return;
    }
    const std::vector<DownloadTaskSnapshot> tasks = m_downloadQueue->Snapshot();
    if (tasks.empty()) {
        SetWindowTextW(m_queuePlaceholder, L"Задач пока нет");
        return;
    }

    std::wostringstream out;
    for (const DownloadTaskSnapshot& task : tasks) {
        out << L"#" << task.id << L"  " << TaskStateText(task.state) << L"  ";
        if (task.percent > 0.0) {
            out << static_cast<int>(task.percent) << L"%  ";
        }
        out << (task.title.empty() ? task.request.url : task.title);
        if (!task.errorText.empty()) {
            out << L" — " << task.errorText;
        }
        out << L"\r\n";
    }
    SetWindowTextW(m_queuePlaceholder, out.str().c_str());
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

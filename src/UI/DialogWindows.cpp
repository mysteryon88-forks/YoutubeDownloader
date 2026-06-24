#include "DialogWindows.h"

#include "AppVersion.h"
#include "BackendText.h"
#include "ToolManagers.h"
#include "UiActions.h"
#include "UiRenderer.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

bool ShowFfmpegInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowWhisperInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config, WhisperBackend backend);
bool ShowWhisperModelDownloadProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config, const WhisperModelInfo& model);
bool ShowWhisperDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowVotCliDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowVotCliInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowWhisperModelsDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowAppUpdateProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release);

namespace {

constexpr const wchar_t* kDialogClassName = L"YoutubeDownloaderDialogWindow";
constexpr const wchar_t* kDialogButtonClassName = L"YoutubeDownloaderDialogButton";
constexpr const wchar_t* kScrollTextClassName = L"YoutubeDownloaderScrollText";
constexpr const wchar_t* kLogViewClassName = L"YoutubeDownloaderLogView";
constexpr const wchar_t* kLogCopyMenuClassName = L"YoutubeDownloaderLogCopyMenu";

constexpr COLORREF kBackgroundColor = RGB(20, 20, 22);
constexpr COLORREF kPanelColor = RGB(28, 28, 31);
constexpr COLORREF kInputColor = RGB(25, 25, 28);
constexpr COLORREF kTextColor = RGB(242, 242, 242);
constexpr COLORREF kMutedTextColor = RGB(172, 172, 178);
constexpr int kDialogPanelInset = 12;
constexpr int kDialogButtonInset = 16;
constexpr int kDialogButtonGap = 12;
constexpr int kDialogButtonHeight = 34;
constexpr int kScrollTextTopPadding = 12;
constexpr int kScrollTextBottomPadding = 12;
constexpr int kScrollTextClipTopPadding = 8;
constexpr int kScrollTextClipBottomPadding = 8;
constexpr int kSettingsDialogWidth = 820;
constexpr int kSettingsDialogHeight = 800;
constexpr int kSettingsTabWidth = 136;
constexpr int kSettingsTabGap = 12;
constexpr int kSettingsParallelControlWidth = 40;
constexpr int kSettingsParallelValueWidth = 52;
constexpr int kSettingsCancelButtonWidth = 132;
constexpr int kSettingsSaveButtonWidth = 148;
constexpr UINT kProgressUpdateMessage = WM_APP + 40;
constexpr UINT kProgressDoneMessage = WM_APP + 41;

enum class DialogType {
    Info,
    Error,
    Confirmation,
    Settings,
    About,
    Logs,
    Ffmpeg,
    Whisper,
    VotCli,
    WhisperModels,
    Progress
};

enum class ProgressMode {
    FfmpegInstall,
    WhisperInstall,
    WhisperModelDownload,
    VotCliInstall,
    AppUpdate
};

enum class SettingsTab {
    General,
    Utilities
};

enum DialogCommand {
    IdOk = 1,
    IdCancel = 2,
    IdCopy = 10,
    IdAbout = 11,
    IdFfmpeg = 12,
    IdInstall = 13,
    IdSkip = 15,
    IdCheckUpdates = 16,
    IdChooseFolder = 17,
    IdAutoUpdate = 120,
    IdParallelMinus = 121,
    IdParallelPlus = 122,
    IdTranscribeAfterDownload = 123,
    IdWhisper = 124,
    IdWhisperModels = 125,
    IdInstallWhisperCpu = 126,
    IdInstallWhisperCuda = 127,
    IdSettingsTab = 128,
    IdUtilitiesTab = 129,
    IdVoiceOverSeparate = 130,
    IdVoiceOverMixed = 131,
    IdVoiceOverLangRu = 132,
    IdVoiceOverLangEn = 133,
    IdVoiceOverLangKk = 134,
    IdVotCli = 135,
    IdWhisperModelBase = 2000
};

struct ButtonState {
    int commandId = 0;
    bool primary = false;
    bool hot = false;
    bool pressed = false;
    std::wstring text;
};

struct ScrollTextState {
    std::wstring text;
    int scrollY = 0;
    int contentHeight = 0;
    bool draggingThumb = false;
    int dragStartY = 0;
    int dragStartScrollY = 0;
};

struct LogLineLayout {
    RECT rect = {};
};

struct LogViewState {
    std::wstring text;
    std::vector<std::wstring> lines;
    std::vector<LogLineLayout> layouts;
    int scrollY = 0;
    int contentHeight = 0;
    int selectedLine = -1;
    bool draggingThumb = false;
    int dragStartY = 0;
    int dragStartScrollY = 0;
};

struct LogCopyMenuState {
    HWND owner = nullptr;
    std::wstring text;
    bool hot = false;
};

struct ProgressUpdate {
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
    std::wstring status;
};

struct DialogState {
    DialogType type = DialogType::Info;
    HINSTANCE instance = nullptr;
    HWND owner = nullptr;
    HWND window = nullptr;
    HWND scrollText = nullptr;
    HWND logView = nullptr;
    std::wstring title;
    std::wstring message;
    const AppPaths* paths = nullptr;
    AppConfig* config = nullptr;
    AppConfig workingConfig;
    SettingsTab settingsTab = SettingsTab::General;
    bool* savedResult = nullptr;
    std::uint64_t progressDownloaded = 0;
    std::uint64_t progressTotal = 0;
    HWND tooltip = nullptr;
    std::vector<HWND> tooltips;
    HANDLE cancelEvent = nullptr;
    bool progressDone = false;
    bool progressSuccess = false;
    ULONGLONG lastProgressPaintTick = 0;
    int lastProgressPaintPercent = -1;
    std::wstring lastProgressPaintStatus;
    ProgressMode progressMode = ProgressMode::FfmpegInstall;
    ReleaseAssetInfo release;
    WhisperModelInfo whisperModel;
    WhisperBackend whisperBackend = WhisperBackend::Cpu;
    std::jthread worker;
    std::mutex progressMutex;
    std::optional<ProgressUpdate> pendingProgress;
    std::optional<std::wstring> progressError;
};

int SettingsContentLeft() {
    return kDialogPanelInset + kDialogButtonInset;
}

int SettingsContentRight(int width) {
    return width - kDialogPanelInset - kDialogButtonInset;
}

RECT SettingsParallelValueRect(int width) {
    const int valueRight = SettingsContentRight(width) - kSettingsParallelControlWidth - kDialogButtonGap;
    return {valueRight - kSettingsParallelValueWidth, 300, valueRight, 334};
}

void EnableDarkTitleBar(HWND window) {
    BOOL enabled = TRUE;
    constexpr DWORD kDwmUseImmersiveDarkMode = 20;
    if (FAILED(DwmSetWindowAttribute(window, kDwmUseImmersiveDarkMode, &enabled, sizeof(enabled)))) {
        constexpr DWORD kDwmUseImmersiveDarkModeBefore20H1 = 19;
        DwmSetWindowAttribute(window, kDwmUseImmersiveDarkModeBefore20H1, &enabled, sizeof(enabled));
    }
}

HFONT CreateUiFont(int height = -16, int weight = FW_NORMAL) {
    LOGFONTW font = {};
    font.lfHeight = height;
    font.lfWeight = weight;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&font);
}

void DrawTextBlock(HDC dc, const std::wstring& text, RECT rect, COLORREF color, HFONT font, UINT format) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const RECT& rect, int radius) {
    const int diameter = radius * 2;
    path.AddArc(rect.left, rect.top, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.top, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.right - diameter, rect.bottom - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.left, rect.bottom - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
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
    HBRUSH background = CreateSolidBrush(kPanelColor);
    FillRect(bufferDc, &client, background);
    DeleteObject(background);

    paintContent(bufferDc, client);
    BitBlt(screenDc, 0, 0, width, height, bufferDc, 0, 0, SRCCOPY);

    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(bufferDc);
    EndPaint(window, &paint);
}

std::optional<std::filesystem::path> PickFfmpegFolder(HWND owner) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    }
    dialog->SetTitle(L"Выберите папку FFmpeg или папку bin");

    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = std::filesystem::path(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

UINT TooltipInfoSize();
bool IsTooltipRelayMessage(UINT message);
void RelayDialogTooltipMessage(const DialogState* state, const MSG& message);
void SetDarkButtonState(HWND parent, int id, bool primary, const std::wstring& text);

HWND CreateTooltip(HWND parent, HWND tool, const wchar_t* text) {
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

    SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
    SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 10000);
    SendMessageW(tooltip, TTM_SETTIPBKCOLOR, RGB(35, 35, 38), 0);
    SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, kTextColor, 0);
    SendMessageW(tooltip, TTM_ACTIVATE, TRUE, 0);
    SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    TOOLINFOW info = {};
    info.cbSize = TooltipInfoSize();
    info.uFlags = TTF_IDISHWND | TTF_TRANSPARENT;
    info.hwnd = parent;
    info.uId = reinterpret_cast<UINT_PTR>(tool);
    info.lpszText = const_cast<LPWSTR>(text);
    SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    return tooltip;
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

void RelayDialogTooltipMessage(const DialogState* state, const MSG& message) {
    if (!state || !IsTooltipRelayMessage(message.message)) {
        return;
    }

    MSG relayMessage = message;
    if (state->tooltip) {
        SendMessageW(state->tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
    }
    for (HWND tooltip : state->tooltips) {
        if (tooltip) {
            relayMessage = message;
            SendMessageW(tooltip, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&relayMessage));
        }
    }
}

void AddDialogTooltip(DialogState* state, HWND tool, const wchar_t* text) {
    if (!state || !tool || !text) {
        return;
    }
    HWND tooltip = CreateTooltip(state->window, tool, text);
    if (tooltip) {
        state->tooltips.push_back(tooltip);
    }
}

bool ApplySelectedFfmpegPath(HWND owner, HINSTANCE instance, AppConfig& config, const std::filesystem::path& path) {
    const FfmpegStatus status = FfmpegManager::ResolveUserPath(path);
    if (!status.available) {
        ShowErrorDialog(owner, instance, L"FFmpeg не найден", status.message);
        return false;
    }
    config.ffmpegPath = status.ffmpegExe;
    return true;
}

FfmpegStatus ResolveDialogFfmpegStatus(const DialogState* state) {
    if (!state || !state->config) {
        return {};
    }
    if (state->paths) {
        return FfmpegManager::Resolve(*state->paths, *state->config);
    }
    if (!state->config->ffmpegPath.empty()) {
        return FfmpegManager::ResolveUserPath(state->config->ffmpegPath);
    }
    return {};
}

ToolInstallStatus ResolveDialogWhisperStatus(const DialogState* state) {
    if (!state || !state->paths || !state->config) {
        return {};
    }
    return WhisperManager::Resolve(*state->paths, *state->config);
}

void RefreshWhisperDialogText(DialogState* state);

ToolInstallStatus ResolveDialogWhisperBackendStatus(const DialogState* state, WhisperBackend backend) {
    if (!state || !state->paths) {
        return {};
    }

    ToolInstallStatus status = WhisperManager::ResolveBackend(*state->paths, backend);
    if (!status.installed && backend == WhisperBackend::Cpu) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(state->paths->localWhisperExePath(), ec)) {
            status.installed = true;
            status.executable = state->paths->localWhisperExePath();
            status.whisperBackend = WhisperBackend::Cpu;
        }
    }
    return status;
}

bool IsWhisperBackendSelected(const DialogState* state, WhisperBackend backend) {
    const ToolInstallStatus active = ResolveDialogWhisperStatus(state);
    const ToolInstallStatus backendStatus = ResolveDialogWhisperBackendStatus(state, backend);
    return active.installed &&
        backendStatus.installed &&
        active.whisperBackend == backend &&
        active.executable == backendStatus.executable;
}

std::wstring WhisperBackendButtonText(const DialogState* state, WhisperBackend backend) {
    const std::wstring name = WhisperManager::BackendDisplayName(backend);
    if (IsWhisperBackendSelected(state, backend)) {
        return name + L" выбран";
    }

    const ToolInstallStatus status = ResolveDialogWhisperBackendStatus(state, backend);
    return status.installed ? L"Выбрать " + name : L"Скачать " + name;
}

void RefreshWhisperBackendButtons(DialogState* state) {
    if (!state || !state->window) {
        return;
    }

    SetDarkButtonState(
        state->window,
        IdInstallWhisperCpu,
        IsWhisperBackendSelected(state, WhisperBackend::Cpu),
        WhisperBackendButtonText(state, WhisperBackend::Cpu)
    );
    SetDarkButtonState(
        state->window,
        IdInstallWhisperCuda,
        IsWhisperBackendSelected(state, WhisperBackend::Cuda),
        WhisperBackendButtonText(state, WhisperBackend::Cuda)
    );
}

void SelectWhisperBackend(DialogState* state, WhisperBackend backend) {
    if (!state || !state->config) {
        return;
    }

    state->config->whisperPath.clear();
    state->config->whisperBackend = backend;
    state->workingConfig = *state->config;
    if (state->savedResult) {
        *state->savedResult = true;
    }
    RefreshWhisperDialogText(state);
    RefreshWhisperBackendButtons(state);
}

std::filesystem::path ResolveDialogWhisperModelPath(const DialogState* state) {
    if (!state || !state->config) {
        return {};
    }
    if (!state->workingConfig.whisperModelPath.empty()) {
        return state->workingConfig.whisperModelPath;
    }
    return state->paths ? state->paths->localWhisperModelPath() : std::filesystem::path{};
}

bool IsWhisperModelDownloaded(const DialogState* state, const WhisperModelInfo& model) {
    if (!state || !state->paths) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(WhisperManager::ModelPath(*state->paths, model), ec);
}

bool IsWhisperModelSelected(const DialogState* state, const WhisperModelInfo& model) {
    if (!state || !state->paths || !state->config) {
        return false;
    }
    return ResolveDialogWhisperModelPath(state) == WhisperManager::ModelPath(*state->paths, model);
}

std::wstring WhisperModelButtonText(const DialogState* state, const WhisperModelInfo& model) {
    if (IsWhisperModelSelected(state, model)) {
        return L"Выбрана";
    }
    return IsWhisperModelDownloaded(state, model) ? L"Скачана" : L"Скачать";
}

void RefreshWhisperModelButtons(DialogState* state) {
    if (!state || !state->window) {
        return;
    }
    const std::vector<WhisperModelInfo> models = WhisperManager::ModelCatalog();
    for (size_t i = 0; i < models.size(); ++i) {
        const int id = IdWhisperModelBase + static_cast<int>(i);
        SetDarkButtonState(state->window, id, IsWhisperModelSelected(state, models[i]), WhisperModelButtonText(state, models[i]));
    }
}

std::wstring FfmpegDialogTitle(const FfmpegStatus& status) {
    return status.available ? L"FFmpeg указан" : L"FFmpeg не найден";
}

std::wstring FfmpegDialogMessage(const FfmpegStatus& status) {
    if (status.available) {
        return L"FFmpeg найден и будет использоваться для объединения видео/аудио дорожек и переконвертации.\n\nПуть:\n" +
            status.ffmpegExe.wstring();
    }
    return L"FFmpeg не найден. Без него приложение сможет скачивать только готовые единые файлы без переконвертации и объединения отдельных видео/аудио дорожек.";
}

std::wstring WhisperDialogTitle(const ToolInstallStatus& status) {
    return status.installed ? L"Whisper.cpp установлен" : L"Whisper.cpp не найден";
}

std::wstring WhisperDialogMessage(const ToolInstallStatus& status) {
    if (status.installed) {
        return L"whisper.cpp найден и будет использоваться для расшифровки аудиодорожек после скачивания.\n\nBackend: " +
            WhisperManager::BackendDisplayName(status.whisperBackend) +
            L"\n\nПуть:\n" +
            status.executable.wstring();
    }
    return L"whisper.cpp не найден. Установите локальную сборку, чтобы приложение могло распознавать аудиодорожку и сохранять TXT/SRT расшифровку.";
}

std::wstring VotCliDialogTitle(const VotCliStatus& status) {
    return status.available ? L"vot-cli установлен" : L"vot-cli не найден";
}

std::wstring VotCliDialogMessage(const VotCliStatus& status) {
    if (status.available) {
        return L"vot-cli найден и будет использоваться для получения озвучки перевода.\n\nПуть:\n" +
            status.executable.wstring();
    }
    if (!status.nodeAvailable) {
        return L"vot-cli не найден, и node.exe тоже не найден. Установите Node.js 18+ или проверьте PATH, затем установите vot-cli через npm.";
    }
    return L"vot-cli не найден. Нажмите «Установить», чтобы выполнить команду:\n\nnpm install -g vot-cli\n\nПосле установки приложение проверит, появился ли vot-cli в PATH.";
}

void RefreshWhisperDialogText(DialogState* state) {
    if (!state) {
        return;
    }
    const ToolInstallStatus status = ResolveDialogWhisperStatus(state);
    state->title = WhisperDialogTitle(status);
    state->message = WhisperDialogMessage(status);
}

void RefreshVotCliDialogText(DialogState* state) {
    if (!state || !state->config) {
        return;
    }
    const VotCliStatus status = VotCliManager::Resolve(state->workingConfig);
    state->title = VotCliDialogTitle(status);
    state->message = VotCliDialogMessage(status);
}

void RegisterDialogClasses(HINSTANCE instance);
LRESULT CALLBACK DialogWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK DialogButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LogViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LogCopyMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
HWND CreateLogView(HWND parent, HINSTANCE instance, const std::wstring& text);
LRESULT CALLBACK ScrollTextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

HWND CreateDarkButton(HWND parent, HINSTANCE instance, const wchar_t* text, int id, bool primary) {
    auto* state = new ButtonState{};
    state->commandId = id;
    state->primary = primary;
    state->text = text;

    HWND button = CreateWindowExW(
        0,
        kDialogButtonClassName,
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance,
        state
    );
    if (!button) {
        delete state;
    }
    return button;
}

ButtonState* GetButtonState(HWND button) {
    return reinterpret_cast<ButtonState*>(GetWindowLongPtrW(button, GWLP_USERDATA));
}

void SetDarkButtonState(HWND parent, int id, bool primary, const std::wstring& text = L"") {
    HWND button = GetDlgItem(parent, id);
    if (!button) {
        return;
    }
    ButtonState* state = GetButtonState(button);
    if (!state) {
        return;
    }
    state->primary = primary;
    if (!text.empty()) {
        state->text = text;
    }
    InvalidateRect(button, nullptr, TRUE);
}

template <size_t Count>
void SetControlsVisible(HWND parent, const std::array<int, Count>& ids, bool visible) {
    for (int id : ids) {
        HWND control = GetDlgItem(parent, id);
        if (!control) {
            continue;
        }
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
        EnableWindow(control, visible ? TRUE : FALSE);
    }
}

void RefreshSettingsButtons(DialogState* state) {
    if (!state || !state->window) {
        return;
    }

    SetDarkButtonState(state->window, IdSettingsTab, state->settingsTab == SettingsTab::General);
    SetDarkButtonState(state->window, IdUtilitiesTab, state->settingsTab == SettingsTab::Utilities);

    SetDarkButtonState(state->window, 101, state->workingConfig.quality == L"audio");
    SetDarkButtonState(state->window, 102, state->workingConfig.quality == L"360p");
    SetDarkButtonState(state->window, 103, state->workingConfig.quality == L"480p");
    SetDarkButtonState(state->window, 104, state->workingConfig.quality == L"720p");
    SetDarkButtonState(state->window, 105, state->workingConfig.quality == L"1080p");
    SetDarkButtonState(state->window, 106, state->workingConfig.quality == L"max");

    SetDarkButtonState(state->window, 111, state->workingConfig.container == L"auto");
    SetDarkButtonState(state->window, 112, state->workingConfig.container == L"mp4");
    SetDarkButtonState(state->window, 113, state->workingConfig.container == L"mkv");
    SetDarkButtonState(state->window, 114, state->workingConfig.container == L"webm");

    SetDarkButtonState(
        state->window,
        IdAutoUpdate,
        state->workingConfig.autoUpdateApp,
        state->workingConfig.autoUpdateApp ? L"Автопроверка: Вкл" : L"Автопроверка: Выкл"
    );
    SetDarkButtonState(
        state->window,
        IdTranscribeAfterDownload,
        state->workingConfig.transcribeAfterDownload,
        state->workingConfig.transcribeAfterDownload ? L"Расшифровка: Вкл" : L"Расшифровка: Выкл"
    );
    SetDarkButtonState(state->window, IdVoiceOverSeparate, state->workingConfig.voiceOverMode != L"mixed");
    SetDarkButtonState(state->window, IdVoiceOverMixed, state->workingConfig.voiceOverMode == L"mixed");
    SetDarkButtonState(state->window, IdVoiceOverLangRu, state->workingConfig.voiceOverLanguage == L"ru");
    SetDarkButtonState(state->window, IdVoiceOverLangEn, state->workingConfig.voiceOverLanguage == L"en");
    SetDarkButtonState(state->window, IdVoiceOverLangKk, state->workingConfig.voiceOverLanguage == L"kk");
}

HWND CreateScrollText(HWND parent, HINSTANCE instance, const std::wstring& text) {
    auto* state = new ScrollTextState{};
    state->text = text;

    HWND view = CreateWindowExW(
        0,
        kScrollTextClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        parent,
        nullptr,
        instance,
        state
    );
    if (!view) {
        delete state;
    }
    return view;
}

void CenterWindow(HWND window, HWND owner, int width, int height) {
    RECT ownerRect = {};
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &ownerRect);
    } else {
        ownerRect = {
            0,
            0,
            GetSystemMetrics(SM_CXSCREEN),
            GetSystemMetrics(SM_CYSCREEN)
        };
    }

    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    SetWindowPos(window, nullptr, std::max(0, x), std::max(0, y), width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void RunModal(HWND owner, HWND window) {
    const bool ownerWasEnabled = owner && IsWindow(owner) && IsWindowEnabled(owner);
    if (ownerWasEnabled) {
        EnableWindow(owner, FALSE);
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    MSG message = {};
    while (IsWindow(window) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        RelayDialogTooltipMessage(state, message);
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    RestoreModalOwner(owner, ownerWasEnabled);
}

void ShowModal(DialogState* state, int width, int height) {
    RegisterDialogClasses(state->instance);

    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME;
    if (state->type == DialogType::Logs) {
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }

    HWND window = CreateWindowExW(
        exStyle,
        kDialogClassName,
        state->title.c_str(),
        style,
        0,
        0,
        width,
        height,
        state->owner,
        nullptr,
        state->instance,
        state
    );
    if (!window) {
        delete state;
        return;
    }

    CenterWindow(window, state->owner, width, height);
    RunModal(state->owner, window);
}

void LayoutMessageDialog(DialogState* state, int width, int height) {
    if (state->scrollText) {
        MoveWindow(state->scrollText, 24, 92, width - 48, height - 168, TRUE);
    }

    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    HWND copyButton = GetDlgItem(state->window, IdCopy);
    HWND cancelButton = GetDlgItem(state->window, IdCancel);
    HWND okButton = GetDlgItem(state->window, IdOk);
    HWND updateButton = GetDlgItem(state->window, IdCheckUpdates);
    if (updateButton) {
        MoveWindow(updateButton, kDialogPanelInset + kDialogButtonInset, buttonY, 190, kDialogButtonHeight, TRUE);
    }
    if (copyButton) {
        MoveWindow(
            copyButton,
            panelRight - kDialogButtonInset - 112 - kDialogButtonGap - 140,
            buttonY,
            140,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (cancelButton) {
        MoveWindow(
            cancelButton,
            panelRight - kDialogButtonInset - 112 - kDialogButtonGap - 112,
            buttonY,
            112,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (okButton) {
        MoveWindow(okButton, panelRight - kDialogButtonInset - 112, buttonY, 112, kDialogButtonHeight, TRUE);
    }
}

void LayoutFfmpegDialog(DialogState* state, int width, int height) {
    const int panelLeft = kDialogPanelInset;
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    const int buttonLeft = panelLeft + kDialogButtonInset;
    const int availableWidth = panelRight - panelLeft - (kDialogButtonInset * 2);
    const int buttonWidth = (availableWidth - (kDialogButtonGap * 2)) / 3;
    const std::array<int, 3> ids = {IdInstall, IdChooseFolder, IdSkip};
    int x = buttonLeft;
    for (int id : ids) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, buttonY, buttonWidth, kDialogButtonHeight, TRUE);
            x += buttonWidth + kDialogButtonGap;
        }
    }
}

void LayoutWhisperDialog(DialogState* state, int width, int height) {
    const int panelLeft = kDialogPanelInset;
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    const int buttonLeft = panelLeft + kDialogButtonInset;
    const int availableWidth = panelRight - panelLeft - (kDialogButtonInset * 2);
    const int buttonWidth = (availableWidth - (kDialogButtonGap * 3)) / 4;
    const std::array<int, 4> ids = {IdInstallWhisperCpu, IdInstallWhisperCuda, IdWhisperModels, IdSkip};
    int x = buttonLeft;
    for (int id : ids) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, buttonY, buttonWidth, kDialogButtonHeight, TRUE);
            x += buttonWidth + kDialogButtonGap;
        }
    }
}

void LayoutVotCliDialog(DialogState* state, int width, int height) {
    const int panelLeft = kDialogPanelInset;
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    const int buttonLeft = panelLeft + kDialogButtonInset;
    const int availableWidth = panelRight - panelLeft - (kDialogButtonInset * 2);
    const int buttonWidth = (availableWidth - kDialogButtonGap) / 2;
    const std::array<int, 2> ids = {IdInstall, IdSkip};
    int x = buttonLeft;
    for (int id : ids) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, buttonY, buttonWidth, kDialogButtonHeight, TRUE);
            x += buttonWidth + kDialogButtonGap;
        }
    }
}

void LayoutProgressDialog(DialogState* state, int width, int height) {
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;

    HWND cancel = GetDlgItem(state->window, IdCancel);
    if (cancel) {
        MoveWindow(
            cancel,
            panelRight - kDialogButtonInset - 112,
            panelBottom - kDialogButtonInset - kDialogButtonHeight,
            112,
            kDialogButtonHeight,
            TRUE
        );
    }
}

RECT WhisperModelRowRect(int index) {
    const int top = 88 + (index * 48);
    return {24, top, 594, top + 42};
}

void LayoutWhisperModelsDialog(DialogState* state, int width, int height) {
    UNREFERENCED_PARAMETER(height);
    const std::vector<WhisperModelInfo> models = WhisperManager::ModelCatalog();
    for (size_t i = 0; i < models.size(); ++i) {
        HWND button = GetDlgItem(state->window, IdWhisperModelBase + static_cast<int>(i));
        if (!button) {
            continue;
        }
        const RECT row = WhisperModelRowRect(static_cast<int>(i));
        MoveWindow(button, width - 136, row.top + 5, 104, 30, TRUE);
    }

    HWND ok = GetDlgItem(state->window, IdOk);
    if (ok) {
        MoveWindow(ok, width - kDialogPanelInset - kDialogButtonInset - 112, height - kDialogPanelInset - kDialogButtonInset - kDialogButtonHeight, 112, kDialogButtonHeight, TRUE);
    }
}

void LayoutLogsDialog(DialogState* state, int width, int height) {
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int buttonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;

    HWND copyButton = GetDlgItem(state->window, IdCopy);
    HWND okButton = GetDlgItem(state->window, IdOk);
    if (copyButton) {
        MoveWindow(
            copyButton,
            panelRight - kDialogButtonInset - 112 - kDialogButtonGap - 150,
            buttonY,
            150,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (okButton) {
        MoveWindow(okButton, panelRight - kDialogButtonInset - 112, buttonY, 112, kDialogButtonHeight, TRUE);
    }
    if (state->logView) {
        MoveWindow(
            state->logView,
            24,
            82,
            std::max(120, width - 48),
            std::max(80, buttonY - 98),
            TRUE
        );
    }
}

void LayoutSettingsDialog(DialogState* state, int width, int height) {
    HWND settingsTab = GetDlgItem(state->window, IdSettingsTab);
    HWND utilitiesTab = GetDlgItem(state->window, IdUtilitiesTab);
    const int contentLeft = SettingsContentLeft();
    const int contentRight = SettingsContentRight(width);
    const int tabLeft = contentRight - (kSettingsTabWidth * 2) - kSettingsTabGap;
    if (settingsTab) {
        MoveWindow(settingsTab, tabLeft, 28, kSettingsTabWidth, 34, TRUE);
    }
    if (utilitiesTab) {
        MoveWindow(utilitiesTab, tabLeft + kSettingsTabWidth + kSettingsTabGap, 28, kSettingsTabWidth, 34, TRUE);
    }

    const std::array<int, 19> generalIds = {
        101, 102, 103, 104, 105, 106,
        111, 112, 113, 114,
        IdAutoUpdate, IdParallelMinus, IdParallelPlus,
        IdTranscribeAfterDownload,
        IdVoiceOverSeparate, IdVoiceOverMixed,
        IdVoiceOverLangRu, IdVoiceOverLangEn, IdVoiceOverLangKk
    };
    const std::array<int, 3> utilityIds = {
        IdFfmpeg,
        IdWhisper,
        IdVotCli
    };
    SetControlsVisible(state->window, generalIds, state->settingsTab == SettingsTab::General);
    SetControlsVisible(state->window, utilityIds, state->settingsTab == SettingsTab::Utilities);

    const std::array<int, 6> qualityIds = {101, 102, 103, 104, 105, 106};
    const int qualityButtonWidth = (contentRight - contentLeft - (kDialogButtonGap * 5)) / 6;
    int x = contentLeft;
    for (int id : qualityIds) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, 118, qualityButtonWidth, 32, TRUE);
            x += qualityButtonWidth + kDialogButtonGap;
        }
    }

    const std::array<int, 4> containerIds = {111, 112, 113, 114};
    const int containerButtonWidth = 112;
    x = contentLeft;
    for (int id : containerIds) {
        HWND button = GetDlgItem(state->window, id);
        if (button) {
            MoveWindow(button, x, 206, containerButtonWidth, 32, TRUE);
            x += containerButtonWidth + kDialogButtonGap;
        }
    }

    HWND ffmpeg = GetDlgItem(state->window, IdFfmpeg);
    HWND about = GetDlgItem(state->window, IdAbout);
    HWND autoUpdate = GetDlgItem(state->window, IdAutoUpdate);
    HWND parallelMinus = GetDlgItem(state->window, IdParallelMinus);
    HWND parallelPlus = GetDlgItem(state->window, IdParallelPlus);
    HWND transcribe = GetDlgItem(state->window, IdTranscribeAfterDownload);
    HWND voiceOverSeparate = GetDlgItem(state->window, IdVoiceOverSeparate);
    HWND voiceOverMixed = GetDlgItem(state->window, IdVoiceOverMixed);
    HWND voiceOverLangRu = GetDlgItem(state->window, IdVoiceOverLangRu);
    HWND voiceOverLangEn = GetDlgItem(state->window, IdVoiceOverLangEn);
    HWND voiceOverLangKk = GetDlgItem(state->window, IdVoiceOverLangKk);
    HWND whisper = GetDlgItem(state->window, IdWhisper);
    HWND votCli = GetDlgItem(state->window, IdVotCli);
    HWND cancel = GetDlgItem(state->window, IdCancel);
    HWND ok = GetDlgItem(state->window, IdOk);
    if (autoUpdate) {
        MoveWindow(autoUpdate, contentLeft, 300, 250, 34, TRUE);
    }
    const RECT parallelValue = SettingsParallelValueRect(width);
    if (parallelMinus) {
        MoveWindow(
            parallelMinus,
            parallelValue.left - kDialogButtonGap - kSettingsParallelControlWidth,
            300,
            kSettingsParallelControlWidth,
            34,
            TRUE
        );
    }
    if (parallelPlus) {
        MoveWindow(parallelPlus, contentRight - kSettingsParallelControlWidth, 300, kSettingsParallelControlWidth, 34, TRUE);
    }
    if (ffmpeg) {
        MoveWindow(ffmpeg, contentLeft, 166, 210, 34, TRUE);
    }
    if (transcribe) {
        MoveWindow(transcribe, contentLeft, 348, 250, 34, TRUE);
    }
    if (voiceOverSeparate) {
        MoveWindow(voiceOverSeparate, contentLeft, 482, 260, 34, TRUE);
    }
    if (voiceOverMixed) {
        MoveWindow(voiceOverMixed, contentLeft + 272, 482, 300, 34, TRUE);
    }
    if (voiceOverLangRu) {
        MoveWindow(voiceOverLangRu, contentLeft, 576, 86, 34, TRUE);
    }
    if (voiceOverLangEn) {
        MoveWindow(voiceOverLangEn, contentLeft + 98, 576, 86, 34, TRUE);
    }
    if (voiceOverLangKk) {
        MoveWindow(voiceOverLangKk, contentLeft + 196, 576, 86, 34, TRUE);
    }
    if (whisper) {
        MoveWindow(whisper, contentLeft, 380, 210, 34, TRUE);
    }
    if (votCli) {
        MoveWindow(votCli, contentLeft, 612, 210, 34, TRUE);
    }
    const int panelRight = width - kDialogPanelInset;
    const int panelBottom = height - kDialogPanelInset;
    const int bottomButtonY = panelBottom - kDialogButtonInset - kDialogButtonHeight;
    if (about) {
        MoveWindow(about, contentLeft, bottomButtonY, 210, kDialogButtonHeight, TRUE);
    }
    if (cancel) {
        MoveWindow(
            cancel,
            panelRight - kDialogButtonInset - kSettingsSaveButtonWidth - kDialogButtonGap - kSettingsCancelButtonWidth,
            bottomButtonY,
            kSettingsCancelButtonWidth,
            kDialogButtonHeight,
            TRUE
        );
    }
    if (ok) {
        MoveWindow(ok, panelRight - kDialogButtonInset - kSettingsSaveButtonWidth, bottomButtonY, kSettingsSaveButtonWidth, kDialogButtonHeight, TRUE);
    }
}

void LayoutDialog(DialogState* state, int width, int height) {
    switch (state->type) {
    case DialogType::Info:
    case DialogType::Error:
    case DialogType::Confirmation:
    case DialogType::About:
        LayoutMessageDialog(state, width, height);
        break;
    case DialogType::Logs:
        LayoutLogsDialog(state, width, height);
        break;
    case DialogType::Ffmpeg:
        LayoutFfmpegDialog(state, width, height);
        break;
    case DialogType::Whisper:
        LayoutWhisperDialog(state, width, height);
        break;
    case DialogType::VotCli:
        LayoutVotCliDialog(state, width, height);
        break;
    case DialogType::Progress:
        LayoutProgressDialog(state, width, height);
        break;
    case DialogType::WhisperModels:
        LayoutWhisperModelsDialog(state, width, height);
        break;
    case DialogType::Settings:
        LayoutSettingsDialog(state, width, height);
        break;
    }
}

void DrawDialogBackground(HDC dc, const RECT& client) {
    UiRenderer::DrawBackground(dc, client);

    RECT panel = {kDialogPanelInset, kDialogPanelInset, client.right - kDialogPanelInset, client.bottom - kDialogPanelInset};
    UiRenderer::DrawPanel(dc, panel);
}

void DrawMessageDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-15, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 56};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const std::wstring subtitle = state->type == DialogType::Error
        ? L"Ошибка. Текст можно скопировать для диагностики."
        : (state->type == DialogType::Confirmation
            ? L"Доступно обновление приложения."
            : L"Информация приложения.");
    RECT subtitleRect = {24, 56, client.right - 24, 82};
    DrawTextBlock(dc, subtitle, subtitleRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DeleteObject(titleFont);
    DeleteObject(textFont);
}

void DrawFfmpegDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-15, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT messageRect = {24, 76, client.right - 24, 206};
    DrawTextBlock(
        dc,
        state->message,
        messageRect,
        kTextColor,
        textFont,
        DT_LEFT | DT_WORDBREAK
    );

    DeleteObject(titleFont);
    DeleteObject(textFont);
}

void DrawProgressDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-15, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-13, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT statusRect = {24, 72, client.right - 24, 102};
    DrawTextBlock(
        dc,
        state->message,
        statusRect,
        kTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    const std::wstring sizes = FormatProgressBytes(state->progressDownloaded, state->progressTotal);
    if (!sizes.empty()) {
        RECT sizesRect = {24, 104, client.right - 24, 126};
        DrawTextBlock(dc, sizes, sizesRect, kMutedTextColor, smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    const int percent = state->progressSuccess
        ? 100
        : CalculateProgressPercent(state->progressDownloaded, state->progressTotal);
    RECT progressRect = {24, 136, client.right - 84, 144};
    UiRenderer::DrawProgressBar(dc, progressRect, percent);

    RECT percentRect = {client.right - 74, 130, client.right - 24, 150};
    DrawTextBlock(
        dc,
        std::to_wstring(percent) + L"%",
        percentRect,
        kMutedTextColor,
        smallFont,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE
    );

    DeleteObject(titleFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void DrawUtilityStatusPath(
    HDC dc,
    const std::wstring& label,
    const std::filesystem::path& path,
    int left,
    int top,
    int right,
    HFONT textFont
) {
    RECT labelRect = {left, top, right, top + 20};
    DrawTextBlock(dc, label, labelRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT pathRect = {left, top + 22, right, top + 44};
    DrawTextBlock(dc, path.wstring(), pathRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void InvalidateProgressContent(HWND window) {
    RECT client = {};
    GetClientRect(window, &client);
    RECT progressArea = {20, 68, client.right - 20, 158};
    InvalidateRect(window, &progressArea, FALSE);
}

bool ShouldRepaintProgress(DialogState* state, const ProgressUpdate& update) {
    if (!state) {
        return false;
    }

    const int percent = CalculateProgressPercent(update.downloaded, update.total);
    const ULONGLONG now = GetTickCount64();
    const bool firstPaint = state->lastProgressPaintTick == 0;
    const bool statusChanged = update.status != state->lastProgressPaintStatus;
    const bool percentChanged = percent != state->lastProgressPaintPercent;
    const bool enoughTimePassed = now - state->lastProgressPaintTick >= 120;
    const bool complete = update.total > 0 && update.downloaded >= update.total;

    if (!firstPaint && !statusChanged && !percentChanged && !enoughTimePassed && !complete) {
        return false;
    }

    state->lastProgressPaintTick = now;
    state->lastProgressPaintPercent = percent;
    state->lastProgressPaintStatus = update.status;
    return true;
}

void DrawLogsDialog(DialogState* state, HDC dc, const RECT& client) {
    UNREFERENCED_PARAMETER(state);
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-14, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, L"Логи", titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitleRect = {24, 56, client.right - 24, 78};
    DrawTextBlock(
        dc,
        L"Выделите нужные строки или скопируйте весь текущий лог.",
        subtitleRect,
        kMutedTextColor,
        textFont,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );

    DeleteObject(titleFont);
    DeleteObject(textFont);
}

void DrawSettingsDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT labelFont = CreateUiFont(-15, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-14, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 58};
    DrawTextBlock(dc, state->title, titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (state->settingsTab == SettingsTab::General) {
        RECT qualityLabel = {24, 84, client.right - 24, 110};
        DrawTextBlock(dc, L"Качество по умолчанию", qualityLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT containerLabel = {24, 172, client.right - 24, 198};
        DrawTextBlock(dc, L"Контейнер", containerLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT behaviorLabel = {24, 266, client.right - 24, 292};
        DrawTextBlock(dc, L"Поведение", behaviorLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        DrawTextBlock(dc, L"Параллельные загрузки", {330, 300, 620, 334}, kTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextBlock(
            dc,
            std::to_wstring(state->workingConfig.maxParallelDownloads),
            SettingsParallelValueRect(client.right),
            kTextColor,
            labelFont,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );

        RECT voiceOverLabel = {24, 430, client.right - 24, 456};
        DrawTextBlock(dc, L"Озвучка", voiceOverLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT voiceOverModeLabel = {24, 456, client.right - 24, 480};
        DrawTextBlock(dc, L"Режим", voiceOverModeLabel, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT voiceOverLanguageLabel = {24, 548, client.right - 24, 574};
        DrawTextBlock(dc, L"Язык результата", voiceOverLanguageLabel, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    } else {
        RECT ffmpegLabel = {24, 84, client.right - 24, 110};
        DrawTextBlock(dc, L"FFmpeg", ffmpegLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        std::wstring ffmpegText = L"Не выбран";
        std::filesystem::path ffmpegPath;
        if (state->paths) {
            const FfmpegStatus status = FfmpegManager::Resolve(*state->paths, state->workingConfig);
            if (status.available) {
                ffmpegText = L"Найден:";
                ffmpegPath = status.ffmpegExe;
            }
        } else if (!state->workingConfig.ffmpegPath.empty()) {
            const FfmpegStatus status = FfmpegManager::ResolveUserPath(state->workingConfig.ffmpegPath);
            if (status.available) {
                ffmpegText = L"Найден:";
                ffmpegPath = status.ffmpegExe;
            } else {
                ffmpegText = L"Указанный путь недоступен";
            }
        }
        if (ffmpegPath.empty()) {
            RECT ffmpegTextRect = {24, 112, client.right - 24, 138};
            DrawTextBlock(dc, ffmpegText, ffmpegTextRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            DrawUtilityStatusPath(dc, ffmpegText, ffmpegPath, 24, 112, client.right - 24, textFont);
        }

        RECT whisperLabel = {24, 236, client.right - 24, 262};
        DrawTextBlock(dc, L"Whisper.cpp", whisperLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const ToolInstallStatus whisperStatus = ResolveDialogWhisperStatus(state);
        const std::filesystem::path whisperModelPath = ResolveDialogWhisperModelPath(state);
        std::error_code modelEc;
        const bool whisperModelAvailable = !whisperModelPath.empty() && std::filesystem::is_regular_file(whisperModelPath, modelEc);
        const WhisperUtilityStatusText whisperText = BuildWhisperUtilityStatusText(
            whisperStatus.installed,
            whisperStatus.executable,
            whisperModelAvailable,
            whisperModelPath
        );
        if (whisperStatus.installed) {
            const std::wstring whisperLabelText = L"Найден (" + WhisperManager::BackendDisplayName(whisperStatus.whisperBackend) + L"):";
            DrawUtilityStatusPath(dc, whisperLabelText, whisperStatus.executable, 24, 264, client.right - 24, textFont);
        } else {
            RECT whisperExeRect = {24, 264, client.right - 24, 288};
            DrawTextBlock(dc, whisperText.executableText, whisperExeRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        if (whisperModelAvailable) {
            DrawUtilityStatusPath(dc, L"Модель:", whisperModelPath, 24, 322, client.right - 24, textFont);
        } else {
            RECT whisperModelRect = {24, 322, client.right - 24, 346};
            DrawTextBlock(dc, whisperText.modelText, whisperModelRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        RECT votCliLabel = {24, 474, client.right - 24, 500};
        DrawTextBlock(dc, L"vot-cli", votCliLabel, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const VotCliStatus votCliStatus = VotCliManager::Resolve(state->workingConfig);
        if (votCliStatus.available) {
            DrawUtilityStatusPath(dc, L"Найден:", votCliStatus.executable, 24, 502, client.right - 24, textFont);
        } else {
            RECT votCliRect = {24, 502, client.right - 24, 526};
            DrawTextBlock(
                dc,
                votCliStatus.message.empty() ? L"Не найден" : votCliStatus.message,
                votCliRect,
                kMutedTextColor,
                textFont,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
            );
        }

        if (votCliStatus.nodeAvailable) {
            DrawUtilityStatusPath(dc, L"Node:", votCliStatus.nodeExecutable, 24, 556, client.right - 24, textFont);
        } else {
            RECT nodeRect = {24, 556, client.right - 24, 580};
            DrawTextBlock(dc, L"node.exe не найден", nodeRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    DeleteObject(titleFont);
    DeleteObject(labelFont);
    DeleteObject(textFont);
}

void DrawWhisperModelsDialog(DialogState* state, HDC dc, const RECT& client) {
    DrawDialogBackground(dc, client);

    HFONT titleFont = CreateUiFont(-18, FW_SEMIBOLD);
    HFONT labelFont = CreateUiFont(-15, FW_SEMIBOLD);
    HFONT textFont = CreateUiFont(-13, FW_NORMAL);
    HFONT smallFont = CreateUiFont(-12, FW_NORMAL);

    RECT titleRect = {24, 28, client.right - 24, 56};
    DrawTextBlock(dc, L"Модели Whisper", titleRect, kTextColor, titleFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT subtitleRect = {24, 56, client.right - 24, 80};
    DrawTextBlock(dc, L"Выберите качество распознавания. Большие модели точнее, но скачиваются и работают дольше.", subtitleRect, kMutedTextColor, textFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const std::vector<WhisperModelInfo> models = WhisperManager::ModelCatalog();
    for (size_t i = 0; i < models.size(); ++i) {
        const WhisperModelInfo& model = models[i];
        const RECT row = WhisperModelRowRect(static_cast<int>(i));

        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        Gdiplus::GraphicsPath rowPath;
        AddRoundedRect(rowPath, row, 7);
        const bool selected = IsWhisperModelSelected(state, model);
        const bool downloaded = IsWhisperModelDownloaded(state, model);
        const Gdiplus::Color rowColor = selected
            ? Gdiplus::Color(255, 47, 47, 55)
            : (downloaded ? Gdiplus::Color(255, 31, 43, 38) : Gdiplus::Color(255, 35, 35, 38));
        const Gdiplus::Color borderColor = selected
            ? Gdiplus::Color(255, 96, 96, 108)
            : (downloaded ? Gdiplus::Color(255, 72, 132, 98) : Gdiplus::Color(255, 46, 46, 50));
        Gdiplus::SolidBrush rowBrush(rowColor);
        Gdiplus::Pen rowBorder(borderColor, 1.0f);
        graphics.FillPath(&rowBrush, &rowPath);
        graphics.DrawPath(&rowBorder, &rowPath);
        if (downloaded && !selected) {
            RECT accent = {row.left + 1, row.top + 8, row.left + 5, row.bottom - 8};
            Gdiplus::GraphicsPath accentPath;
            AddRoundedRect(accentPath, accent, 2);
            Gdiplus::SolidBrush accentBrush(Gdiplus::Color(255, 78, 180, 126));
            graphics.FillPath(&accentBrush, &accentPath);
        }

        std::wstring title = model.name;
        if (model.recommended) {
            title += L" · Рекомендовано";
        } else if (model.bestQuality) {
            title += L" · Максимум качества";
        }
        if (downloaded) {
            title += L" · Скачана";
        }

        RECT modelTitleRect = {row.left + 12, row.top + 4, row.right - 128, row.top + 22};
        DrawTextBlock(dc, title, modelTitleRect, kTextColor, labelFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT tagsRect = {row.left + 12, row.top + 22, row.right - 128, row.bottom - 2};
        DrawTextBlock(dc, model.tags, tagsRect, kMutedTextColor, smallFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    DeleteObject(titleFont);
    DeleteObject(labelFont);
    DeleteObject(textFont);
    DeleteObject(smallFont);
}

void CreateMessageControls(DialogState* state) {
    state->scrollText = CreateScrollText(state->window, state->instance, state->message);
    if (state->type == DialogType::Confirmation) {
        HWND laterButton = CreateDarkButton(state->window, state->instance, L"Позже", IdCancel, false);
        HWND installButton = CreateDarkButton(state->window, state->instance, L"Установить", IdOk, true);
        AddDialogTooltip(state, laterButton, L"Закрывает предложение без установки обновления.");
        AddDialogTooltip(state, installButton, L"Скачивает и устанавливает найденное обновление.");
        return;
    }
    if (state->type == DialogType::About) {
        HWND updateButton = CreateDarkButton(state->window, state->instance, L"Проверить обновление", IdCheckUpdates, false);
        AddDialogTooltip(state, updateButton, L"Проверяет наличие новой версии приложения.");
    }
    HWND copyButton = CreateDarkButton(state->window, state->instance, L"Скопировать", IdCopy, false);
    HWND okButton = CreateDarkButton(state->window, state->instance, L"OK", IdOk, true);
    AddDialogTooltip(state, copyButton, L"Копирует текст этого окна в буфер обмена.");
    AddDialogTooltip(state, okButton, L"Закрывает окно.");
}

void CreateLogsControls(DialogState* state) {
    state->logView = CreateLogView(state->window, state->instance, state->message);
    HWND copyButton = CreateDarkButton(state->window, state->instance, L"Скопировать всё", IdCopy, false);
    HWND okButton = CreateDarkButton(state->window, state->instance, L"Закрыть", IdOk, true);
    AddDialogTooltip(state, copyButton, L"Копирует весь текущий лог в буфер обмена.");
    AddDialogTooltip(state, okButton, L"Закрывает окно логов.");
}

void CreateFfmpegControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"Установить", IdInstall, true);
    HWND folderButton = CreateDarkButton(state->window, state->instance, L"Выбрать папку", IdChooseFolder, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"Пропустить", IdSkip, false);
    AddDialogTooltip(state, installButton, L"Скачивает и настраивает локальный FFmpeg для объединения видео и аудио.");
    AddDialogTooltip(state, skipButton, L"Закрывает окно без настройки FFmpeg.");
    if (folderButton) {
        state->tooltip = CreateTooltip(
            state->window,
            folderButton,
            L"Выберите папку, где находится ffmpeg.exe, или папку выше, содержащую bin\\ffmpeg.exe, ffprobe.exe и ffplay.exe."
        );
    }
}

void CreateWhisperControls(DialogState* state) {
    HWND cpuButton = CreateDarkButton(
        state->window,
        state->instance,
        WhisperBackendButtonText(state, WhisperBackend::Cpu).c_str(),
        IdInstallWhisperCpu,
        IsWhisperBackendSelected(state, WhisperBackend::Cpu)
    );
    HWND cudaButton = CreateDarkButton(
        state->window,
        state->instance,
        WhisperBackendButtonText(state, WhisperBackend::Cuda).c_str(),
        IdInstallWhisperCuda,
        IsWhisperBackendSelected(state, WhisperBackend::Cuda)
    );
    HWND modelsButton = CreateDarkButton(state->window, state->instance, L"Модели", IdWhisperModels, false);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"Пропустить", IdSkip, false);
    AddDialogTooltip(state, cpuButton, L"Скачивает или выбирает обычную CPU-сборку whisper.cpp.");
    AddDialogTooltip(state, cudaButton, L"Скачивает или выбирает CUDA-сборку whisper.cpp для NVIDIA GPU.");
    AddDialogTooltip(state, modelsButton, L"Открывает менеджер моделей Whisper с подсказками по скорости и качеству.");
    AddDialogTooltip(state, skipButton, L"Закрывает окно Whisper без изменений.");
    RefreshWhisperBackendButtons(state);
}

void CreateVotCliControls(DialogState* state) {
    HWND installButton = CreateDarkButton(state->window, state->instance, L"Установить", IdInstall, true);
    HWND skipButton = CreateDarkButton(state->window, state->instance, L"Пропустить", IdSkip, false);
    AddDialogTooltip(state, installButton, L"Запускает npm install -g vot-cli.");
    AddDialogTooltip(state, skipButton, L"Закрывает окно vot-cli без изменений.");
}

void StartFfmpegInstallWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;

    state->worker = std::jthread([state, window, paths, config, cancelEvent](std::stop_token) {
        try {
            const FfmpegStatus status = FfmpegManager::InstallEssentials(
                paths,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->ffmpegPath = status.ffmpegExe;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка установки FFmpeg";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartWhisperInstallWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;
    const WhisperBackend backend = state->whisperBackend;

    state->worker = std::jthread([state, window, paths, config, cancelEvent, backend](std::stop_token) {
        try {
            const ToolInstallStatus status = WhisperManager::Install(
                paths,
                backend,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->whisperPath.clear();
            config->whisperBackend = status.whisperBackend;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка установки whisper.cpp";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartVotCliInstallWorker(DialogState* state) {
    if (!state || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;

    std::thread([window, config, cancelEvent]() {
        try {
            const VotCliStatus status = VotCliManager::InstallGlobal(
                [window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    auto* update = new ProgressUpdate{};
                    update->downloaded = downloaded;
                    update->total = total;
                    update->status = statusText;
                    PostMessageW(window, kProgressUpdateMessage, 0, reinterpret_cast<LPARAM>(update));
                },
                cancelEvent
            );
            config->votCliPath = status.executable;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            auto* error = new std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
            PostMessageW(window, kProgressDoneMessage, FALSE, reinterpret_cast<LPARAM>(error));
        } catch (...) {
            auto* error = new std::wstring(L"Неизвестная ошибка установки vot-cli");
            PostMessageW(window, kProgressDoneMessage, FALSE, reinterpret_cast<LPARAM>(error));
        }
    }).detach();
}

void StartWhisperModelDownloadWorker(DialogState* state) {
    if (!state || !state->paths || !state->config || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    const WhisperModelInfo model = state->whisperModel;
    AppConfig* config = state->config;
    HANDLE cancelEvent = state->cancelEvent;

    state->worker = std::jthread([state, window, paths, model, config, cancelEvent](std::stop_token) {
        try {
            const std::filesystem::path modelPath = WhisperManager::DownloadModel(
                paths,
                model,
                [state, window](std::uint64_t downloaded, std::uint64_t total, const std::wstring& statusText) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, statusText};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            config->whisperModelPath = modelPath;
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка скачивания модели Whisper";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void StartAppUpdateWorker(DialogState* state) {
    if (!state || !state->paths || !state->cancelEvent) {
        return;
    }

    HWND window = state->window;
    const AppPaths paths = *state->paths;
    const ReleaseAssetInfo release = state->release;
    HANDLE cancelEvent = state->cancelEvent;

    state->worker = std::jthread([state, window, paths, release, cancelEvent](std::stop_token) {
        try {
            const std::filesystem::path downloadedExe = AppUpdateService::DownloadUpdateExe(
                paths,
                release,
                [state, window](std::uint64_t downloaded, std::uint64_t total) {
                    {
                        std::lock_guard lock(state->progressMutex);
                        state->pendingProgress = ProgressUpdate{downloaded, total, L"Скачивание обновления..."};
                    }
                    PostMessageW(window, kProgressUpdateMessage, 0, 0);
                },
                cancelEvent
            );
            if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
                throw std::runtime_error("operation canceled");
            }
            AppUpdateService::StartDownloadedUpdate(paths, downloadedExe);
            PostMessageW(window, kProgressDoneMessage, TRUE, 0);
        } catch (const std::exception& ex) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = std::wstring(ex.what(), ex.what() + std::strlen(ex.what()));
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        } catch (...) {
            {
                std::lock_guard lock(state->progressMutex);
                state->progressError = L"Неизвестная ошибка обновления приложения";
            }
            PostMessageW(window, kProgressDoneMessage, FALSE, 0);
        }
    });
}

void CreateProgressControls(DialogState* state) {
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"Отмена", IdCancel, false);
    AddDialogTooltip(state, cancelButton, L"Отменяет текущую операцию.");
    if (state->progressMode == ProgressMode::AppUpdate) {
        StartAppUpdateWorker(state);
    } else if (state->progressMode == ProgressMode::WhisperInstall) {
        StartWhisperInstallWorker(state);
    } else if (state->progressMode == ProgressMode::WhisperModelDownload) {
        StartWhisperModelDownloadWorker(state);
    } else if (state->progressMode == ProgressMode::VotCliInstall) {
        StartVotCliInstallWorker(state);
    } else {
        StartFfmpegInstallWorker(state);
    }
}

void CreateWhisperModelsControls(DialogState* state) {
    const std::vector<WhisperModelInfo> models = WhisperManager::ModelCatalog();
    for (size_t i = 0; i < models.size(); ++i) {
        const int id = IdWhisperModelBase + static_cast<int>(i);
        HWND button = CreateDarkButton(
            state->window,
            state->instance,
            WhisperModelButtonText(state, models[i]).c_str(),
            id,
            IsWhisperModelSelected(state, models[i])
        );
        AddDialogTooltip(state, button, IsWhisperModelDownloaded(state, models[i])
            ? L"Выбирает уже скачанную модель для новых расшифровок."
            : L"Скачивает модель и выбирает её для новых расшифровок.");
    }
    HWND okButton = CreateDarkButton(state->window, state->instance, L"OK", IdOk, true);
    AddDialogTooltip(state, okButton, L"Закрывает менеджер моделей.");
}

void CreateSettingsControls(DialogState* state) {
    HWND settingsTabButton = CreateDarkButton(state->window, state->instance, L"Настройки", IdSettingsTab, true);
    HWND utilitiesTabButton = CreateDarkButton(state->window, state->instance, L"Утилиты", IdUtilitiesTab, false);

    const std::array<std::pair<int, const wchar_t*>, 6> qualityButtons = {{
        {101, L"Аудио"},
        {102, L"360p"},
        {103, L"480p"},
        {104, L"720p"},
        {105, L"1080p"},
        {106, L"Макс."}
    }};
    for (const auto& [id, text] : qualityButtons) {
        const bool selected =
            (id == 101 && state->workingConfig.quality == L"audio") ||
            (id == 102 && state->workingConfig.quality == L"360p") ||
            (id == 103 && state->workingConfig.quality == L"480p") ||
            (id == 104 && state->workingConfig.quality == L"720p") ||
            (id == 105 && state->workingConfig.quality == L"1080p") ||
            (id == 106 && state->workingConfig.quality == L"max");
        HWND button = CreateDarkButton(state->window, state->instance, text, id, selected);
        AddDialogTooltip(state, button, L"Выбирает качество, которое будет использоваться для новых задач.");
    }

    const std::array<std::pair<int, const wchar_t*>, 4> containerButtons = {{
        {111, L"Auto"},
        {112, L"MP4"},
        {113, L"MKV"},
        {114, L"WEBM"}
    }};
    for (const auto& [id, text] : containerButtons) {
        const bool selected =
            (id == 111 && state->workingConfig.container == L"auto") ||
            (id == 112 && state->workingConfig.container == L"mp4") ||
            (id == 113 && state->workingConfig.container == L"mkv") ||
            (id == 114 && state->workingConfig.container == L"webm");
        HWND button = CreateDarkButton(state->window, state->instance, text, id, selected);
        AddDialogTooltip(state, button, L"Выбирает контейнер итогового файла для новых задач.");
    }

    HWND ffmpegButton = CreateDarkButton(state->window, state->instance, L"FFmpeg", IdFfmpeg, false);
    HWND autoUpdateButton = CreateDarkButton(
        state->window,
        state->instance,
        state->workingConfig.autoUpdateApp ? L"Автопроверка: Вкл" : L"Автопроверка: Выкл",
        IdAutoUpdate,
        state->workingConfig.autoUpdateApp
    );
    HWND minusButton = CreateDarkButton(state->window, state->instance, L"-", IdParallelMinus, false);
    HWND plusButton = CreateDarkButton(state->window, state->instance, L"+", IdParallelPlus, false);
    HWND transcribeButton = CreateDarkButton(
        state->window,
        state->instance,
        state->workingConfig.transcribeAfterDownload ? L"Расшифровка: Вкл" : L"Расшифровка: Выкл",
        IdTranscribeAfterDownload,
        state->workingConfig.transcribeAfterDownload
    );
    HWND voiceOverSeparateButton = CreateDarkButton(
        state->window,
        state->instance,
        L"Отдельная дорожка",
        IdVoiceOverSeparate,
        state->workingConfig.voiceOverMode != L"mixed"
    );
    HWND voiceOverMixedButton = CreateDarkButton(
        state->window,
        state->instance,
        L"Приглушить оригинал",
        IdVoiceOverMixed,
        state->workingConfig.voiceOverMode == L"mixed"
    );
    HWND voiceOverRuButton = CreateDarkButton(state->window, state->instance, L"RU", IdVoiceOverLangRu, state->workingConfig.voiceOverLanguage == L"ru");
    HWND voiceOverEnButton = CreateDarkButton(state->window, state->instance, L"EN", IdVoiceOverLangEn, state->workingConfig.voiceOverLanguage == L"en");
    HWND voiceOverKkButton = CreateDarkButton(state->window, state->instance, L"KK", IdVoiceOverLangKk, state->workingConfig.voiceOverLanguage == L"kk");
    HWND whisperButton = CreateDarkButton(state->window, state->instance, L"Whisper", IdWhisper, false);
    HWND votCliButton = CreateDarkButton(state->window, state->instance, L"vot-cli", IdVotCli, false);
    HWND aboutButton = CreateDarkButton(state->window, state->instance, L"О программе", IdAbout, false);
    HWND cancelButton = CreateDarkButton(state->window, state->instance, L"Отмена", IdCancel, false);
    HWND saveButton = CreateDarkButton(state->window, state->instance, L"Сохранить", IdOk, true);
    AddDialogTooltip(state, settingsTabButton, L"Показывает основные настройки скачивания.");
    AddDialogTooltip(state, utilitiesTabButton, L"Показывает настройки FFmpeg и Whisper.cpp.");
    AddDialogTooltip(state, ffmpegButton, L"Открывает настройку FFmpeg.");
    AddDialogTooltip(state, autoUpdateButton, L"Включает или отключает автоматическую проверку обновлений приложения.");
    AddDialogTooltip(state, minusButton, L"Уменьшает количество параллельных загрузок.");
    AddDialogTooltip(state, plusButton, L"Увеличивает количество параллельных загрузок.");
    AddDialogTooltip(state, transcribeButton, L"Включает автоматическую расшифровку через whisper.cpp после успешного скачивания.");
    AddDialogTooltip(state, voiceOverSeparateButton, L"Собирает новый файл с отдельной аудиодорожкой перевода.");
    AddDialogTooltip(state, voiceOverMixedButton, L"Собирает новый файл, где оригинал приглушен и смешан с переводом.");
    AddDialogTooltip(state, voiceOverRuButton, L"Запрашивает русскую озвучку через vot-cli.");
    AddDialogTooltip(state, voiceOverEnButton, L"Запрашивает английскую озвучку через vot-cli.");
    AddDialogTooltip(state, voiceOverKkButton, L"Запрашивает казахскую озвучку через vot-cli.");
    AddDialogTooltip(state, whisperButton, L"Открывает установку whisper.cpp и менеджер моделей.");
    AddDialogTooltip(state, votCliButton, L"Открывает установку vot-cli через npm.");
    AddDialogTooltip(state, aboutButton, L"Показывает информацию о приложении.");
    AddDialogTooltip(state, cancelButton, L"Закрывает настройки без сохранения изменений.");
    AddDialogTooltip(state, saveButton, L"Сохраняет выбранные настройки.");
}

void RegisterDialogClasses(HINSTANCE instance) {
    WNDCLASSEXW dialogClass = {};
    dialogClass.cbSize = sizeof(dialogClass);
    dialogClass.style = CS_HREDRAW | CS_VREDRAW;
    dialogClass.lpfnWndProc = DialogWindowProc;
    dialogClass.hInstance = instance;
    dialogClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    dialogClass.hbrBackground = nullptr;
    dialogClass.lpszClassName = kDialogClassName;
    RegisterClassExW(&dialogClass);

    WNDCLASSEXW buttonClass = {};
    buttonClass.cbSize = sizeof(buttonClass);
    buttonClass.lpfnWndProc = DialogButtonProc;
    buttonClass.hInstance = instance;
    buttonClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
    buttonClass.hbrBackground = nullptr;
    buttonClass.lpszClassName = kDialogButtonClassName;
    RegisterClassExW(&buttonClass);

    WNDCLASSEXW scrollTextClass = {};
    scrollTextClass.cbSize = sizeof(scrollTextClass);
    scrollTextClass.lpfnWndProc = ScrollTextProc;
    scrollTextClass.hInstance = instance;
    scrollTextClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    scrollTextClass.hbrBackground = nullptr;
    scrollTextClass.lpszClassName = kScrollTextClassName;
    RegisterClassExW(&scrollTextClass);

    WNDCLASSEXW logViewClass = {};
    logViewClass.cbSize = sizeof(logViewClass);
    logViewClass.style = CS_HREDRAW | CS_VREDRAW;
    logViewClass.lpfnWndProc = LogViewProc;
    logViewClass.hInstance = instance;
    logViewClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    logViewClass.hbrBackground = nullptr;
    logViewClass.lpszClassName = kLogViewClassName;
    RegisterClassExW(&logViewClass);

    WNDCLASSEXW logMenuClass = {};
    logMenuClass.cbSize = sizeof(logMenuClass);
    logMenuClass.lpfnWndProc = LogCopyMenuProc;
    logMenuClass.hInstance = instance;
    logMenuClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    logMenuClass.hbrBackground = nullptr;
    logMenuClass.lpszClassName = kLogCopyMenuClassName;
    RegisterClassExW(&logMenuClass);
}

LRESULT CALLBACK DialogWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<DialogState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        EnableDarkTitleBar(window);
        return TRUE;
    }

    switch (message) {
    case WM_GETMINMAXINFO:
        if (state && state->type == DialogType::Logs) {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = 720;
            minMaxInfo->ptMinTrackSize.y = 460;
            return 0;
        }
        break;

    case WM_CREATE:
        if (state) {
            if (state->type == DialogType::Settings) {
                CreateSettingsControls(state);
            } else if (state->type == DialogType::WhisperModels) {
                CreateWhisperModelsControls(state);
            } else if (state->type == DialogType::Whisper) {
                CreateWhisperControls(state);
            } else if (state->type == DialogType::VotCli) {
                CreateVotCliControls(state);
            } else if (state->type == DialogType::Ffmpeg) {
                CreateFfmpegControls(state);
            } else if (state->type == DialogType::Progress) {
                CreateProgressControls(state);
            } else if (state->type == DialogType::Logs) {
                CreateLogsControls(state);
            } else {
                CreateMessageControls(state);
            }
            RECT client = {};
            GetClientRect(window, &client);
            LayoutDialog(state, client.right, client.bottom);
        }
        return 0;

    case WM_SIZE:
        if (state) {
            LayoutDialog(state, LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(window, nullptr, TRUE);
            if (state->logView) {
                InvalidateRect(state->logView, nullptr, TRUE);
            }
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        if (state) {
            PaintBuffered(window, [state](HDC dc, const RECT& client) {
                if (state->type == DialogType::Settings) {
                    DrawSettingsDialog(state, dc, client);
                } else if (state->type == DialogType::WhisperModels) {
                    DrawWhisperModelsDialog(state, dc, client);
                } else if (state->type == DialogType::Whisper) {
                    DrawFfmpegDialog(state, dc, client);
                } else if (state->type == DialogType::VotCli) {
                    DrawFfmpegDialog(state, dc, client);
                } else if (state->type == DialogType::Ffmpeg) {
                    DrawFfmpegDialog(state, dc, client);
                } else if (state->type == DialogType::Progress) {
                    DrawProgressDialog(state, dc, client);
                } else if (state->type == DialogType::Logs) {
                    DrawLogsDialog(state, dc, client);
                } else {
                    DrawMessageDialog(state, dc, client);
                }
            });
        }
        return 0;

    case WM_COMMAND:
        if (state) {
            const int command = LOWORD(wParam);
            if (state->type == DialogType::WhisperModels && command >= IdWhisperModelBase) {
                const std::vector<WhisperModelInfo> models = WhisperManager::ModelCatalog();
                const size_t index = static_cast<size_t>(command - IdWhisperModelBase);
                if (index < models.size() && state->paths && state->config) {
                    const WhisperModelInfo& model = models[index];
                    if (IsWhisperModelDownloaded(state, model)) {
                        state->config->whisperModelPath = WhisperManager::ModelPath(*state->paths, model);
                        state->workingConfig = *state->config;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                    } else if (ShowWhisperModelDownloadProgress(window, state->instance, *state->paths, *state->config, model)) {
                        state->workingConfig = *state->config;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                    }
                    RefreshWhisperModelButtons(state);
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                return 0;
            }

            switch (command) {
            case IdSettingsTab:
                if (state->type == DialogType::Settings) {
                    state->settingsTab = SettingsTab::General;
                    RefreshSettingsButtons(state);
                    RECT client = {};
                    GetClientRect(window, &client);
                    LayoutDialog(state, client.right, client.bottom);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            case IdUtilitiesTab:
                if (state->type == DialogType::Settings) {
                    state->settingsTab = SettingsTab::Utilities;
                    RefreshSettingsButtons(state);
                    RECT client = {};
                    GetClientRect(window, &client);
                    LayoutDialog(state, client.right, client.bottom);
                    InvalidateRect(window, nullptr, FALSE);
                }
                return 0;
            case 101:
                state->workingConfig.quality = L"audio";
                RefreshSettingsButtons(state);
                return 0;
            case 102:
                state->workingConfig.quality = L"360p";
                RefreshSettingsButtons(state);
                return 0;
            case 103:
                state->workingConfig.quality = L"480p";
                RefreshSettingsButtons(state);
                return 0;
            case 104:
                state->workingConfig.quality = L"720p";
                RefreshSettingsButtons(state);
                return 0;
            case 105:
                state->workingConfig.quality = L"1080p";
                RefreshSettingsButtons(state);
                return 0;
            case 106:
                state->workingConfig.quality = L"max";
                RefreshSettingsButtons(state);
                return 0;
            case 111:
                state->workingConfig.container = L"auto";
                RefreshSettingsButtons(state);
                return 0;
            case 112:
                state->workingConfig.container = L"mp4";
                RefreshSettingsButtons(state);
                return 0;
            case 113:
                state->workingConfig.container = L"mkv";
                RefreshSettingsButtons(state);
                return 0;
            case 114:
                state->workingConfig.container = L"webm";
                RefreshSettingsButtons(state);
                return 0;
            case IdAutoUpdate:
                state->workingConfig.autoUpdateApp = !state->workingConfig.autoUpdateApp;
                RefreshSettingsButtons(state);
                return 0;
            case IdTranscribeAfterDownload:
                state->workingConfig.transcribeAfterDownload = !state->workingConfig.transcribeAfterDownload;
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceOverSeparate:
                state->workingConfig.voiceOverMode = L"separate";
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceOverMixed:
                state->workingConfig.voiceOverMode = L"mixed";
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceOverLangRu:
                state->workingConfig.voiceOverLanguage = L"ru";
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceOverLangEn:
                state->workingConfig.voiceOverLanguage = L"en";
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdVoiceOverLangKk:
                state->workingConfig.voiceOverLanguage = L"kk";
                RefreshSettingsButtons(state);
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            case IdWhisper:
                if (state->type == DialogType::Settings && state->paths && state->config) {
                    if (ShowWhisperDialog(window, state->instance, *state->paths, state->workingConfig)) {
                        *state->config = state->workingConfig;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    return 0;
                }
                return 0;
            case IdVotCli:
                if (state->type == DialogType::Settings && state->paths && state->config) {
                    if (ShowVotCliDialog(window, state->instance, *state->paths, state->workingConfig)) {
                        *state->config = state->workingConfig;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    return 0;
                }
                return 0;
            case IdWhisperModels:
                if (state->type == DialogType::Whisper && state->paths && state->config) {
                    if (ShowWhisperModelsDialog(window, state->instance, *state->paths, *state->config)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        RefreshWhisperDialogText(state);
                        InvalidateRect(window, nullptr, FALSE);
                    }
                    return 0;
                }
                return 0;
            case IdInstallWhisperCpu:
            case IdInstallWhisperCuda:
                if (state->type == DialogType::Whisper && state->paths && state->config) {
                    const WhisperBackend backend = command == IdInstallWhisperCuda ? WhisperBackend::Cuda : WhisperBackend::Cpu;
                    if (ResolveDialogWhisperBackendStatus(state, backend).installed) {
                        SelectWhisperBackend(state, backend);
                    } else if (ShowWhisperInstallProgress(window, state->instance, *state->paths, *state->config, backend)) {
                        state->workingConfig = *state->config;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        RefreshWhisperDialogText(state);
                        RefreshWhisperBackendButtons(state);
                    }
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                return 0;
            case IdParallelMinus:
                state->workingConfig.maxParallelDownloads = std::clamp(state->workingConfig.maxParallelDownloads - 1, 3, 10);
                RefreshSettingsButtons(state);
                {
                    RECT client = {};
                    GetClientRect(state->window, &client);
                    RECT parallelValue = SettingsParallelValueRect(client.right);
                    InvalidateRect(state->window, &parallelValue, FALSE);
                }
                return 0;
            case IdParallelPlus:
                state->workingConfig.maxParallelDownloads = std::clamp(state->workingConfig.maxParallelDownloads + 1, 3, 10);
                RefreshSettingsButtons(state);
                {
                    RECT client = {};
                    GetClientRect(state->window, &client);
                    RECT parallelValue = SettingsParallelValueRect(client.right);
                    InvalidateRect(state->window, &parallelValue, FALSE);
                }
                return 0;
            case IdCopy:
                CopyTextToClipboard(window, state->message);
                return 0;
            case IdCheckUpdates:
                if (!state->paths) {
                    ShowErrorDialog(window, state->instance, L"Обновления", L"Путь приложения недоступен.");
                    return 0;
                }
                if (CheckAndOfferAppUpdate(window, state->instance, *state->paths, true)) {
                    HWND owner = GetAncestor(window, GA_ROOTOWNER);
                    DestroyWindow(window);
                    if (owner && IsWindow(owner)) {
                        PostMessageW(owner, WM_CLOSE, 0, 0);
                    }
                }
                return 0;
            case IdAbout:
                if (state->paths) {
                    ShowAboutDialog(window, state->instance, *state->paths);
                }
                return 0;
            case IdFfmpeg:
                if (state->paths && state->config) {
                    if (ShowFfmpegDialog(window, state->instance, *state->paths, state->workingConfig)) {
                        RefreshSettingsButtons(state);
                        InvalidateRect(window, nullptr, FALSE);
                        *state->config = state->workingConfig;
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                    }
                }
                return 0;
            case IdInstall:
                if (state->type == DialogType::Ffmpeg && state->paths && state->config) {
                    if (ShowFfmpegInstallProgress(window, state->instance, *state->paths, *state->config)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::Whisper && state->paths && state->config) {
                    if (ShowWhisperInstallProgress(window, state->instance, *state->paths, *state->config, WhisperBackend::Cpu)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                if (state->type == DialogType::VotCli && state->paths && state->config) {
                    if (ShowVotCliInstallProgress(window, state->instance, *state->paths, *state->config)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case IdChooseFolder:
                if (state->type == DialogType::Ffmpeg && state->config) {
                    const std::optional<std::filesystem::path> selected = PickFfmpegFolder(window);
                    if (selected && ApplySelectedFfmpegPath(window, state->instance, *state->config, *selected)) {
                        if (state->savedResult) {
                            *state->savedResult = true;
                        }
                        DestroyWindow(window);
                    }
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case IdSkip:
                DestroyWindow(window);
                return 0;
            case IdCancel:
                if (state->type == DialogType::Progress && !state->progressDone) {
                    if (state->cancelEvent) {
                        SetEvent(state->cancelEvent);
                    }
                    state->message = L"Отмена...";
                    InvalidateRect(window, nullptr, FALSE);
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case IdOk:
                if (state->type == DialogType::Settings && state->config) {
                    *state->config = state->workingConfig;
                    if (state->savedResult) {
                        *state->savedResult = true;
                    }
                } else if (state->type == DialogType::Confirmation && state->savedResult) {
                    *state->savedResult = true;
                }
                DestroyWindow(window);
                return 0;
            default:
                return 0;
            }
        }
        break;

    case kProgressUpdateMessage:
        if (state && state->type == DialogType::Progress) {
            std::optional<ProgressUpdate> update;
            {
                std::lock_guard lock(state->progressMutex);
                update = std::move(state->pendingProgress);
                state->pendingProgress.reset();
            }
            if (!update) {
                return 0;
            }
            const bool repaint = ShouldRepaintProgress(state, *update);
            state->message = update->status;
            state->progressDownloaded = update->downloaded;
            state->progressTotal = update->total;
            if (repaint) {
                InvalidateProgressContent(window);
            }
        }
        return 0;

    case kProgressDoneMessage:
        if (state && state->type == DialogType::Progress) {
            state->progressDone = true;
            state->progressSuccess = wParam == TRUE;
            if (state->progressSuccess) {
                if (state->progressMode == ProgressMode::AppUpdate) {
                    state->message = L"Обновление скачано. Приложение будет закрыто и запущено заново.";
                } else if (state->progressMode == ProgressMode::WhisperInstall) {
                    state->message = L"whisper.cpp " + WhisperManager::BackendDisplayName(state->whisperBackend) + L" установлен.";
                } else if (state->progressMode == ProgressMode::WhisperModelDownload) {
                    state->message = L"Модель Whisper скачана и выбрана.";
                } else if (state->progressMode == ProgressMode::VotCliInstall) {
                    state->message = L"vot-cli установлен.";
                } else {
                    state->message = L"FFmpeg установлен.";
                }
                if (state->savedResult) {
                    *state->savedResult = true;
                }
                if (state->progressMode == ProgressMode::AppUpdate) {
                    DestroyWindow(window);
                    return 0;
                }
            } else {
                std::optional<std::wstring> error;
                {
                    std::lock_guard lock(state->progressMutex);
                    error = std::move(state->progressError);
                    state->progressError.reset();
                }
                if (error) {
                    state->message = *error;
                } else if (state->progressMode == ProgressMode::AppUpdate) {
                    state->message = L"Не удалось обновить приложение.";
                } else if (state->progressMode == ProgressMode::WhisperInstall) {
                    state->message = L"Не удалось установить whisper.cpp.";
                } else if (state->progressMode == ProgressMode::WhisperModelDownload) {
                    state->message = L"Не удалось скачать модель Whisper.";
                } else if (state->progressMode == ProgressMode::VotCliInstall) {
                    state->message = L"Не удалось установить vot-cli.";
                } else {
                    state->message = L"Не удалось установить FFmpeg.";
                }
            }
            SetDarkButtonState(window, IdCancel, state->progressSuccess, L"OK");
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_CLOSE:
        if (state && state->type == DialogType::Progress && !state->progressDone) {
            if (state->cancelEvent) {
                SetEvent(state->cancelEvent);
            }
            state->message = L"Отмена...";
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        DestroyWindow(window);
        return 0;

    case WM_NCDESTROY:
        if (state && state->tooltip) {
            DestroyWindow(state->tooltip);
            state->tooltip = nullptr;
        }
        if (state) {
            for (HWND tooltip : state->tooltips) {
                if (tooltip) {
                    DestroyWindow(tooltip);
                }
            }
            state->tooltips.clear();
        }
        if (state && state->worker.joinable()) {
            if (state->cancelEvent) {
                SetEvent(state->cancelEvent);
            }
            state->worker.request_stop();
            state->worker.join();
        }
        if (state && state->cancelEvent) {
            CloseHandle(state->cancelEvent);
            state->cancelEvent = nullptr;
        }
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CALLBACK DialogButtonProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
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
                SendMessageW(
                    GetParent(window),
                    WM_COMMAND,
                    MAKEWPARAM(state->commandId, BN_CLICKED),
                    reinterpret_cast<LPARAM>(window)
                );
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (state && (wParam == VK_SPACE || wParam == VK_RETURN)) {
            SendMessageW(
                GetParent(window),
                WM_COMMAND,
                MAKEWPARAM(state->commandId, BN_CLICKED),
                reinterpret_cast<LPARAM>(window)
            );
            return 0;
        }
        break;

    case WM_PAINT:
        {
            PaintBuffered(window, [state](HDC dc, const RECT& client) {
                if (state) {
                    UiRenderer::DrawButton(dc, client, state->text.c_str(), state->primary, state->pressed, state->hot, true);
                }
            });
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

int MeasureTextHeight(HDC dc, HFONT font, const std::wstring& text, int width) {
    HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(dc, font));
    RECT measure = {0, 0, width, 1};
    DrawTextW(dc, text.c_str(), -1, &measure, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    return measure.bottom - measure.top;
}

bool ClampScroll(HWND window, ScrollTextState* state, int visibleHeight) {
    UNREFERENCED_PARAMETER(window);
    const int previousScrollY = state->scrollY;
    const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
    state->scrollY = std::clamp(state->scrollY, 0, maxScroll);
    return state->scrollY != previousScrollY;
}

bool SetScrollY(HWND window, ScrollTextState* state, int desiredScrollY, int visibleHeight) {
    const int previousScrollY = state->scrollY;
    state->scrollY = desiredScrollY;
    ClampScroll(window, state, visibleHeight);
    return state->scrollY != previousScrollY;
}

bool ClampScroll(HWND window, LogViewState* state, int visibleHeight) {
    UNREFERENCED_PARAMETER(window);
    const int previousScrollY = state->scrollY;
    const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
    state->scrollY = std::clamp(state->scrollY, 0, maxScroll);
    return state->scrollY != previousScrollY;
}

bool SetScrollY(HWND window, LogViewState* state, int desiredScrollY, int visibleHeight) {
    const int previousScrollY = state->scrollY;
    state->scrollY = desiredScrollY;
    ClampScroll(window, state, visibleHeight);
    return state->scrollY != previousScrollY;
}

int GetScrollVisibleEnd(const RECT& client) {
    return std::max(1, static_cast<int>(client.bottom - client.top) - kScrollTextClipBottomPadding);
}

RECT GetScrollbarThumb(const RECT& client, int contentHeight, int scrollY) {
    constexpr int barWidth = 8;
    constexpr int padding = 6;
    RECT track = {client.right - barWidth - padding, client.top + padding, client.right - padding, client.bottom - padding};
    const int trackHeight = std::max(1, static_cast<int>(track.bottom - track.top));
    const int visibleHeight = GetScrollVisibleEnd(client);
    const int thumbHeight = std::clamp((visibleHeight * trackHeight) / std::max(visibleHeight, contentHeight), 28, trackHeight);
    const int maxScroll = std::max(1, contentHeight - visibleHeight);
    const int maxThumbTravel = std::max(0, trackHeight - thumbHeight);
    const int thumbTop = static_cast<int>(track.top) + (scrollY * maxThumbTravel) / maxScroll;
    return {track.left, thumbTop, track.right, thumbTop + thumbHeight};
}

LRESULT CALLBACK ScrollTextProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    ScrollTextState* state = reinterpret_cast<ScrollTextState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<ScrollTextState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
        if (state) {
            RECT client = {};
            GetClientRect(window, &client);
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int desiredScrollY = state->scrollY - ((delta / WHEEL_DELTA) * 42);
            if (SetScrollY(window, state, desiredScrollY, GetScrollVisibleEnd(client))) {
                InvalidateRect(window, nullptr, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (state) {
            RECT client = {};
            GetClientRect(window, &client);
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (state->contentHeight > GetScrollVisibleEnd(client) && PtInRect(&thumb, point)) {
                state->draggingThumb = true;
                state->dragStartY = point.y;
                state->dragStartScrollY = state->scrollY;
                SetCapture(window);
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (state && state->draggingThumb) {
            RECT client = {};
            GetClientRect(window, &client);
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            const int visibleHeight = GetScrollVisibleEnd(client);
            const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
            const int trackTravel = std::max(
                1,
                static_cast<int>(client.bottom - client.top) - 12 - static_cast<int>(thumb.bottom - thumb.top)
            );
            const int dy = GET_Y_LPARAM(lParam) - state->dragStartY;
            const int desiredScrollY = state->dragStartScrollY + (dy * maxScroll) / trackTravel;
            if (SetScrollY(window, state, desiredScrollY, visibleHeight)) {
                InvalidateRect(window, nullptr, TRUE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (state && state->draggingThumb) {
            state->draggingThumb = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        return 0;

    case WM_PAINT:
        if (state) {
            PaintBuffered(window, [window, state](HDC dc, const RECT& client) {
                UiRenderer::DrawInputFrame(dc, client);

                HFONT font = CreateUiFont(-15, FW_NORMAL);
                const int textWidth = std::max(40, static_cast<int>(client.right - client.left) - 38);
                const int textHeight = MeasureTextHeight(dc, font, state->text, textWidth);
                state->contentHeight = textHeight + kScrollTextTopPadding + kScrollTextBottomPadding;
                ClampScroll(window, state, GetScrollVisibleEnd(client));

                RECT textRect = {
                    client.left + 14,
                    client.top + kScrollTextTopPadding - state->scrollY,
                    client.right - 24,
                    client.top + kScrollTextTopPadding - state->scrollY + textHeight
                };
                HRGN clip = CreateRectRgn(
                    client.left + 10,
                    client.top + kScrollTextClipTopPadding,
                    client.right - 16,
                    client.bottom - kScrollTextClipBottomPadding
                );
                SelectClipRgn(dc, clip);
                DrawTextBlock(dc, state->text, textRect, kTextColor, font, DT_LEFT | DT_WORDBREAK);
                SelectClipRgn(dc, nullptr);
                DeleteObject(clip);

                if (state->contentHeight > GetScrollVisibleEnd(client)) {
                    Gdiplus::Graphics graphics(dc);
                    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                    RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
                    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 39, 39, 43));
                    Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 86, 86, 92));
                    graphics.FillRectangle(
                        &trackBrush,
                        static_cast<INT>(client.right - 14),
                        static_cast<INT>(client.top + 8),
                        6,
                        static_cast<INT>(client.bottom - client.top - 16)
                    );
                    graphics.FillRectangle(
                        &thumbBrush,
                        static_cast<INT>(thumb.left),
                        static_cast<INT>(thumb.top),
                        static_cast<INT>(thumb.right - thumb.left),
                        static_cast<INT>(thumb.bottom - thumb.top)
                    );
                }

                DeleteObject(font);
            });
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

std::vector<std::wstring> SplitLogLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L'\n', start);
        std::wstring line = end == std::wstring::npos
            ? text.substr(start)
            : text.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    if (lines.empty()) {
        lines.push_back({});
    }
    return lines;
}

void RebuildLogLayout(HDC dc, HFONT font, LogViewState* state, const RECT& client) {
    const int textWidth = std::max(40, static_cast<int>(client.right - client.left) - 42);
    int y = kScrollTextTopPadding;
    state->layouts.clear();
    state->layouts.reserve(state->lines.size());
    for (const std::wstring& line : state->lines) {
        const int textHeight = std::max(18, MeasureTextHeight(dc, font, line.empty() ? L" " : line, textWidth));
        LogLineLayout layout;
        layout.rect = {0, y, textWidth, y + textHeight + 8};
        state->layouts.push_back(layout);
        y = layout.rect.bottom;
    }
    state->contentHeight = y + kScrollTextBottomPadding;
    ClampScroll(nullptr, state, GetScrollVisibleEnd(client));
}

int HitTestLogLine(LogViewState* state, int contentY) {
    for (size_t index = 0; index < state->layouts.size(); ++index) {
        const RECT& rect = state->layouts[index].rect;
        if (contentY >= rect.top && contentY < rect.bottom) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

void ShowLogCopyMenu(HWND owner, HINSTANCE instance, POINT screenPoint, const std::wstring& text) {
    if (text.empty()) {
        return;
    }

    auto* state = new LogCopyMenuState{};
    state->owner = owner;
    state->text = text;

    constexpr int menuWidth = 148;
    constexpr int menuHeight = 36;
    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = std::max(
        static_cast<int>(workArea.left),
        std::min(static_cast<int>(screenPoint.x), static_cast<int>(workArea.right) - menuWidth)
    );
    const int y = std::max(
        static_cast<int>(workArea.top),
        std::min(static_cast<int>(screenPoint.y), static_cast<int>(workArea.bottom) - menuHeight)
    );

    HWND menu = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kLogCopyMenuClassName,
        L"",
        WS_POPUP,
        x,
        y,
        menuWidth,
        menuHeight,
        owner,
        nullptr,
        instance,
        state
    );
    if (!menu) {
        delete state;
        return;
    }
    ShowWindow(menu, SW_SHOW);
    SetFocus(menu);
}

HWND CreateLogView(HWND parent, HINSTANCE instance, const std::wstring& text) {
    auto* state = new LogViewState{};
    state->text = text;
    state->lines = SplitLogLines(text);

    HWND view = CreateWindowExW(
        0,
        kLogViewClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0,
        0,
        10,
        10,
        parent,
        nullptr,
        instance,
        state
    );
    if (!view) {
        delete state;
    }
    return view;
}

LRESULT CALLBACK LogCopyMenuProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    LogCopyMenuState* state = reinterpret_cast<LogCopyMenuState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<LogCopyMenuState*>(create->lpCreateParams);
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
                {1, L"Копировать", false}
            };
            UiRenderer::DrawPopupMenu(dc, client, items, state && state->hot ? 1 : 0);
        });
        return 0;

    case WM_LBUTTONUP:
        if (state) {
            CopyTextToClipboard(state->owner ? state->owner : window, state->text);
        }
        DestroyWindow(window);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            if (state) {
                CopyTextToClipboard(state->owner ? state->owner : window, state->text);
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

LRESULT CALLBACK LogViewProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    LogViewState* state = reinterpret_cast<LogViewState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<LogViewState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }

    switch (message) {
    case WM_ERASEBKGND:
        return 1;

    case WM_MOUSEWHEEL:
        if (state) {
            RECT client = {};
            GetClientRect(window, &client);
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            const int desiredScrollY = state->scrollY - ((delta / WHEEL_DELTA) * 44);
            if (SetScrollY(window, state, desiredScrollY, GetScrollVisibleEnd(client))) {
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        if (state) {
            SetFocus(window);
            RECT client = {};
            GetClientRect(window, &client);
            POINT point = {};
            if (message == WM_CONTEXTMENU) {
                point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (point.x == -1 && point.y == -1) {
                    if (state->selectedLine >= 0 && state->selectedLine < static_cast<int>(state->layouts.size())) {
                        const RECT selected = state->layouts[static_cast<size_t>(state->selectedLine)].rect;
                        point = {client.left + 18, client.top + selected.top - state->scrollY + 4};
                    } else {
                        return 0;
                    }
                } else {
                    ScreenToClient(window, &point);
                }
            } else {
                point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            }
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            if (message == WM_LBUTTONDOWN &&
                state->contentHeight > GetScrollVisibleEnd(client) &&
                PtInRect(&thumb, point)) {
                state->draggingThumb = true;
                state->dragStartY = point.y;
                state->dragStartScrollY = state->scrollY;
                SetCapture(window);
                return 0;
            }

            HDC dc = GetDC(window);
            HFONT font = CreateUiFont(-13, FW_NORMAL);
            RebuildLogLayout(dc, font, state, client);
            DeleteObject(font);
            ReleaseDC(window, dc);

            const int hit = HitTestLogLine(state, point.y + state->scrollY);
            if (hit >= 0) {
                state->selectedLine = hit;
                InvalidateRect(window, nullptr, FALSE);
                if (message == WM_RBUTTONUP || message == WM_CONTEXTMENU) {
                    POINT screenPoint = point;
                    ClientToScreen(window, &screenPoint);
                    HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE));
                    ShowLogCopyMenu(window, instance, screenPoint, state->lines[static_cast<size_t>(hit)]);
                }
            }
        }
        return 0;

    case WM_MOUSEMOVE:
        if (state && state->draggingThumb) {
            RECT client = {};
            GetClientRect(window, &client);
            RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
            const int visibleHeight = GetScrollVisibleEnd(client);
            const int maxScroll = std::max(0, state->contentHeight - visibleHeight);
            const int trackTravel = std::max(
                1,
                static_cast<int>(client.bottom - client.top) - 12 - static_cast<int>(thumb.bottom - thumb.top)
            );
            const int dy = GET_Y_LPARAM(lParam) - state->dragStartY;
            const int desiredScrollY = state->dragStartScrollY + (dy * maxScroll) / trackTravel;
            if (SetScrollY(window, state, desiredScrollY, visibleHeight)) {
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (state && state->draggingThumb) {
            state->draggingThumb = false;
            if (GetCapture() == window) {
                ReleaseCapture();
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (state) {
            const bool controlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (controlDown && wParam == 'C' && state->selectedLine >= 0 &&
                state->selectedLine < static_cast<int>(state->lines.size())) {
                CopyTextToClipboard(window, state->lines[static_cast<size_t>(state->selectedLine)]);
                return 0;
            }
        }
        break;

    case WM_PAINT:
        if (state) {
            PaintBuffered(window, [window, state](HDC dc, const RECT& client) {
                UiRenderer::DrawInputFrame(dc, client);

                HFONT font = CreateUiFont(-13, FW_NORMAL);
                RebuildLogLayout(dc, font, state, client);

                HRGN clip = CreateRectRgn(
                    client.left + 10,
                    client.top + kScrollTextClipTopPadding,
                    client.right - 16,
                    client.bottom - kScrollTextClipBottomPadding
                );
                SelectClipRgn(dc, clip);

                const int textLeft = client.left + 14;
                const int textRight = client.right - 28;
                for (size_t index = 0; index < state->lines.size(); ++index) {
                    const RECT layout = state->layouts[index].rect;
                    RECT visualRect = {
                        textLeft,
                        client.top + layout.top - state->scrollY,
                        textRight,
                        client.top + layout.bottom - state->scrollY
                    };
                    if (visualRect.bottom < client.top || visualRect.top > client.bottom) {
                        continue;
                    }
                    if (static_cast<int>(index) == state->selectedLine) {
                        Gdiplus::Graphics graphics(dc);
                        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                        RECT selected = {visualRect.left - 6, visualRect.top - 2, visualRect.right + 2, visualRect.bottom - 4};
                        Gdiplus::GraphicsPath path;
                        AddRoundedRect(path, selected, 5);
                        Gdiplus::SolidBrush fill(Gdiplus::Color(255, 48, 48, 54));
                        graphics.FillPath(&fill, &path);
                    }

                    RECT textRect = {
                        visualRect.left,
                        visualRect.top,
                        visualRect.right,
                        visualRect.bottom - 6
                    };
                    DrawTextBlock(
                        dc,
                        state->lines[index],
                        textRect,
                        kTextColor,
                        font,
                        DT_LEFT | DT_WORDBREAK
                    );
                }
                SelectClipRgn(dc, nullptr);
                DeleteObject(clip);

                if (state->contentHeight > GetScrollVisibleEnd(client)) {
                    Gdiplus::Graphics graphics(dc);
                    graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                    RECT thumb = GetScrollbarThumb(client, state->contentHeight, state->scrollY);
                    Gdiplus::SolidBrush trackBrush(Gdiplus::Color(255, 39, 39, 43));
                    Gdiplus::SolidBrush thumbBrush(Gdiplus::Color(255, 86, 86, 92));
                    graphics.FillRectangle(
                        &trackBrush,
                        static_cast<INT>(client.right - 14),
                        static_cast<INT>(client.top + 8),
                        6,
                        static_cast<INT>(client.bottom - client.top - 16)
                    );
                    graphics.FillRectangle(
                        &thumbBrush,
                        static_cast<INT>(thumb.left),
                        static_cast<INT>(thumb.top),
                        static_cast<INT>(thumb.right - thumb.left),
                        static_cast<INT>(thumb.bottom - thumb.top)
                    );
                }

                DeleteObject(font);
            });
        }
        return 0;

    case WM_SIZE:
        if (state) {
            state->layouts.clear();
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

bool ShowConfirmationDialog(
    HWND owner,
    HINSTANCE instance,
    const std::wstring& title,
    const std::wstring& message
) {
    auto* state = new DialogState{};
    state->type = DialogType::Confirmation;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->message = message;
    bool confirmed = false;
    state->savedResult = &confirmed;
    ShowModal(state, 560, 360);
    return confirmed;
}

} // namespace

void ShowInfoDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
    auto* state = new DialogState{};
    state->type = DialogType::Info;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->message = message;
    ShowModal(state, 560, 360);
}

void ShowErrorDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message) {
    auto* state = new DialogState{};
    state->type = DialogType::Error;
    state->instance = instance;
    state->owner = owner;
    state->title = title;
    state->message = message;
    ShowModal(state, 620, 420);
}

void ShowLogsDialog(HWND owner, HINSTANCE instance, const std::wstring& logText) {
    auto* state = new DialogState{};
    state->type = DialogType::Logs;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Логи";
    state->message = logText.empty() ? L"Лог пока пуст." : logText;
    ShowModal(state, 860, 560);
}

bool ShowSettingsDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Settings;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Настройки";
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    state->workingConfig = config;
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, kSettingsDialogWidth, kSettingsDialogHeight);
    return saved;
}

void ShowAboutDialog(HWND owner, HINSTANCE instance, const AppPaths& paths) {
    auto* state = new DialogState{};
    state->type = DialogType::About;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->title = L"О программе";
    state->message =
        L"YouTube Downloader\n\n"
        L"Портативный Win32 загрузчик видео с YouTube.\n"
        L"Поддерживает расшифровку аудиодорожек через Whisper.cpp.\n\n"
        L"Версия: " YTD_APP_VERSION_WIDE;
    ShowModal(state, 560, 360);
}

bool ShowFfmpegDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Ffmpeg;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    const FfmpegStatus status = ResolveDialogFfmpegStatus(state);
    state->title = FfmpegDialogTitle(status);
    state->message = FfmpegDialogMessage(status);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 600, 370);
    return saved;
}

bool ShowWhisperDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Whisper;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    RefreshWhisperDialogText(state);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 600, 370);
    return saved;
}

bool ShowVotCliDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::VotCli;
    state->instance = instance;
    state->owner = owner;
    state->paths = paths.root().empty() ? nullptr : &paths;
    state->config = &config;
    state->workingConfig = config;
    RefreshVotCliDialogText(state);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 600, 370);
    return saved;
}

bool ShowFfmpegInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Установка FFmpeg";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowWhisperInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config, WhisperBackend backend) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::WhisperInstall;
    state->whisperBackend = backend;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Установка whisper.cpp " + WhisperManager::BackendDisplayName(backend);
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowVotCliInstallProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::VotCliInstall;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Установка vot-cli";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowWhisperModelDownloadProgress(
    HWND owner,
    HINSTANCE instance,
    const AppPaths& paths,
    AppConfig& config,
    const WhisperModelInfo& model
) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::WhisperModelDownload;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Скачивание модели Whisper";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->config = &config;
    state->whisperModel = model;
    state->progressTotal = model.sizeBytes;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 560, 270);
    return saved;
}

bool ShowWhisperModelsDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config) {
    auto* state = new DialogState{};
    state->type = DialogType::WhisperModels;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Модели Whisper";
    state->paths = &paths;
    state->config = &config;
    state->workingConfig = config;
    bool saved = false;
    state->savedResult = &saved;
    ShowModal(state, 640, 740);
    return saved;
}

bool ShowAppUpdateProgress(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release) {
    auto* state = new DialogState{};
    state->type = DialogType::Progress;
    state->progressMode = ProgressMode::AppUpdate;
    state->instance = instance;
    state->owner = owner;
    state->title = L"Обновление приложения";
    state->message = L"Подготовка...";
    state->paths = &paths;
    state->release = release;
    state->cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool updated = false;
    state->savedResult = &updated;
    ShowModal(state, 560, 270);
    return updated;
}

bool OfferAppUpdate(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release, bool notifyWhenCurrent) {
    if (!release.found) {
        if (notifyWhenCurrent) {
            ShowInfoDialog(owner, instance, L"Обновления", L"В последнем релизе не найден файл YoutubeDownloader.exe.");
        }
        return false;
    }

    if (!ShouldInstallAppUpdate(release)) {
        if (notifyWhenCurrent) {
            ShowInfoDialog(
                owner,
                instance,
                L"Обновления",
                L"Установлена актуальная версия: " YTD_APP_VERSION_WIDE
            );
        }
        return false;
    }

    if (!ShowConfirmationDialog(
            owner,
            instance,
            L"Обновление доступно",
            BuildAppUpdatePromptMessage(release)
        )) {
        return false;
    }

    return ShowAppUpdateProgress(owner, instance, paths, release);
}

bool CheckAndOfferAppUpdate(HWND owner, HINSTANCE instance, const AppPaths& paths, bool notifyWhenCurrent) {
    try {
        const ReleaseAssetInfo release = AppUpdateService::CheckLatestRelease();
        return OfferAppUpdate(owner, instance, paths, release, notifyWhenCurrent);
    } catch (const std::exception& ex) {
        if (notifyWhenCurrent) {
            ShowErrorDialog(
                owner,
                instance,
                L"Проверка обновлений не удалась",
                std::wstring(ex.what(), ex.what() + std::strlen(ex.what()))
            );
        }
    } catch (...) {
        if (notifyWhenCurrent) {
            ShowErrorDialog(owner, instance, L"Проверка обновлений не удалась", L"Неизвестная ошибка.");
        }
    }
    return false;
}

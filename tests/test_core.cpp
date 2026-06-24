#include "AppPaths.h"
#include "AppVersion.h"
#include "AsyncWait.h"
#include "BackendText.h"
#include "Config.h"
#include "DownloadQueue.h"
#include "DownloadQueueStore.h"
#include "FileOperations.h"
#include "KeyboardShortcuts.h"
#include "Logger.h"
#include "ProcessRunner.h"
#include "TranscriptionClient.h"
#include "ToolManagers.h"
#include "UiActions.h"
#include "Version.h"
#include "VoiceOverTranslationClient.h"
#include "YtDlpClient.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

fs::path MakeTempRoot(const std::wstring& name) {
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / name;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    Require(!ec, "failed to create temp root");
    return root;
}

void TestAppPaths() {
    const fs::path root = fs::path(L"C:/Portable/YoutubeDownloader");
    const AppPaths paths(root);

    Require(paths.root() == root, "root path mismatch");
    Require(paths.stuffDir() == root / L"stuff", "stuff path mismatch");
    Require(paths.configPath() == root / L"stuff" / L"config.ini", "config path mismatch");
    Require(paths.logPath() == root / L"stuff" / L"ytdl.log", "log path mismatch");
    Require(paths.downloadQueuePath() == root / L"stuff" / L"download_queue.json", "download queue path mismatch");
    Require(paths.thumbCacheDir() == root / L"stuff" / L"thumb_cache", "thumb cache path mismatch");
    Require(paths.toolsDir() == root / L"tools", "tools path mismatch");
    Require(paths.ytDlpDir() == root / L"tools" / L"yt-dlp", "yt-dlp dir path mismatch");
    Require(paths.ytDlpExePath() == root / L"tools" / L"yt-dlp" / L"yt-dlp.exe", "yt-dlp path mismatch");
    Require(paths.ytDlpVersionPath() == root / L"tools" / L"yt-dlp" / L"version.txt", "yt-dlp version path mismatch");
    Require(paths.localFfmpegBinDir() == root / L"tools" / L"ffmpeg" / L"bin", "ffmpeg bin path mismatch");
    Require(paths.localFfmpegExePath() == root / L"tools" / L"ffmpeg" / L"bin" / L"ffmpeg.exe", "ffmpeg path mismatch");
    Require(paths.localFfprobeExePath() == root / L"tools" / L"ffmpeg" / L"bin" / L"ffprobe.exe", "ffprobe path mismatch");
    Require(paths.localWhisperDir() == root / L"tools" / L"whisper", "whisper dir path mismatch");
    Require(paths.localWhisperExePath() == root / L"tools" / L"whisper" / L"whisper-cli.exe", "whisper exe path mismatch");
    Require(paths.localWhisperCpuDir() == root / L"tools" / L"whisper" / L"cpu", "whisper CPU dir path mismatch");
    Require(paths.localWhisperCudaDir() == root / L"tools" / L"whisper" / L"cuda", "whisper CUDA dir path mismatch");
    Require(paths.localWhisperCpuExePath() == root / L"tools" / L"whisper" / L"cpu" / L"whisper-cli.exe", "whisper CPU exe path mismatch");
    Require(paths.localWhisperCudaExePath() == root / L"tools" / L"whisper" / L"cuda" / L"whisper-cli.exe", "whisper CUDA exe path mismatch");
    Require(paths.localWhisperModelsDir() == root / L"tools" / L"whisper" / L"models", "whisper models path mismatch");
    Require(paths.localWhisperModelPath() == root / L"tools" / L"whisper" / L"models" / L"ggml-large-v3-turbo.bin", "whisper model path mismatch");
    Require(paths.transcriptionTempDir() == root / L"stuff" / L"transcription_tmp", "transcription temp path mismatch");
    Require(paths.voiceOverTempDir() == root / L"stuff" / L"voiceover_tmp", "voice-over temp path mismatch");
}

void TestConfigDefaultsAndRoundTrip() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_Config");
    const AppPaths paths(root);
    const AppConfig defaults = ConfigStore::Load(paths);

    Require(fs::is_regular_file(paths.configPath()), "default config should be created on first load");
    Require(defaults.quality == L"max", "default quality mismatch");
    Require(defaults.container == L"auto", "default container mismatch");
    Require(defaults.maxParallelDownloads == 3, "default max parallel mismatch");
    Require(defaults.autoUpdateApp == true, "default app auto update mismatch");
    Require(defaults.transcribeAfterDownload == false, "default transcription flag mismatch");
    Require(defaults.whisperBackend == WhisperBackend::Auto, "default whisper backend mismatch");
    Require(defaults.whisperLanguage == L"auto", "default whisper language mismatch");
    Require(defaults.voiceOverLanguage == L"ru", "default voice-over language mismatch");
    Require(defaults.voiceOverMode == L"separate", "default voice-over mode mismatch");
    Require(defaults.votCliPath.empty(), "default vot-cli path should be empty");
    Require(defaults.originalVolumePercent == 25, "default original volume mismatch");
    Require(!defaults.downloadDir.empty(), "default download dir is empty");

    AppConfig saved = defaults;
    saved.downloadDir = root / L"Downloads";
    saved.cookiesPath = root / L"cookies.txt";
    saved.ffmpegPath = root / L"ffmpeg.exe";
    saved.transcribeAfterDownload = true;
    saved.whisperPath = root / L"whisper" / L"whisper-cli.exe";
    saved.whisperModelPath = root / L"models" / L"ggml-small.bin";
    saved.whisperBackend = WhisperBackend::Cuda;
    saved.whisperLanguage = L"ru";
    saved.votCliPath = root / L"node" / L"vot-cli.cmd";
    saved.voiceOverLanguage = L"en";
    saved.voiceOverMode = L"mixed";
    saved.originalVolumePercent = 40;
    saved.quality = L"720p";
    saved.container = L"mp4";
    saved.maxParallelDownloads = 5;
    saved.autoUpdateApp = true;
    saved.lastYtDlpCheckAt = L"2026-06-17T20:00:00Z";
    saved.lastYtDlpVersion = L"2026.06.09";

    ConfigStore::Save(paths, saved);
    Require(fs::is_regular_file(paths.configPath()), "config file was not written");

    const AppConfig loaded = ConfigStore::Load(paths);
    Require(loaded.downloadDir == saved.downloadDir, "download dir round-trip mismatch");
    Require(loaded.cookiesPath == saved.cookiesPath, "cookies path round-trip mismatch");
    Require(loaded.ffmpegPath == saved.ffmpegPath, "ffmpeg path round-trip mismatch");
    Require(loaded.transcribeAfterDownload == true, "transcribe flag round-trip mismatch");
    Require(loaded.whisperPath == saved.whisperPath, "whisper path round-trip mismatch");
    Require(loaded.whisperModelPath == saved.whisperModelPath, "whisper model path round-trip mismatch");
    Require(loaded.whisperBackend == WhisperBackend::Cuda, "whisper backend round-trip mismatch");
    Require(loaded.whisperLanguage == L"ru", "whisper language round-trip mismatch");
    Require(loaded.votCliPath == saved.votCliPath, "vot-cli path round-trip mismatch");
    Require(loaded.voiceOverLanguage == L"en", "voice-over language round-trip mismatch");
    Require(loaded.voiceOverMode == L"mixed", "voice-over mode round-trip mismatch");
    Require(loaded.originalVolumePercent == 40, "original volume round-trip mismatch");
    Require(loaded.quality == L"720p", "quality round-trip mismatch");
    Require(loaded.container == L"mp4", "container round-trip mismatch");
    Require(loaded.maxParallelDownloads == 5, "max parallel round-trip mismatch");
    Require(loaded.autoUpdateApp == true, "auto update round-trip mismatch");
    Require(loaded.lastYtDlpCheckAt == L"2026-06-17T20:00:00Z", "yt-dlp check timestamp mismatch");
    Require(loaded.lastYtDlpVersion == L"2026.06.09", "yt-dlp version mismatch");
}

void TestConfigDropsLegacyFfmpegPromptFlag() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_LegacyFfmpegPromptFlag");
    const AppPaths paths(root);

    fs::create_directories(paths.configPath().parent_path());
    {
        std::ofstream out(paths.configPath(), std::ios::binary | std::ios::trunc);
        out << R"json({"ffmpeg_prompt_dismissed":true,"quality":"720p"})json";
    }

    const AppConfig loaded = ConfigStore::Load(paths);
    Require(loaded.quality == L"720p", "quality should load alongside the legacy ffmpeg prompt flag");

    ConfigStore::Save(paths, loaded);
    std::ifstream savedConfig(paths.configPath(), std::ios::binary);
    const std::string savedText{
        std::istreambuf_iterator<char>(savedConfig),
        std::istreambuf_iterator<char>()
    };
    Require(
        savedText.find("ffmpeg_prompt_dismissed") == std::string::npos,
        "saved config should drop the legacy ffmpeg prompt flag"
    );
}

void TestMainWindowShortcutResolution() {
    Require(
        ResolveMainWindowShortcut(true, 'V') == MainWindowShortcutAction::PasteUrl,
        "Ctrl+V should paste into the URL field"
    );
    Require(
        ResolveMainWindowShortcut(true, 'v') == MainWindowShortcutAction::PasteUrl,
        "Ctrl+v should paste into the URL field"
    );
    Require(
        ResolveMainWindowShortcut(false, '\r') == MainWindowShortcutAction::Download,
        "Enter should start download"
    );
    Require(
        ResolveMainWindowShortcut(false, 'V') == MainWindowShortcutAction::None,
        "V without Ctrl should not trigger paste"
    );
}

void TestDownloadAttemptResolution() {
    Require(
        ResolveDownloadAttempt(false, false) == DownloadAttemptAction::ShowYtDlpNotReady,
        "download should show a readiness message when yt-dlp is unavailable"
    );
    Require(
        ResolveDownloadAttempt(true, true) == DownloadAttemptAction::ShowPreviewLoading,
        "download should wait while preview metadata is loading"
    );
    Require(
        ResolveDownloadAttempt(true, false) == DownloadAttemptAction::Enqueue,
        "download should enqueue when yt-dlp is ready"
    );
}

void TestWhisperUtilityStatusText() {
    const fs::path executable = L"C:/Tools/whisper/whisper-cli.exe";
    const fs::path model = L"C:/Tools/whisper/models/ggml-large-v3-turbo.bin";

    const WhisperUtilityStatusText found = BuildWhisperUtilityStatusText(true, executable, true, model);
    Require(found.executableText == L"Найден: " + executable.wstring(), "found whisper executable text mismatch");
    Require(found.modelText == L"Модель: " + model.wstring(), "found whisper model text mismatch");

    const WhisperUtilityStatusText missing = BuildWhisperUtilityStatusText(false, {}, false, model);
    Require(missing.executableText == L"Не найден", "missing whisper executable text mismatch");
    Require(missing.modelText == L"Модель не скачана", "missing whisper model text mismatch");
}

void TestEditContextMenuModel() {
    const std::vector<EditContextMenuItem> emptyItems = BuildEditContextMenuItems(false, false, false, false);
    Require(emptyItems.size() == 8, "edit menu should keep a stable command layout");
    Require(emptyItems[0].id == IdEditMenuUndo && !emptyItems[0].enabled, "undo should be disabled without undo state");
    Require(emptyItems[2].id == IdEditMenuCut && !emptyItems[2].enabled, "cut should be disabled without selection");
    Require(emptyItems[4].id == IdEditMenuPaste && !emptyItems[4].enabled, "paste should be disabled without clipboard text");
    Require(emptyItems[7].id == IdEditMenuSelectAll && !emptyItems[7].enabled, "select all should be disabled without text");
    Require(EditContextMenuHeight(emptyItems) == 228, "edit menu height mismatch");
    Require(HitTestEditContextMenuItem(emptyItems, 2 + 34 + 10 + 1) == 0, "disabled cut command should not hit");

    const std::vector<EditContextMenuItem> activeItems = BuildEditContextMenuItems(true, true, true, true);
    Require(activeItems[0].enabled, "undo should be enabled when edit can undo");
    Require(activeItems[2].enabled, "cut should be enabled when text is selected");
    Require(activeItems[3].enabled, "copy should be enabled when text is selected");
    Require(activeItems[4].enabled, "paste should be enabled when clipboard has text");
    Require(activeItems[7].enabled, "select all should be enabled when edit has text");
    Require(HitTestEditContextMenuItem(activeItems, 2 + 34 + 10 + 1) == IdEditMenuCut, "active cut command hit mismatch");
    Require(HitTestEditContextMenuItem(activeItems, EditContextMenuHeight(activeItems) + 5) == 0, "outside menu should not hit");
}

struct PasteEditSubclassState {
    WNDPROC originalWindowProc = nullptr;
    const wchar_t* replacementText = nullptr;
    bool pasteReceived = false;
};

LRESULT CALLBACK PasteEditSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<PasteEditSubclassState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state != nullptr && message == WM_PASTE) {
        state->pasteReceived = true;
        SendMessageW(
            window,
            EM_REPLACESEL,
            TRUE,
            reinterpret_cast<LPARAM>(state->replacementText)
        );
        return 0;
    }

    if (state != nullptr && state->originalWindowProc != nullptr) {
        return CallWindowProcW(state->originalWindowProc, window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void TestPasteReplacesExistingEditText() {
    const std::wstring existingUrl = L"https://example.invalid/old";
    const std::wstring replacementUrl = L"https://www.youtube.com/watch?v=unicode_тест";

    HWND edit = CreateWindowExW(
        0,
        L"EDIT",
        existingUrl.c_str(),
        WS_POPUP | ES_AUTOHSCROLL,
        0,
        0,
        320,
        24,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    Require(edit != nullptr, "failed to create hidden edit control");

    PasteEditSubclassState state;
    state.replacementText = replacementUrl.c_str();
    SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    state.originalWindowProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        edit,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(PasteEditSubclassProc)
    ));
    const bool subclassInstalled = state.originalWindowProc != nullptr;
    std::wstring pastedText;
    bool editFocused = false;
    if (subclassInstalled) {
        PasteReplacingEditText(edit);

        const int textLength = GetWindowTextLengthW(edit);
        std::vector<wchar_t> textBuffer(static_cast<std::size_t>(textLength) + 1);
        GetWindowTextW(edit, textBuffer.data(), static_cast<int>(textBuffer.size()));
        pastedText = textBuffer.data();
        editFocused = GetFocus() == edit;

        SetWindowLongPtrW(edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(state.originalWindowProc));
    }
    SetWindowLongPtrW(edit, GWLP_USERDATA, 0);
    DestroyWindow(edit);

    Require(subclassInstalled, "failed to subclass hidden edit control");
    Require(state.pasteReceived, "paste action should send WM_PASTE to the edit control");
    Require(editFocused, "paste should focus the edit control");
    Require(pastedText == replacementUrl, "paste should replace the complete edit text");
}

void TestPasteReplacingEditTextIgnoresInvalidHandles() {
    PasteReplacingEditText(nullptr);
    PasteReplacingEditText(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1)));
}

void TestModalOwnerRestorationPreservesEnabledState() {
    HWND owner = CreateWindowExW(
        0,
        L"STATIC",
        L"Hidden owner",
        WS_OVERLAPPEDWINDOW,
        -32000,
        -32000,
        320,
        200,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    Require(owner != nullptr, "failed to create hidden owner window");

    EnableWindow(owner, FALSE);
    RestoreModalOwner(owner, false);
    Require(IsWindowEnabled(owner) == FALSE, "owner should stay disabled when it was not previously enabled");

    ShowWindow(owner, SW_SHOWMINNOACTIVE);
    const bool wasIconic = IsIconic(owner) != FALSE;
    RestoreModalOwner(owner, true);
    const bool ownerEnabled = IsWindowEnabled(owner) != FALSE;
    const bool ownerRestored = IsIconic(owner) == FALSE;
    const bool ownerActivated = GetActiveWindow() == owner;

    ShowWindow(owner, SW_HIDE);
    DestroyWindow(owner);

    Require(wasIconic, "owner fixture should start minimized");
    Require(ownerEnabled, "owner should be re-enabled when it was previously enabled");
    Require(ownerRestored, "iconic owner should be restored");
    Require(ownerActivated, "restored owner should become the active window on its creating thread");
}

void TestRestoreModalOwnerIgnoresInvalidHandles() {
    RestoreModalOwner(nullptr, true);
    RestoreModalOwner(reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1)), true);
}

void TestConfigParallelDownloadBounds() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_ConfigBounds");
    const AppPaths paths(root);

    {
        fs::create_directories(paths.configPath().parent_path());
        std::ofstream out(paths.configPath());
        out << R"json({"max_parallel_downloads": 1})json";
    }
    Require(ConfigStore::Load(paths).maxParallelDownloads == 3, "parallel downloads should clamp to minimum 3");

    {
        std::ofstream out(paths.configPath(), std::ios::trunc);
        out << R"json({"max_parallel_downloads": 99})json";
    }
    Require(ConfigStore::Load(paths).maxParallelDownloads == 10, "parallel downloads should clamp to maximum 10");
}

void TestConfigUtf8RoundTrip() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_ConfigUtf8");
    const AppPaths paths(root);

    AppConfig saved = ConfigStore::Load(paths);
    saved.downloadDir = root / L"Загрузки";
    saved.cookiesPath = root / L"куки.txt";
    saved.ffmpegPath = root / L"инструменты" / L"ffmpeg.exe";
    saved.quality = L"1080p";
    saved.container = L"mkv";
    saved.lastYtDlpVersion = L"2026.06.17";

    ConfigStore::Save(paths, saved);
    const AppConfig loaded = ConfigStore::Load(paths);

    Require(loaded.downloadDir == saved.downloadDir, "utf8 download dir round-trip mismatch");
    Require(loaded.cookiesPath == saved.cookiesPath, "utf8 cookies path round-trip mismatch");
    Require(loaded.ffmpegPath == saved.ffmpegPath, "utf8 ffmpeg path round-trip mismatch");
    Require(loaded.quality == L"1080p", "utf8 quality round-trip mismatch");
    Require(loaded.container == L"mkv", "utf8 container round-trip mismatch");
    Require(loaded.lastYtDlpVersion == L"2026.06.17", "utf8 version round-trip mismatch");
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(2)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

void TestLoggerTruncatesAtStartupAndAppendsWithinRun() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_Logger");
    const AppPaths paths(root);
    fs::create_directories(paths.stuffDir());
    {
        std::ofstream old(paths.logPath(), std::ios::binary | std::ios::trunc);
        old << "old-run";
    }

    Logger logger(paths);
    {
        std::ifstream truncated(paths.logPath(), std::ios::binary);
        const std::string text{
            std::istreambuf_iterator<char>(truncated),
            std::istreambuf_iterator<char>()
        };
        Require(text.empty(), "logger should truncate the previous run");
    }

    logger.Info(L"startup");
    logger.Error(L"failure");

    std::ifstream current(paths.logPath(), std::ios::binary);
    const std::string text{
        std::istreambuf_iterator<char>(current),
        std::istreambuf_iterator<char>()
    };
    Require(text.find("old-run") == std::string::npos, "old log content should be removed");
    Require(text.find("startup") != std::string::npos, "startup log entry missing");
    Require(text.find("failure") != std::string::npos, "error log entry missing");
}

void TestLoggerReadsCurrentLogText() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_LoggerRead");
    const AppPaths paths(root);
    Logger logger(paths);

    logger.Info(L"first line");
    logger.Error(L"second line");

    const std::wstring text = logger.ReadAll();
    Require(text.find(L"first line") != std::wstring::npos, "log reader should include info entries");
    Require(text.find(L"second line") != std::wstring::npos, "log reader should include error entries");
    Require(text.find(L"[INFO]") != std::wstring::npos, "log reader should preserve levels");
    Require(text.find(L"[ERROR]") != std::wstring::npos, "log reader should preserve error levels");
}

void TestCommitDownloadedFilePreservesTargetUntilValidated() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_FileCommit");
    const fs::path target = root / L"tool.exe";
    const fs::path staged = root / L"tool.exe.new";

    auto writeText = [](const fs::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    };
    auto readText = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };

    writeText(target, "working");
    writeText(staged, "bad");
    bool rejectedShortFile = false;
    try {
        CommitDownloadedFile(staged, target, 3, 4);
    } catch (const std::exception&) {
        rejectedShortFile = true;
    }
    Require(rejectedShortFile, "short download should be rejected");
    Require(readText(target) == "working", "short download should preserve the working target");
    Require(!fs::exists(staged), "rejected staged file should be removed");

    writeText(staged, "replacement");
    CommitDownloadedFile(staged, target, 11, 11);
    Require(readText(target) == "replacement", "validated download should replace the target");
    Require(!fs::exists(staged), "committed staged file should be consumed");
}

void TestWaitForDelayCompletesOrStopsPromptly() {
    std::stop_source running;
    const auto startedAt = std::chrono::steady_clock::now();
    Require(
        WaitForDelay(running.get_token(), std::chrono::milliseconds(20)),
        "delay should complete when no stop is requested"
    );
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;
    Require(elapsed >= std::chrono::milliseconds(15), "delay returned before its timeout");

    std::stop_source stopped;
    stopped.request_stop();
    const auto canceledAt = std::chrono::steady_clock::now();
    Require(
        !WaitForDelay(stopped.get_token(), std::chrono::seconds(2)),
        "delay should report a stop request"
    );
    Require(
        std::chrono::steady_clock::now() - canceledAt < std::chrono::milliseconds(100),
        "stopped delay should return promptly"
    );
}

void TestProgressPresentation() {
    Require(FormatBytes(0).empty(), "zero bytes should format as empty text");
    Require(FormatBytes(512) == L"512 B", "byte formatting mismatch");
    Require(FormatBytes(1536) == L"1.5 KB", "kilobyte formatting mismatch");
    Require(FormatBytes(10 * 1024) == L"10 KB", "whole kilobyte formatting mismatch");
    Require(FormatBytes(1024 * 1024) == L"1.0 MB", "megabyte formatting mismatch");

    Require(FormatProgressBytes(1024, 2048) == L"1.0 KB / 2.0 KB", "known-total progress text mismatch");
    Require(FormatProgressBytes(0, 2048) == L"0 B / 2.0 KB", "zero-downloaded progress text mismatch");
    Require(FormatProgressBytes(1024, 0) == L"1.0 KB", "downloaded-only progress text mismatch");
    Require(FormatProgressBytes(0, 0).empty(), "unknown progress sizes should be hidden");

    Require(FormatDuration(0).empty(), "zero duration should be hidden");
    Require(FormatDuration(7) == L"7 с", "seconds duration mismatch");
    Require(FormatDuration(75) == L"1 мин 15 с", "minute duration mismatch");
    Require(FormatDuration(3600) == L"1 ч", "hour duration mismatch");
    Require(FormatDuration(9000) == L"2 ч 30 мин", "large second count should become hours and minutes");
    Require(FormatDuration(172861) == L"2 д 1 мин", "multi-day duration mismatch");

    Require(FormatElapsedDuration(1) == L"1 секунда", "single elapsed second text mismatch");
    Require(FormatElapsedDuration(2) == L"2 секунды", "few elapsed seconds text mismatch");
    Require(FormatElapsedDuration(7) == L"7 секунд", "many elapsed seconds text mismatch");
    Require(FormatElapsedDuration(62) == L"1 минута 2 секунды", "elapsed minute text mismatch");
    Require(FormatElapsedDuration(125) == L"2 минуты 5 секунд", "elapsed minutes text mismatch");

    Require(CalculateProgressPercent(0, 0) == 0, "unknown-total percent should be zero");
    Require(CalculateProgressPercent(50, 100) == 50, "half progress percent mismatch");
    Require(CalculateProgressPercent(200, 100) == 100, "progress percent should clamp to 100");
}

void TestVersionCompare() {
    Require(CompareVersions(L"2026.06.09", L"2026.06.10") < 0, "date version compare mismatch");
    Require(CompareVersions(L"1.2.0", L"1.2") == 0, "missing patch compare mismatch");
    Require(CompareVersions(L"v1.10.0", L"1.2.99") > 0, "numeric compare mismatch");
    Require(CompareVersions(L"2025.01.01", L"2025.01.01") == 0, "equal compare mismatch");
}

bool ContainsArg(const std::vector<std::wstring>& args, const std::wstring& value) {
    return std::ranges::find(args, value) != args.end();
}

size_t ArgIndex(const std::vector<std::wstring>& args, const std::wstring& value) {
    const auto it = std::ranges::find(args, value);
    Require(it != args.end(), "argument not found");
    return static_cast<size_t>(std::distance(args.begin(), it));
}

void TestYtDlpDownloadArguments() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_YtDlpArgs");
    const fs::path cookies = root / L"cookies.txt";
    {
        std::ofstream out(cookies);
        out << "# Netscape HTTP Cookie File\n";
    }

    YtDlpDownloadRequest request;
    request.url = L"https://www.youtube.com/watch?v=test";
    request.outputDirectory = root / L"Downloads";
    request.quality = L"720p";
    request.container = L"mp4";
    request.cookiesPath = cookies;
    request.ffmpegExePath = root / L"tools" / L"ffmpeg" / L"bin" / L"ffmpeg.exe";
    request.ffmpegAvailable = true;

    const std::vector<std::wstring> args = BuildDownloadArguments(request);

    Require(ContainsArg(args, L"--cookies"), "cookies argument missing");
    Require(args.at(ArgIndex(args, L"--cookies") + 1) == cookies.wstring(), "cookies path argument mismatch");
    Require(ContainsArg(args, L"--ffmpeg-location"), "ffmpeg location argument missing");
    Require(args.at(ArgIndex(args, L"--ffmpeg-location") + 1) == request.ffmpegExePath.parent_path().wstring(), "ffmpeg location mismatch");
    Require(ContainsArg(args, L"--merge-output-format"), "merge output format missing");
    Require(args.at(ArgIndex(args, L"--merge-output-format") + 1) == L"mp4", "merge output format value mismatch");
    Require(!ContainsArg(args, L"--no-simulate"), "download arguments should keep the pre-whisper progress behavior");
    Require(!ContainsArg(args, L"--print"), "download arguments should not add print output during downloads");
    Require(ContainsArg(args, L"--progress-template"), "progress template missing");
    Require(ContainsArg(args, L"--output"), "output template missing");
    Require(args.at(ArgIndex(args, L"--output") + 1).find(L"%(title).200s [%(id)s].%(ext)s") != std::wstring::npos, "output template mismatch");
    Require(args.at(ArgIndex(args, L"--format") + 1).find(L"height<=720") != std::wstring::npos, "720p format mismatch");
    Require(args.back() == request.url, "url should be last argument");

    request.quality = L"audio";
    request.container = L"auto";
    request.cookiesPath.clear();
    const std::vector<std::wstring> audioArgs = BuildDownloadArguments(request);
    Require(args.at(ArgIndex(args, L"--format") + 1) != audioArgs.at(ArgIndex(audioArgs, L"--format") + 1), "audio format did not change");
    Require(audioArgs.at(ArgIndex(audioArgs, L"--format") + 1) == L"bestaudio/best", "audio format mismatch");
    Require(!ContainsArg(audioArgs, L"--cookies"), "cookies should be omitted when path is empty");
    Require(!ContainsArg(audioArgs, L"--merge-output-format"), "auto container should not force remux");
}

void TestYtDlpOutputPathParsing() {
    const fs::path direct = L"C:/Downloads/Video [abc].mp4";
    Require(
        ExtractYtDlpOutputPath(L"[download] Destination: " + direct.wstring()) == direct,
        "download destination output path mismatch"
    );

    const fs::path merged = L"C:/Downloads/Video [abc].mkv";
    Require(
        ExtractYtDlpOutputPath(L"[Merger] Merging formats into \"" + merged.wstring() + L"\"") == merged,
        "merger output path mismatch"
    );

    Require(
        ExtractYtDlpOutputPath(L"\r  [download] Destination: " + direct.wstring()) == direct,
        "indented destination output path mismatch"
    );
    Require(
        ExtractYtDlpOutputPath(L"__YTDLP_OUTPUT__:" + merged.wstring()) == merged,
        "printed after_move output path mismatch"
    );
    Require(
        ExtractYtDlpOutputPath(L"[download] " + direct.wstring() + L" has already been downloaded") == direct,
        "already downloaded output path mismatch"
    );
    Require(ExtractYtDlpOutputPath(L"[download] 50% of 1MiB").empty(), "plain progress line should not produce a path");
}

void TestYtDlpOutputFileFallbackFindsNewMedia() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_OutputFallback");
    const fs::path oldVideo = root / L"old.mp4";
    {
        std::ofstream out(oldVideo);
        out << "old";
    }

    const std::vector<OutputDirectoryFile> before = SnapshotOutputDirectory(root);

    const fs::path partial = root / L"short.webm.part";
    const fs::path metadata = root / L"short.info.json";
    const fs::path newVideo = root / L"short.webm";
    {
        std::ofstream out(partial);
        out << "partial";
    }
    {
        std::ofstream out(metadata);
        out << "{}";
    }
    {
        std::ofstream out(newVideo);
        out << "new";
    }

    Require(
        FindDownloadedMediaFile({}, root, before) == newVideo,
        "fallback should find newly downloaded media when yt-dlp did not report an output path"
    );
    Require(
        FindDownloadedMediaFile({newVideo}, root, before) == newVideo,
        "reported media path should be preferred when available"
    );
}

void TestTranscriptionCommandArguments() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_TranscriptionArgs");
    const fs::path media = root / L"Video [abc].mp4";
    const fs::path wav = root / L"tmp" / L"Video [abc].wav";
    const fs::path outputBase = root / L"Video [abc]";

    const std::vector<std::wstring> ffmpegArgs = BuildFfmpegAudioExtractionArguments(media, wav);
    Require(ffmpegArgs == std::vector<std::wstring>({
        L"-y",
        L"-i",
        media.wstring(),
        L"-vn",
        L"-ac",
        L"1",
        L"-ar",
        L"16000",
        L"-c:a",
        L"pcm_s16le",
        wav.wstring()
    }), "ffmpeg audio extraction arguments mismatch");

    TranscriptionRequest request;
    request.whisperModelPath = root / L"models" / L"ggml-base.bin";
    request.language = L"auto";

    const std::vector<std::wstring> whisperArgs = BuildWhisperArguments(request, wav, outputBase);
    Require(ContainsArg(whisperArgs, L"-m"), "whisper model argument missing");
    Require(whisperArgs.at(ArgIndex(whisperArgs, L"-m") + 1) == request.whisperModelPath.wstring(), "whisper model path mismatch");
    Require(ContainsArg(whisperArgs, L"-f"), "whisper input argument missing");
    Require(whisperArgs.at(ArgIndex(whisperArgs, L"-f") + 1) == wav.wstring(), "whisper wav path mismatch");
    Require(ContainsArg(whisperArgs, L"-otxt"), "whisper txt output flag missing");
    Require(ContainsArg(whisperArgs, L"-osrt"), "whisper srt output flag missing");
    Require(ContainsArg(whisperArgs, L"-of"), "whisper output base argument missing");
    Require(whisperArgs.at(ArgIndex(whisperArgs, L"-of") + 1) == outputBase.wstring(), "whisper output base mismatch");
    Require(ContainsArg(whisperArgs, L"-l"), "whisper language argument missing");
    Require(whisperArgs.at(ArgIndex(whisperArgs, L"-l") + 1) == L"auto", "whisper language mismatch");
    Require(TranscriptOutputBaseFor(media) == outputBase, "transcript output base mismatch");
}

void TestVoiceOverCommandArguments() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_VoiceOverArgs");
    const fs::path temp = root / L"tmp";
    const fs::path media = root / L"Video [abc].mp4";
    const fs::path webmMedia = root / L"Clip [def].webm";

    VoiceOverTranslationRequest request;
    request.mediaPath = media;
    request.tempDirectory = temp;
    request.ffmpegExePath = root / L"ffmpeg.exe";
    request.votCliPath = root / L"vot-cli.cmd";
    request.youtubeUrl = L"https://www.youtube.com/watch?v=abc";
    request.language = L"ru";
    request.mode = L"separate";
    request.originalVolumePercent = 25;

    const VoiceOverTranslationPaths separatePaths = BuildVoiceOverPaths(media, temp, request.language, request.mode);
    Require(separatePaths.tempAudioPath == temp / L"Video [abc].vot.ru.mp3", "voice-over temp audio path mismatch");
    Require(separatePaths.finalAudioPath == root / L"Video [abc].vot.ru.mp3", "voice-over final audio path mismatch");
    Require(separatePaths.finalVideoPath == root / L"Video [abc].vot.ru.mp4", "voice-over mp4 output path mismatch");

    const std::vector<std::wstring> votArgs = BuildVotCliArguments(request, separatePaths.tempAudioPath);
    Require(ContainsArg(votArgs, L"--output"), "vot-cli output argument missing");
    Require(votArgs.at(ArgIndex(votArgs, L"--output") + 1) == temp.wstring(), "vot-cli output directory mismatch");
    Require(ContainsArg(votArgs, L"--output-file"), "vot-cli output-file argument missing");
    Require(votArgs.at(ArgIndex(votArgs, L"--output-file") + 1) == L"Video [abc].vot.ru.mp3", "vot-cli output filename mismatch");
    Require(ContainsArg(votArgs, L"--reslang=ru"), "vot-cli result language argument missing");
    Require(votArgs.back() == request.youtubeUrl, "vot-cli URL should be last argument");

    const VoiceOverProcessInvocation cmdInvocation = BuildVotCliInvocation(request, separatePaths.tempAudioPath);
    Require(cmdInvocation.executable.filename() == L"cmd.exe", "vot-cli .cmd should launch through cmd.exe");
    Require(ContainsArg(cmdInvocation.arguments, L"/c"), "vot-cli .cmd invocation should use cmd /c");
    Require(ContainsArg(cmdInvocation.arguments, request.votCliPath.wstring()), "vot-cli .cmd path should be passed to cmd.exe");
    Require(cmdInvocation.arguments.back() == request.youtubeUrl, "vot-cli .cmd invocation should keep URL last");

    request.votCliPath = root / L"vot-cli.exe";
    const VoiceOverProcessInvocation exeInvocation = BuildVotCliInvocation(request, separatePaths.tempAudioPath);
    Require(exeInvocation.executable == request.votCliPath, "vot-cli exe should launch directly");
    Require(exeInvocation.arguments == BuildVotCliArguments(request, separatePaths.tempAudioPath), "vot-cli exe arguments mismatch");
    request.votCliPath = root / L"vot-cli.cmd";

    const std::vector<std::wstring> muxArgs = BuildVoiceOverMuxArguments(
        media,
        separatePaths.tempAudioPath,
        separatePaths.finalVideoPath,
        request.language
    );
    Require(muxArgs == std::vector<std::wstring>({
        L"-y",
        L"-i",
        media.wstring(),
        L"-i",
        separatePaths.tempAudioPath.wstring(),
        L"-map",
        L"0:v",
        L"-map",
        L"0:a?",
        L"-map",
        L"1:a",
        L"-c:v",
        L"copy",
        L"-c:a",
        L"aac",
        L"-metadata:s:a:1",
        L"language=rus",
        L"-metadata:s:a:1",
        L"title=VOT Russian",
        separatePaths.finalVideoPath.wstring()
    }), "voice-over mux arguments mismatch");

    const VoiceOverTranslationPaths mixedPaths = BuildVoiceOverPaths(webmMedia, temp, request.language, L"mixed");
    Require(mixedPaths.finalVideoPath == root / L"Clip [def].vot-mixed.ru.mkv", "voice-over non-mp4 mixed output path mismatch");

    const std::vector<std::wstring> mixArgs = BuildVoiceOverMixArguments(
        webmMedia,
        mixedPaths.tempAudioPath,
        mixedPaths.finalVideoPath,
        request.originalVolumePercent
    );
    Require(ContainsArg(mixArgs, L"-filter_complex"), "voice-over mix filter missing");
    Require(
        mixArgs.at(ArgIndex(mixArgs, L"-filter_complex") + 1) ==
            L"[0:a]volume=0.25[orig];[orig][1:a]amix=inputs=2:duration=first[a]",
        "voice-over mix filter mismatch"
    );
    Require(ContainsArg(mixArgs, L"-map"), "voice-over mix map argument missing");
    Require(mixArgs.back() == mixedPaths.finalVideoPath.wstring(), "voice-over mix output should be last argument");
}

void TestTranscriptionUsesAsciiTemporaryWhisperPaths() {
    const fs::path root = fs::path(L"C:/Downloads");
    const fs::path media = root / L"Человек-паук： Новый день [abc].webm";
    const fs::path temp = fs::path(L"C:/Portable/YoutubeDownloader/stuff/transcription_tmp");
    const TranscriptionPaths paths = BuildTranscriptionPaths(media, temp, 42);

    Require(paths.finalTextPath == root / L"Человек-паук： Новый день [abc].txt", "final transcript txt path mismatch");
    Require(paths.finalSrtPath == root / L"Человек-паук： Новый день [abc].srt", "final transcript srt path mismatch");
    Require(paths.wavPath == temp / L"audio-42.wav", "temporary wav path should be ASCII");
    Require(paths.whisperOutputBase == temp / L"transcript-42", "temporary whisper output base should be ASCII");
    Require(paths.tempTextPath == temp / L"transcript-42.txt", "temporary txt path mismatch");
    Require(paths.tempSrtPath == temp / L"transcript-42.srt", "temporary srt path mismatch");
}

void TestWhisperProgressParsing() {
    const std::optional<double> bracketed = ParseWhisperProgressPercent(L"whisper: [ 42%] audio processed");
    Require(bracketed && *bracketed == 42.0, "bracketed whisper progress mismatch");

    const std::optional<double> named = ParseWhisperProgressPercent(L"whisper_print_progress_callback: progress = 87%");
    Require(named && *named == 87.0, "named whisper progress mismatch");

    const std::optional<double> decimal = ParseWhisperProgressPercent(L"progress = 12.5%");
    Require(decimal && *decimal == 12.5, "decimal whisper progress mismatch");

    Require(!ParseWhisperProgressPercent(L"no percentage here"), "plain whisper line should not produce progress");
    Require(ParseWhisperProgressPercent(L"progress = 140%").value_or(0.0) == 100.0, "whisper progress should be clamped");
}

void TestTranscriptionErrorSummaryUsesLastMeaningfulLine() {
    Require(
        BuildProcessErrorSummary(
            L"load_backend: loaded CPU backend\r\nwhisper: failed to open output file\r\n",
            L"",
            L"fallback"
        ) == L"whisper: failed to open output file",
        "stderr summary should use the last meaningful line"
    );
    Require(
        BuildProcessErrorSummary(L"\r\n", L"stdout first\nstdout last\n", L"fallback") == L"stdout last",
        "stdout summary should be used when stderr has no meaningful line"
    );
    Require(
        BuildProcessErrorSummary(L"", L"", L"fallback") == L"fallback",
        "fallback should be used when process output is empty"
    );
}

void TestYtDlpProgressParsing() {
    const std::wstring line =
        L"__YTDLP_PROGRESS__ status=downloading downloaded=512 total=1024 total_estimate=1024 speed=2048 eta=5 part=video vcodec=av01 acodec=none ext=mp4 format=399 height=1080";
    const YtDlpProgress progress = ParseYtDlpProgressLine(line);

    Require(progress.recognized, "progress line was not recognized");
    Require(progress.rawStatus == L"downloading", "progress status mismatch");
    Require(progress.stage == L"Скачивание видео:", "progress stage mismatch");
    Require(progress.percent == 50.0, "progress percent mismatch");
    Require(progress.speedBytesPerSecond == 2048, "progress speed mismatch");
    Require(progress.etaSeconds == 5, "progress eta mismatch");
    Require(progress.totalBytes == 1024, "progress total mismatch");
    Require(progress.mediaKind == L"video", "progress media kind mismatch");
    Require(progress.extension == L"mp4", "progress extension mismatch");
    Require(progress.formatId == L"399", "progress format id mismatch");
    Require(progress.resolution == L"1080p", "progress resolution mismatch");

    const YtDlpProgress ignored = ParseYtDlpProgressLine(L"[download] 50.0% of 1.00MiB");
    Require(!ignored.recognized, "plain yt-dlp line should not be recognized as machine progress");

    const std::wstring audioLine =
        L"__YTDLP_PROGRESS__ status=downloading downloaded=256 total=1024 total_estimate=1024 speed=1024 eta=7 part=medium vcodec=none acodec=mp4a.40.2 ext=m4a format=140";
    const YtDlpProgress audio = ParseYtDlpProgressLine(audioLine);
    Require(audio.recognized, "audio progress line was not recognized");
    Require(audio.mediaKind == L"audio", "audio media kind mismatch");
    Require(audio.stage == L"Скачивание аудио:", "audio progress stage mismatch");
    Require(audio.extension == L"m4a", "audio extension mismatch");
    Require(audio.formatId == L"140", "audio format id mismatch");

    const std::wstring finishedLine =
        L"__YTDLP_PROGRESS__ status=finished downloaded=900 total=1000 total_estimate=1000 speed=0 eta=0 part=video vcodec=av01 acodec=none ext=mp4 format=399 height=720";
    const YtDlpProgress finished = ParseYtDlpProgressLine(finishedLine);
    Require(finished.recognized, "finished progress line was not recognized");
    Require(finished.percent == 100.0, "finished video progress should be 100 percent");
    Require(finished.resolution == L"720p", "finished video resolution mismatch");
}

void TestYtDlpProcessLineParsing() {
    const YtDlpProcessLine progress = ParseYtDlpProcessLine(
        L"__YTDLP_PROGRESS__ status=downloading downloaded=256 total=1024 total_estimate=1024 speed=1024 eta=7 part=medium vcodec=none acodec=mp4a.40.2 ext=m4a format=140 height=NA"
    );
    Require(progress.progress.recognized, "process line should expose yt-dlp progress");
    Require(progress.progress.mediaKind == L"audio", "process line progress should keep audio media kind");
    Require(progress.outputPath.empty(), "progress-only line should not expose an output path");

    const YtDlpProcessLine output = ParseYtDlpProcessLine(L"__YTDLP_OUTPUT__:C:\\Downloads\\video.webm");
    Require(!output.progress.recognized, "output-only line should not expose progress");
    Require(output.outputPath == fs::path(L"C:\\Downloads\\video.webm"), "process line output path mismatch");

    const YtDlpProcessLine carriageReturnProgress = ParseYtDlpProcessLine(
        L"\r __YTDLP_PROGRESS__ status=downloading downloaded=512 total=1024 total_estimate=1024 speed=2048 eta=5 part=video vcodec=av01 acodec=none ext=mp4 format=399 height=1080"
    );
    Require(carriageReturnProgress.progress.recognized, "progress with leading carriage return should be recognized");
    Require(carriageReturnProgress.progress.mediaKind == L"video", "trimmed progress media kind mismatch");
}

void TestGitHubReleaseParsing() {
    const std::string ytDlpRelease = R"json(
{
  "tag_name": "2026.06.09",
  "html_url": "https://github.com/yt-dlp/yt-dlp/releases/tag/2026.06.09",
  "assets": [
    {"name": "SHA2-256SUMS", "browser_download_url": "https://example.invalid/sums"},
    {"name": "yt-dlp.exe", "browser_download_url": "https://example.invalid/yt-dlp.exe"}
  ]
}
)json";

    const ReleaseAssetInfo ytDlp = ParseGitHubReleaseAsset(ytDlpRelease, "yt-dlp.exe");
    Require(ytDlp.found, "yt-dlp asset not found");
    Require(ytDlp.version == L"2026.06.09", "yt-dlp release version mismatch");
    Require(ytDlp.downloadUrl == L"https://example.invalid/yt-dlp.exe", "yt-dlp asset url mismatch");

    const std::string appRelease = R"json(
{
  "tag_name": "v1.0.4",
  "html_url": "https://github.com/Laynholt/YoutubeDownloader/releases/tag/v1.0.4",
  "assets": [
    {"name": "YoutubeDownloader.exe", "browser_download_url": "https://example.invalid/YoutubeDownloader.exe"}
  ]
}
)json";

    const ReleaseAssetInfo app = ParseGitHubReleaseAsset(appRelease, "YoutubeDownloader.exe");
    Require(app.found, "app update asset not found");
    Require(app.version == L"1.0.4", "app release version should be normalized");
    Require(app.downloadUrl == L"https://example.invalid/YoutubeDownloader.exe", "app asset url mismatch");

    const ReleaseAssetInfo missing = ParseGitHubReleaseAsset(appRelease, "missing.exe");
    Require(!missing.found, "missing asset should not be found");
}

void TestWhisperReleaseParsing() {
    const std::string release = R"json(
{
  "tag_name": "v1.9.1",
  "html_url": "https://github.com/ggml-org/whisper.cpp/releases/tag/v1.9.1",
  "assets": [
    {
      "name": "whisper-bin-x64.zip",
      "browser_download_url": "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/whisper-bin-x64.zip"
    },
    {
      "name": "whisper-cublas-12.4.0-bin-x64.zip",
      "browser_download_url": "https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/whisper-cublas-12.4.0-bin-x64.zip"
    }
  ]
}
)json";

    const ReleaseAssetInfo whisper = ParseGitHubReleaseAsset(release, WhisperManager::WindowsCpuAssetName());
    Require(whisper.found, "whisper release asset should be found");
    Require(whisper.version == L"1.9.1", "whisper release version mismatch");
    Require(
        whisper.downloadUrl == L"https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/whisper-bin-x64.zip",
        "whisper release download URL mismatch"
    );

    const ReleaseAssetInfo cuda = ParseGitHubReleaseAsset(release, WhisperManager::WindowsCudaAssetName());
    Require(cuda.found, "whisper CUDA release asset should be found");
    Require(cuda.version == L"1.9.1", "whisper CUDA release version mismatch");
    Require(
        cuda.downloadUrl == L"https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.1/whisper-cublas-12.4.0-bin-x64.zip",
        "whisper CUDA release download URL mismatch"
    );

    Require(
        std::string(WhisperManager::BackendAssetName(WhisperBackend::Cpu)) == "whisper-bin-x64.zip",
        "CPU backend asset mismatch"
    );
    Require(
        std::string(WhisperManager::BackendAssetName(WhisperBackend::Cuda)) == "whisper-cublas-12.4.0-bin-x64.zip",
        "CUDA backend asset mismatch"
    );
}

void TestWhisperBackendResolution() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_WhisperBackendResolution");
    const AppPaths paths(root);
    AppConfig config;

    ToolInstallStatus missing = WhisperManager::Resolve(paths, config);
    Require(!missing.installed, "missing whisper backend should not resolve");

    fs::create_directories(paths.localWhisperCudaExePath().parent_path());
    {
        std::ofstream out(paths.localWhisperCudaExePath());
        out << "cuda";
    }

    ToolInstallStatus cudaOnly = WhisperManager::Resolve(paths, config);
    Require(cudaOnly.installed, "single CUDA backend should resolve");
    Require(cudaOnly.executable == paths.localWhisperCudaExePath(), "single CUDA backend path mismatch");
    Require(cudaOnly.whisperBackend == WhisperBackend::Cuda, "single CUDA backend type mismatch");

    fs::create_directories(paths.localWhisperCpuExePath().parent_path());
    {
        std::ofstream out(paths.localWhisperCpuExePath());
        out << "cpu";
    }

    config.whisperBackend = WhisperBackend::Cpu;
    ToolInstallStatus selectedCpu = WhisperManager::Resolve(paths, config);
    Require(selectedCpu.installed, "selected CPU backend should resolve");
    Require(selectedCpu.executable == paths.localWhisperCpuExePath(), "selected CPU backend path mismatch");
    Require(selectedCpu.whisperBackend == WhisperBackend::Cpu, "selected CPU backend type mismatch");

    config.whisperBackend = WhisperBackend::Cuda;
    ToolInstallStatus selectedCuda = WhisperManager::Resolve(paths, config);
    Require(selectedCuda.installed, "selected CUDA backend should resolve");
    Require(selectedCuda.executable == paths.localWhisperCudaExePath(), "selected CUDA backend path mismatch");
    Require(selectedCuda.whisperBackend == WhisperBackend::Cuda, "selected CUDA backend type mismatch");

    std::error_code ec;
    fs::remove(paths.localWhisperCudaExePath(), ec);
    ToolInstallStatus fallbackCpu = WhisperManager::Resolve(paths, config);
    Require(fallbackCpu.installed, "missing selected backend should fall back to the installed backend");
    Require(fallbackCpu.executable == paths.localWhisperCpuExePath(), "fallback CPU backend path mismatch");
    Require(fallbackCpu.whisperBackend == WhisperBackend::Cpu, "fallback CPU backend type mismatch");

    config.whisperPath = root / L"custom" / L"whisper-cli.exe";
    fs::create_directories(config.whisperPath.parent_path());
    {
        std::ofstream out(config.whisperPath);
        out << "custom";
    }

    ToolInstallStatus custom = WhisperManager::Resolve(paths, config);
    Require(custom.installed, "custom whisper path should resolve");
    Require(custom.executable == config.whisperPath, "custom whisper path mismatch");
    Require(custom.whisperBackend == WhisperBackend::Custom, "custom whisper backend type mismatch");
}

void TestWhisperResolutionHonorsLegacyCpuSelectionAlongsideCuda() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_WhisperLegacyCpuSelection");
    const AppPaths paths(root);

    fs::create_directories(paths.localWhisperExePath().parent_path());
    {
        std::ofstream out(paths.localWhisperExePath());
        out << "legacy cpu";
    }
    fs::create_directories(paths.localWhisperCudaExePath().parent_path());
    {
        std::ofstream out(paths.localWhisperCudaExePath());
        out << "cuda";
    }

    AppConfig config;
    config.whisperBackend = WhisperBackend::Cpu;
    const ToolInstallStatus selectedCpu = WhisperManager::Resolve(paths, config);
    Require(selectedCpu.installed, "legacy CPU should resolve when CPU is selected");
    Require(selectedCpu.executable == paths.localWhisperExePath(), "legacy CPU selected path mismatch");
    Require(selectedCpu.whisperBackend == WhisperBackend::Cpu, "legacy CPU selected backend mismatch");
}

void TestWhisperModelCatalog() {
    const std::vector<WhisperModelInfo> models = WhisperManager::ModelCatalog();
    Require(models.size() >= 8, "whisper model catalog should include common and quantized models");

    const auto turbo = std::ranges::find_if(models, [](const WhisperModelInfo& model) {
        return model.id == L"large-v3-turbo";
    });
    Require(turbo != models.end(), "large-v3-turbo model missing");
    Require(turbo->recommended, "large-v3-turbo should be recommended");
    Require(turbo->fileName == L"ggml-large-v3-turbo.bin", "large-v3-turbo filename mismatch");
    Require(
        turbo->downloadUrl.find(L"https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo.bin") == 0,
        "large-v3-turbo URL mismatch"
    );
    Require(turbo->tags.find(L"быстрее") != std::wstring::npos, "large-v3-turbo should mention speed");
    Require(turbo->tags.find(L"качество") != std::wstring::npos, "large-v3-turbo should mention quality");
    Require(turbo->sizeBytes > 1024ull * 1024ull * 1024ull, "large-v3-turbo size should be over 1 GiB");

    const auto large = std::ranges::find_if(models, [](const WhisperModelInfo& model) {
        return model.id == L"large-v3";
    });
    Require(large != models.end(), "large-v3 model missing");
    Require(large->bestQuality, "large-v3 should be marked as best quality");
    Require(large->tags.find(L"дольше") != std::wstring::npos, "large-v3 should mention slow speed");

    const fs::path root = fs::path(L"C:/Portable/YoutubeDownloader");
    const AppPaths paths(root);
    Require(
        WhisperManager::ModelPath(paths, *turbo) == root / L"tools" / L"whisper" / L"models" / L"ggml-large-v3-turbo.bin",
        "whisper model path mismatch"
    );
}

void TestWhisperExecutableDiscovery() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_WhisperFindExe");
    const fs::path binDir = root / L"whisper-bin-x64";
    fs::create_directories(binDir);
    {
        std::ofstream out(binDir / L"whisper-cli.exe");
        out << "exe";
    }
    {
        std::ofstream out(binDir / L"ggml.dll");
        out << "dll";
    }

    Require(WhisperManager::FindExecutableDir(root) == binDir, "whisper executable directory mismatch");
}

void TestVotCliInstallInvocation() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_VotCliInstallInvocation");
    const fs::path npmCmd = root / L"nodejs" / L"npm.cmd";
    const ToolProcessInvocation cmdInvocation = BuildVotCliInstallInvocation(npmCmd);

    Require(cmdInvocation.executable.filename() == L"cmd.exe", "npm.cmd should launch through cmd.exe");
    Require(ContainsArg(cmdInvocation.arguments, L"/c"), "npm.cmd invocation should use cmd /c");
    Require(ContainsArg(cmdInvocation.arguments, L"call"), "npm.cmd invocation should use call");
    Require(ContainsArg(cmdInvocation.arguments, npmCmd.wstring()), "npm.cmd path should be passed to cmd.exe");
    Require(ContainsArg(cmdInvocation.arguments, L"install"), "npm install verb missing");
    Require(ContainsArg(cmdInvocation.arguments, L"-g"), "npm global flag missing");
    Require(cmdInvocation.arguments.back() == L"vot-cli", "npm package name should be last");

    const fs::path npmExe = root / L"nodejs" / L"npm.exe";
    const ToolProcessInvocation exeInvocation = BuildVotCliInstallInvocation(npmExe);
    Require(exeInvocation.executable == npmExe, "npm.exe should launch directly");
    Require(exeInvocation.arguments == std::vector<std::wstring>({L"install", L"-g", L"vot-cli"}), "npm.exe arguments mismatch");
}

void TestTranscriptTextPathResolution() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_TranscriptPath");
    const fs::path media = root / L"video.webm";
    const fs::path subtitles = root / L"video.srt";
    const fs::path transcript = root / L"video.txt";
    {
        std::ofstream out(media);
        out << "fake media";
    }
    {
        std::ofstream out(subtitles);
        out << "1";
    }
    {
        std::ofstream out(transcript);
        out << "text";
    }

    const std::vector<fs::path> outputs = {media, subtitles, transcript};
    Require(FindTranscriptTextPath(outputs) == transcript, "transcript txt path should be selected from output files");

    const std::vector<fs::path> noTranscript = {media, subtitles};
    Require(FindTranscriptTextPath(noTranscript).empty(), "missing transcript txt should return empty path");
}

void TestVoiceOverVideoPathResolution() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_VoiceOverPath");
    const fs::path media = root / L"video.mp4";
    const fs::path audio = root / L"video.vot.ru.mp3";
    const fs::path translated = root / L"video.vot.ru.mp4";
    const fs::path mixed = root / L"clip.vot-mixed.ru.mkv";
    {
        std::ofstream out(media);
        out << "fake media";
    }
    {
        std::ofstream out(audio);
        out << "voice-over audio";
    }
    {
        std::ofstream out(translated);
        out << "translated video";
    }
    {
        std::ofstream out(mixed);
        out << "mixed translated video";
    }

    Require(
        FindVoiceOverVideoPath({media, audio, translated}, L"ru") == translated,
        "separate voice-over video path should be selected"
    );
    Require(
        FindVoiceOverVideoPath({media, mixed}, L"ru") == mixed,
        "mixed voice-over video path should be selected"
    );
    Require(
        FindVoiceOverVideoPath({media, audio}, L"ru").empty(),
        "voice-over audio alone should not count as a translated video"
    );
    Require(
        FindVoiceOverVideoPath({translated}, L"en").empty(),
        "voice-over lookup should respect the requested language"
    );
}

void TestYtDlpUpdateDecision() {
    ReleaseAssetInfo latest;
    latest.found = true;
    latest.version = L"2026.06.10";

    ToolInstallStatus current;
    current.installed = true;
    current.version = L"2026.06.09";
    Require(ShouldInstallYtDlpUpdate(current, latest), "older yt-dlp should be updated");

    current.version = L"2026.06.10";
    Require(!ShouldInstallYtDlpUpdate(current, latest), "current yt-dlp should not be updated");

    current.installed = false;
    current.version.clear();
    Require(ShouldInstallYtDlpUpdate(current, latest), "missing yt-dlp should be installed");

    latest.found = false;
    Require(!ShouldInstallYtDlpUpdate(current, latest), "missing release metadata should not trigger update");
}

void TestAppUpdateDecision() {
    ReleaseAssetInfo latest;
    latest.found = true;
    latest.version =
        std::to_wstring(YTD_APP_VERSION_MAJOR) + L"." +
        std::to_wstring(YTD_APP_VERSION_MINOR) + L"." +
        std::to_wstring(YTD_APP_VERSION_PATCH + 1);
    Require(ShouldInstallAppUpdate(latest), "newer app version should be offered");

    latest.version = YTD_APP_VERSION_WIDE;
    Require(!ShouldInstallAppUpdate(latest), "current app version should not be offered");

    latest.version = L"1.0.0";
    Require(!ShouldInstallAppUpdate(latest), "older app version should not be offered");

    latest.found = false;
    latest.version = L"9.9.9";
    Require(!ShouldInstallAppUpdate(latest), "missing app asset should not trigger update");
}

void TestAppUpdatePromptMessage() {
    ReleaseAssetInfo release;
    release.version = L"9.8.7";

    const std::wstring message = BuildAppUpdatePromptMessage(release);
    Require(message.find(L"Доступна новая версия: 9.8.7") != std::wstring::npos, "new version missing from update prompt");
    Require(message.find(L"Текущая версия: " YTD_APP_VERSION_WIDE) != std::wstring::npos, "current version missing from update prompt");
    Require(message.find(L"закрыто и запущено заново") != std::wstring::npos, "restart warning missing from update prompt");
}

void TestFfmpegResolutionPrecedence() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_Ffmpeg");
    const AppPaths paths(root);

    AppConfig config;
    config.ffmpegPath = root / L"chosen" / L"ffmpeg.exe";
    fs::create_directories(config.ffmpegPath.parent_path());
    {
        std::ofstream out(config.ffmpegPath);
        out << "fake";
    }

    fs::create_directories(paths.localFfmpegBinDir());
    {
        std::ofstream out(paths.localFfmpegExePath());
        out << "local fake";
    }

    const FfmpegStatus chosen = FfmpegManager::Resolve(paths, config);
    Require(chosen.available, "saved ffmpeg path should resolve");
    Require(chosen.source == FfmpegSource::ConfiguredPath, "saved ffmpeg path should take precedence");
    Require(chosen.ffmpegExe == config.ffmpegPath, "configured ffmpeg path mismatch");

    config.ffmpegPath.clear();
    const FfmpegStatus local = FfmpegManager::Resolve(paths, config);
    Require(local.available, "local ffmpeg path should resolve");
    Require(local.source == FfmpegSource::LocalTools, "local ffmpeg source mismatch");
    Require(local.ffmpegExe == paths.localFfmpegExePath(), "local ffmpeg path mismatch");
}

void TestFfmpegUserPathAndExtractedTreeResolution() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_FfmpegUserPath");

    const fs::path directExe = root / L"direct" / L"ffmpeg.exe";
    fs::create_directories(directExe.parent_path());
    {
        std::ofstream out(directExe);
        out << "fake";
    }
    const FfmpegStatus direct = FfmpegManager::ResolveUserPath(directExe);
    Require(direct.available, "direct ffmpeg.exe should resolve");
    Require(direct.ffmpegExe == directExe, "direct ffmpeg.exe path mismatch");

    const fs::path plainDir = root / L"plain";
    fs::create_directories(plainDir);
    {
        std::ofstream out(plainDir / L"ffmpeg.exe");
        out << "fake";
    }
    const FfmpegStatus plain = FfmpegManager::ResolveUserPath(plainDir);
    Require(plain.available, "folder containing ffmpeg.exe should resolve");
    Require(plain.ffmpegExe == plainDir / L"ffmpeg.exe", "plain folder ffmpeg path mismatch");

    const fs::path extractedBin = root / L"extract" / L"ffmpeg-8.1.1-essentials_build" / L"bin";
    fs::create_directories(extractedBin);
    {
        std::ofstream out(extractedBin / L"ffmpeg.exe");
        out << "fake";
    }
    Require(
        FfmpegManager::FindExtractedBinDir(root / L"extract") == extractedBin,
        "extracted ffmpeg bin directory mismatch"
    );
}

void TestProcessRunnerCapturesOutputAndExitCode() {
    ProcessRunOptions options;
    options.executable = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = {L"/C", L"echo stdout-line && echo stderr-line 1>&2 && exit /b 7"};
    options.timeoutMs = 5000;

    const ProcessRunResult result = ProcessRunner::Run(options);

    Require(!result.timedOut, "process should not time out");
    Require(!result.canceled, "process should not be canceled");
    Require(result.exitCode == 7, "process exit code mismatch");
    Require(result.stdoutText.find(L"stdout-line") != std::wstring::npos, "stdout was not captured");
    Require(result.stderrText.find(L"stderr-line") != std::wstring::npos, "stderr was not captured");
}

void TestProcessRunnerEmitsEachOutputLineOnce() {
    std::vector<std::wstring> lines;
    ProcessRunOptions options;
    options.executable = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = {L"/C", L"for /L %i in (1,1,1000) do @echo line-%i"};
    options.timeoutMs = 10000;
    options.onStdoutLine = [&](const std::wstring& line) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    };

    const ProcessRunResult result = ProcessRunner::Run(options);

    Require(result.exitCode == 0, "line fixture process failed");
    Require(lines.size() == 1000, "stdout callback should emit each line once");
    Require(std::ranges::count(lines, L"line-1") == 1, "first line was duplicated");
    Require(std::ranges::count(lines, L"line-1000") == 1, "last line was duplicated");
}

void TestDownloadQueueStoreRoundTripSnapshots() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_QueueStore");
    const AppPaths paths(root);

    DownloadTaskSnapshot task;
    task.id = 42;
    task.request.ytDlpExePath = root / L"tools" / L"yt-dlp.exe";
    task.request.url = L"https://www.youtube.com/watch?v=roundtrip";
    task.request.outputDirectory = root / L"Downloads";
    task.request.cookiesPath = root / L"cookies.txt";
    task.request.ffmpegExePath = root / L"tools" / L"ffmpeg.exe";
    task.request.quality = L"1080p";
    task.request.container = L"mp4";
    task.request.ffmpegAvailable = true;
    task.title = L"Round Trip";
    task.thumbnailPath = root / L"stuff" / L"thumb_cache" / L"thumb.jpg";
    task.state = DownloadTaskState::Canceled;
    task.percent = 37.5;
    task.statusText = L"Остановлено";
    task.errorText = L"Manual stop";
    task.downloadedBytes = 1234;
    task.totalBytes = 9999;
    task.speedBytesPerSecond = 456;
    task.etaSeconds = 78;
    task.mediaKind = L"video";
    task.formatId = L"137";
    task.extension = L"mp4";
    task.resolution = L"1080p";
    task.outputFiles = {root / L"Downloads" / L"Round Trip.mp4"};

    DownloadQueueStore::Save(paths, {task});
    const std::vector<DownloadTaskSnapshot> loaded = DownloadQueueStore::Load(paths);

    Require(loaded.size() == 1, "queue store should load one task");
    const DownloadTaskSnapshot& restored = loaded.front();
    Require(restored.id == 42, "restored task id mismatch");
    Require(restored.request.ytDlpExePath == task.request.ytDlpExePath, "restored yt-dlp path mismatch");
    Require(restored.request.url == task.request.url, "restored url mismatch");
    Require(restored.request.outputDirectory == task.request.outputDirectory, "restored output directory mismatch");
    Require(restored.request.cookiesPath == task.request.cookiesPath, "restored cookies path mismatch");
    Require(restored.request.ffmpegExePath == task.request.ffmpegExePath, "restored ffmpeg path mismatch");
    Require(restored.request.quality == L"1080p", "restored quality mismatch");
    Require(restored.request.container == L"mp4", "restored container mismatch");
    Require(restored.request.ffmpegAvailable, "restored ffmpeg flag mismatch");
    Require(restored.title == L"Round Trip", "restored title mismatch");
    Require(restored.thumbnailPath == task.thumbnailPath, "restored thumbnail path mismatch");
    Require(restored.state == DownloadTaskState::Canceled, "restored state mismatch");
    Require(restored.percent == 37.5, "restored percent mismatch");
    Require(restored.statusText == L"Остановлено", "restored status mismatch");
    Require(restored.errorText == L"Manual stop", "restored error mismatch");
    Require(restored.downloadedBytes == 1234, "restored downloaded bytes mismatch");
    Require(restored.totalBytes == 9999, "restored total bytes mismatch");
    Require(restored.speedBytesPerSecond == 456, "restored speed mismatch");
    Require(restored.etaSeconds == 78, "restored eta mismatch");
    Require(restored.mediaKind == L"video", "restored media kind mismatch");
    Require(restored.formatId == L"137", "restored format id mismatch");
    Require(restored.extension == L"mp4", "restored extension mismatch");
    Require(restored.resolution == L"1080p", "restored resolution mismatch");
    Require(restored.outputFiles == task.outputFiles, "restored output files mismatch");
}

void TestDownloadQueueStoreSkipsInvalidEntries() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_QueueStoreInvalid");
    const AppPaths paths(root);
    fs::create_directories(paths.stuffDir());
    {
        std::ofstream out(paths.downloadQueuePath(), std::ios::binary | std::ios::trunc);
        out << R"json({"version":1,"tasks":[{"id":1},{"id":2,"url":"https://example.invalid/ok","state":"Canceled","title":"OK"}]})json";
    }

    const std::vector<DownloadTaskSnapshot> loaded = DownloadQueueStore::Load(paths);

    Require(loaded.size() == 1, "queue store should skip invalid entries only");
    Require(loaded.front().id == 2, "valid queue entry id mismatch");
    Require(loaded.front().request.url == L"https://example.invalid/ok", "valid queue entry url mismatch");
}

bool WaitForProcessExit(DWORD processId, std::chrono::milliseconds timeout) {
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) {
        return true;
    }
    const DWORD wait = WaitForSingleObject(process, static_cast<DWORD>(timeout.count()));
    CloseHandle(process);
    return wait == WAIT_OBJECT_0;
}

void TerminateProcessIfStillRunning(DWORD processId) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, processId);
    if (!process) {
        return;
    }
    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 5000);
    }
    CloseHandle(process);
}

std::filesystem::path CurrentTestExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2, L'\0');
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    Require(length > 0, "failed to resolve current test executable path");
    buffer.resize(length);
    return buffer;
}

void TestYtDlpExecutableVersionValidation() {
    Require(
        ValidateYtDlpExecutableVersion(CurrentTestExecutablePath(), L"2026.06.09"),
        "yt-dlp executable validation should accept expected version output"
    );
    Require(
        !ValidateYtDlpExecutableVersion(CurrentTestExecutablePath(), L"2026.06.10"),
        "yt-dlp executable validation should reject mismatched version output"
    );
}

std::wstring QuoteCommandArgument(const std::wstring& arg) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : arg) {
        if (ch == L'"') {
            quoted += L"\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

int RunProcessTreeParentFixture() {
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION process = {};
    std::wstring commandLine =
        QuoteCommandArgument(CurrentTestExecutablePath().wstring()) + L" --process-tree-child-fixture";
    if (!CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process
        )) {
        return 2;
    }

    std::cout << process.dwProcessId << "\n";
    std::cout.flush();
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Sleep(60000);
    return 0;
}

int RunProcessTreeChildFixture() {
    Sleep(60000);
    return 0;
}

void TestProcessRunnerCancelKillsChildProcessTree() {
    HANDLE cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Require(cancelEvent != nullptr, "failed to create process cancel event");

    DWORD childPid = 0;
    ProcessRunOptions options;
    options.executable = CurrentTestExecutablePath();
    options.arguments = {L"--process-tree-parent-fixture"};
    options.timeoutMs = 10000;
    options.cancelEvent = cancelEvent;
    options.onStdoutLine = [&](const std::wstring& line) {
        if (childPid != 0 || line.empty()) {
            return;
        }
        try {
            childPid = static_cast<DWORD>(std::stoul(line));
            SetEvent(cancelEvent);
        } catch (...) {
        }
    };

    const ProcessRunResult result = ProcessRunner::Run(options);
    CloseHandle(cancelEvent);

    Require(result.canceled, "parent process should be canceled");
    Require(childPid != 0, "child process id was not captured");
    const bool childExited = WaitForProcessExit(childPid, std::chrono::milliseconds(5000));
    if (!childExited) {
        TerminateProcessIfStillRunning(childPid);
    }
    Require(childExited, "canceling a process run should kill child processes");
}

void TestYtDlpMetadataParsing() {
    const std::string videoJson = R"json(
{
  "id": "abc123",
  "title": "Example Video",
  "uploader": "Example Channel",
  "duration": 125,
  "thumbnail": "https://example.invalid/thumb.jpg",
  "webpage_url": "https://www.youtube.com/watch?v=abc123"
}
)json";

    const VideoPreview video = ParseVideoPreviewJson(videoJson);
    Require(video.id == L"abc123", "video id mismatch");
    Require(video.title == L"Example Video", "video title mismatch");
    Require(video.uploader == L"Example Channel", "video uploader mismatch");
    Require(video.durationSeconds == 125, "video duration mismatch");
    Require(video.thumbnailUrl == L"https://example.invalid/thumb.jpg", "video thumbnail mismatch");
    Require(!video.isPlaylist, "single video should not be playlist");

    const std::string thumbnailFallbackJson = R"json(
{
  "id": "thumb1",
  "title": "Thumbnail Fallback",
  "thumbnail": "https://example.invalid/thumb.webp",
  "thumbnails": [
    {"url": "https://example.invalid/thumb-small.webp"},
    {"url": "https://example.invalid/thumb-large.jpg"}
  ]
}
)json";

    const VideoPreview thumbnailFallback = ParseVideoPreviewJson(thumbnailFallbackJson);
    Require(
        thumbnailFallback.thumbnailUrl == L"https://example.invalid/thumb-large.jpg",
        "metadata parser should prefer GDI-friendly thumbnails"
    );

    const std::string playlistJson = R"json(
{
  "id": "playlist1",
  "title": "Example Playlist",
  "_type": "playlist",
  "webpage_url": "https://www.youtube.com/playlist?list=playlist1",
  "entries": [
    {"id": "v1", "title": "Video One", "url": "https://www.youtube.com/watch?v=v1", "duration": 10, "thumbnail": "https://example.invalid/v1.jpg"},
    {"id": "v2", "title": "Video Two", "webpage_url": "https://www.youtube.com/watch?v=v2", "duration": 20, "thumbnail": "https://example.invalid/v2.jpg"}
  ]
}
)json";

    const VideoPreview playlist = ParseVideoPreviewJson(playlistJson);
    Require(playlist.isPlaylist, "playlist should be marked as playlist");
    Require(playlist.entries.size() == 2, "playlist entry count mismatch");
    Require(playlist.entries[0].id == L"v1", "playlist first entry id mismatch");
    Require(playlist.entries[0].webpageUrl == L"https://www.youtube.com/watch?v=v1", "playlist first entry url mismatch");
    Require(playlist.entries[0].thumbnailUrl == L"https://example.invalid/v1.jpg", "playlist first entry thumbnail mismatch");
    Require(playlist.entries[1].title == L"Video Two", "playlist second entry title mismatch");
    Require(playlist.entries[1].thumbnailUrl == L"https://example.invalid/v2.jpg", "playlist second entry thumbnail mismatch");
}

void TestDownloadQueueSchedulingAndRetry() {
    std::atomic<int> running = 0;
    std::atomic<int> maxRunning = 0;
    std::atomic<int> attempts = 0;

    DownloadQueue queue(2);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        const int now = ++running;
        int observed = maxRunning.load();
        while (now > observed && !maxRunning.compare_exchange_weak(observed, now)) {
        }
        UNREFERENCED_PARAMETER(callbacks);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        --running;
        ++attempts;
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/video";
    request.outputDirectory = fs::temp_directory_path();
    YtDlpDownloadRequest secondRequest = request;
    secondRequest.url = L"https://example.invalid/video-2";
    YtDlpDownloadRequest thirdRequest = request;
    thirdRequest.url = L"https://example.invalid/video-3";

    const int first = queue.Enqueue(request, L"First");
    const int second = queue.Enqueue(secondRequest, L"Second");
    const int third = queue.Enqueue(thirdRequest, L"Third");

    queue.WaitForIdle();
    Require(maxRunning.load() <= 2, "queue exceeded max parallel downloads");
    Require(queue.GetTask(first).state == DownloadTaskState::Completed, "first task should complete");
    Require(queue.GetTask(second).state == DownloadTaskState::Completed, "second task should complete");
    Require(queue.GetTask(third).state == DownloadTaskState::Completed, "third task should complete");
    Require(queue.ClearFinished() == 3, "clear finished should remove completed tasks");

    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        ++attempts;
        return DownloadTaskResult{false, L"planned failure", {}};
    });
    const int failing = queue.Enqueue(request, L"Failing");
    queue.WaitForIdle();
    Require(queue.GetTask(failing).state == DownloadTaskState::Failed, "failing task should fail");

    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        ++attempts;
        return DownloadTaskResult{true, L"", {}};
    });
    Require(queue.Retry(failing), "retry should be accepted for failed task");
    queue.WaitForIdle();
    Require(queue.GetTask(failing).state == DownloadTaskState::Completed, "retried task should complete");
    Require(attempts.load() >= 5, "executor attempt count mismatch");
}

void TestDownloadQueuePostProcessingStatus() {
    std::atomic<bool> release = false;
    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onPostProcessing(37.0, L"Recognizing speech");
        while (!release.load()) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{true, L"", {}, L"Ready with transcript"};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/post-processing";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, L"Post Processing");
    for (int i = 0; i < 100; ++i) {
        const DownloadTaskSnapshot task = queue.GetTask(id);
        if (task.state == DownloadTaskState::PostProcessing) {
            Require(task.statusText == L"Recognizing speech", "post-processing status mismatch");
            Require(task.percent == 37.0, "post-processing percent mismatch");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(queue.GetTask(id).state == DownloadTaskState::PostProcessing, "task should enter post-processing state");

    release = true;
    queue.WaitForIdle();
    const DownloadTaskSnapshot completed = queue.GetTask(id);
    Require(completed.state == DownloadTaskState::Completed, "post-processing task should complete");
    Require(completed.statusText == L"Ready with transcript", "completed task should keep executor status");
}

void TestDownloadQueueCompletedTaskShowsElapsedTime() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/elapsed-time";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, L"Elapsed Time");
    queue.WaitForIdle();

    const DownloadTaskSnapshot completed = queue.GetTask(id);
    Require(completed.state == DownloadTaskState::Completed, "elapsed-time task should complete");
    Require(
        completed.statusText.find(L"скачано за") != std::wstring::npos,
        "completed task should show elapsed download time"
    );
    Require(
        completed.statusText.find(L"секунда") != std::wstring::npos ||
            completed.statusText.find(L"секунды") != std::wstring::npos ||
            completed.statusText.find(L"секунд") != std::wstring::npos,
        "completed task should format elapsed seconds"
    );
}

void TestDownloadQueueProgressResetsForNextDownloadTrack() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_TrackProgressReset");
    const fs::path output = root / L"video.mp4";
    std::atomic<bool> release = false;

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onOutputLine(L"[download] Destination: " + output.wstring());

        {
            std::ofstream out(output, std::ios::binary);
            out << std::string(1000, 'v');
        }

        YtDlpProgress video;
        video.recognized = true;
        video.stage = L"Скачивание видео:";
        video.mediaKind = L"video";
        video.percent = 100.0;
        video.downloadedBytes = 1000;
        video.totalBytes = 1000;
        callbacks.onProgressDetails(video);

        YtDlpProgress audio;
        audio.recognized = true;
        audio.stage = L"Скачивание аудио:";
        audio.mediaKind = L"audio";
        audio.percent = 25.0;
        audio.downloadedBytes = 250;
        audio.totalBytes = 1000;
        callbacks.onProgressDetails(audio);

        YtDlpProgress audioNext = audio;
        audioNext.percent = 40.0;
        audioNext.downloadedBytes = 400;
        callbacks.onProgressDetails(audioNext);

        while (!release.load()) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{false, L"planned stop", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/track-progress-reset";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Track Progress Reset");
    for (int i = 0; i < 100; ++i) {
        const DownloadTaskSnapshot task = queue.GetTask(id);
        if (task.state == DownloadTaskState::Downloading && task.mediaKind == L"audio" && task.percent >= 40.0) {
            Require(task.percent == 40.0, "audio track progress should not include completed video bytes");
            Require(task.downloadedBytes == 400, "audio downloaded bytes should reset to the current track");
            Require(task.totalBytes == 1000, "audio total bytes should come from the current track");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const DownloadTaskSnapshot audioTask = queue.GetTask(id);
    Require(audioTask.mediaKind == L"audio", "task should expose audio as the active track");
    Require(audioTask.percent == 40.0, "audio progress should remain at the current track percent");

    release = true;
    queue.WaitForIdle();
}

void TestDownloadQueueStartsPostProcessingForCompletedTask() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_ManualPostProcessing");
    const fs::path media = root / L"video.mp4";
    const fs::path transcript = root / L"video.txt";
    std::atomic<bool> release = false;

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(stopToken);
        callbacks.onOutputLine(L"[download] Destination: " + media.wstring());
        {
            std::ofstream out(media, std::ios::binary);
            out << "downloaded media";
        }
        return DownloadTaskResult{true, L"", {media}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/manual-transcription";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Manual Transcription");
    queue.WaitForIdle();
    Require(queue.GetTask(id).state == DownloadTaskState::Completed, "fixture task should complete before manual post-processing");

    const bool started = queue.StartPostProcessing(
        id,
        [&](
            const DownloadTaskSnapshot& task,
            std::stop_token stopToken,
            const DownloadTaskCallbacks& callbacks
        ) {
            Require(task.state == DownloadTaskState::PostProcessing, "manual post-processing snapshot state mismatch");
            Require(
                std::ranges::find(task.outputFiles, media) != task.outputFiles.end(),
                "manual post-processing should receive completed media output"
            );
            callbacks.onPostProcessing(42.0, L"Recognizing speech");
            while (!release.load()) {
                if (stopToken.stop_requested()) {
                    return DownloadTaskResult{true, L"", task.outputFiles, L"Ready without transcript"};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            {
                std::ofstream out(transcript);
                out << "transcript";
            }
            return DownloadTaskResult{true, L"", {transcript}, L"Ready with transcript"};
        },
        L"Recognizing speech"
    );
    Require(started, "manual post-processing should start for a completed task");

    for (int i = 0; i < 100; ++i) {
        const DownloadTaskSnapshot task = queue.GetTask(id);
        if (task.state == DownloadTaskState::PostProcessing && task.percent == 42.0) {
            Require(task.statusText == L"Recognizing speech", "manual post-processing status mismatch");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Require(queue.GetTask(id).state == DownloadTaskState::PostProcessing, "task should enter manual post-processing state");

    release = true;
    queue.WaitForIdle();

    const DownloadTaskSnapshot completed = queue.GetTask(id);
    Require(completed.state == DownloadTaskState::Completed, "manual post-processing task should return to completed");
    Require(completed.statusText == L"Ready with transcript", "manual post-processing should keep completion status");
    Require(std::ranges::find(completed.outputFiles, media) != completed.outputFiles.end(), "manual post-processing should keep media output");
    Require(std::ranges::find(completed.outputFiles, transcript) != completed.outputFiles.end(), "manual post-processing should add transcript output");
}

void TestDownloadQueueLogsTaskLifecycle() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_QueueLogger");
    const AppPaths paths(root);
    Logger logger(paths);
    DownloadQueue queue(1, &logger);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/logged-task";
    queue.Enqueue(request, L"Logged task");
    queue.WaitForIdle();

    std::ifstream log(paths.logPath(), std::ios::binary);
    const std::string text{
        std::istreambuf_iterator<char>(log),
        std::istreambuf_iterator<char>()
    };
    Require(text.find("https://example.invalid/logged-task") != std::string::npos, "queue log should identify the task URL");
    Require(text.find("completed") != std::string::npos, "queue log should record task completion");
}

void TestDownloadQueueLogsPostProcessingProgress() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_PostProcessingLogger");
    const AppPaths paths(root);
    Logger logger(paths);
    DownloadQueue queue(1, &logger);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onPostProcessing(50.0, L"Recognizing speech");
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/logged-post-processing";
    queue.Enqueue(request, L"Logged post-processing");
    queue.WaitForIdle();

    std::ifstream log(paths.logPath(), std::ios::binary);
    const std::string text{
        std::istreambuf_iterator<char>(log),
        std::istreambuf_iterator<char>()
    };
    Require(text.find("Post-processing progress") != std::string::npos, "queue log should record post-processing progress");
    Require(text.find("status=\"Recognizing speech\"") != std::string::npos, "queue log should include post-processing status");
    Require(text.find("progress=50.0%") != std::string::npos, "queue log should include post-processing percent");
    Require(text.find("elapsed=") != std::string::npos, "queue log should include post-processing elapsed time");
    Require(text.find("eta=") != std::string::npos, "queue log should include post-processing eta");
}

void TestDownloadQueueShutdownWaitsForActiveWorker() {
    std::atomic<bool> started = false;
    std::atomic<bool> finished = false;
    {
        DownloadQueue queue(1);
        queue.SetExecutor([&](
            const DownloadTaskSnapshot& task,
            std::stop_token stopToken,
            const DownloadTaskCallbacks& callbacks
        ) {
            UNREFERENCED_PARAMETER(task);
            UNREFERENCED_PARAMETER(stopToken);
            UNREFERENCED_PARAMETER(callbacks);
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            finished = true;
            return DownloadTaskResult{false, L"stopped", {}};
        });

        YtDlpDownloadRequest request;
        request.url = L"https://example.invalid/shutdown";
        queue.Enqueue(request, L"Shutdown");
        Require(WaitUntil([&]() { return started.load(); }), "shutdown fixture did not start");
    }
    Require(finished.load(), "queue destruction should join active workers");
}

void TestDownloadQueueUpdatesParallelismAtRuntime() {
    std::atomic<int> started = 0;
    std::atomic<int> running = 0;
    std::atomic<int> maxRunning = 0;
    std::atomic<bool> release = false;

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        ++started;
        const int now = ++running;
        int observed = maxRunning.load();
        while (now > observed && !maxRunning.compare_exchange_weak(observed, now)) {
        }
        while (!release.load() && !stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        --running;
        return DownloadTaskResult{true, L"", {}};
    });

    for (int index = 0; index < 3; ++index) {
        YtDlpDownloadRequest request;
        request.url = L"https://example.invalid/parallel-" + std::to_wstring(index);
        queue.Enqueue(request, L"Parallel");
    }

    Require(WaitUntil([&]() { return started.load() == 1; }), "initial parallel limit was not applied");
    queue.SetMaxParallelDownloads(2);
    Require(WaitUntil([&]() { return started.load() == 2; }), "increased parallel limit was not applied immediately");

    queue.SetMaxParallelDownloads(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Require(started.load() == 2, "lowered parallel limit should not start another task");
    Require(maxRunning.load() == 2, "runtime parallel limit should permit exactly two active tasks");

    release = true;
    queue.WaitForIdle();
    Require(started.load() == 3, "queued work should resume after active tasks finish");
}

void TestDownloadQueueRejectsDuplicateVisibleUrl() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/duplicate";
    request.outputDirectory = fs::temp_directory_path();

    const int first = queue.Enqueue(request, L"First");
    const int duplicate = queue.Enqueue(request, L"Duplicate");

    Require(duplicate == first, "duplicate visible URL should return the existing task id");
    Require(queue.Snapshot().size() == 1, "duplicate visible URL should not create another task row");
    queue.WaitForIdle();
}

void TestDownloadQueueAllowsDuplicateUrlAfterCompletion() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/repeat-completed";
    request.outputDirectory = fs::temp_directory_path();

    const int first = queue.Enqueue(request, L"First");
    queue.WaitForIdle();
    Require(queue.GetTask(first).state == DownloadTaskState::Completed, "first duplicate-url task should complete");

    const int second = queue.Enqueue(request, L"Second");
    Require(second != first, "completed duplicate URL should enqueue a new task");
    Require(queue.Snapshot().size() == 2, "completed duplicate URL should keep both visible task rows");
    queue.WaitForIdle();
}

void TestDownloadQueueEnrichesDuplicateVisibleUrl() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/enrich";
    request.outputDirectory = fs::temp_directory_path();

    const int first = queue.Enqueue(request, request.url);
    const fs::path thumbnail = fs::temp_directory_path() / L"thumb.jpg";
    const int duplicate = queue.Enqueue(request, L"Resolved Title", thumbnail);

    const DownloadTaskSnapshot task = queue.GetTask(first);
    Require(duplicate == first, "duplicate visible URL should return the existing enriched task id");
    Require(task.title == L"Resolved Title", "duplicate enqueue should enrich placeholder title");
    Require(task.thumbnailPath == thumbnail, "duplicate enqueue should enrich missing thumbnail");
    queue.WaitForIdle();
}

void TestDownloadQueueIgnoresOutputLinesWithoutPathsForRevision() {
    DownloadQueue queue(1);
    std::atomic<std::uint64_t> beforeLine = 0;
    std::atomic<std::uint64_t> afterLine = 0;
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        beforeLine = queue.Revision();
        callbacks.onOutputLine(L"[download] plain status line without output path");
        afterLine = queue.Revision();
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/revision-noise";
    request.outputDirectory = fs::temp_directory_path();

    queue.Enqueue(request, L"Revision Noise");
    queue.WaitForIdle();
    Require(beforeLine.load() == afterLine.load(), "non-path output lines should not change queue revision");
}

void TestDownloadQueueEnrichesExistingTaskAfterPreviewCompletes() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/late-preview";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, request.url);
    const fs::path thumbnail = fs::temp_directory_path() / L"late-thumb.jpg";
    Require(queue.EnrichMetadata(request.url, L"Late Preview Title", thumbnail), "preview completion should enrich existing task");
    const DownloadTaskSnapshot task = queue.GetTask(id);
    Require(task.title == L"Late Preview Title", "late preview should update placeholder title");
    Require(task.thumbnailPath == thumbnail, "late preview should update missing thumbnail");
    queue.WaitForIdle();
}

void TestDownloadQueueCancelAndDeleteTask() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        for (int i = 0; i < 20; ++i) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/cancel";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, L"Cancelable");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Require(queue.Cancel(id), "running task should accept cancel");
    queue.WaitForIdle();
    Require(queue.GetTask(id).state == DownloadTaskState::Canceled, "task should become canceled");
    Require(queue.DeleteFiles(id), "delete should remove canceled task from queue");
    Require(queue.Snapshot().empty(), "deleted canceled task should disappear from queue");
}

void TestDownloadQueueCloseCompletedTaskKeepsOutputFile() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_DeleteOutput");
    const fs::path output = root / L"video.mp4";
    {
        std::ofstream out(output);
        out << "fake video";
    }

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onOutputLine(L"[download] Destination: " + output.wstring());
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/delete-output";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Delete Output");
    queue.WaitForIdle();
    Require(fs::is_regular_file(output), "fixture output file should exist before delete");
    Require(queue.DeleteFiles(id), "close should remove completed task");
    Require(fs::exists(output), "close should keep completed output file");
    Require(queue.Snapshot().empty(), "closed completed task should disappear from queue");
}

void TestDownloadQueueDeleteCanceledTaskRemovesPartialFile() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_DeletePartial");
    const fs::path output = root / L"video.mp4";
    const fs::path partial = fs::path(output.wstring() + L".part");

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onOutputLine(L"[download] Destination: " + output.wstring());
        {
            std::ofstream out(partial);
            out << "partial video";
        }
        for (int i = 0; i < 50; ++i) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/delete-partial";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Delete Partial");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Require(queue.Cancel(id), "running task should accept cancel before partial delete");
    queue.WaitForIdle();
    Require(fs::is_regular_file(partial), "partial output file should exist before delete");
    Require(queue.DeleteFiles(id), "delete should remove canceled task");
    Require(!fs::exists(partial), "delete should remove partial output file");
    Require(queue.Snapshot().empty(), "deleted canceled task should disappear from queue");
}

void TestDownloadQueueDeleteCanceledTaskHandlesIndentedDestinationAndFragments() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_DeleteIndentedPartial");
    const fs::path output = root / L"video.f399.mp4";
    const fs::path partial = fs::path(output.wstring() + L".part");
    const fs::path fragment = fs::path(output.wstring() + L".part-Frag42.part");

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onOutputLine(L"\r  [download] Destination: " + output.wstring());
        {
            std::ofstream out(partial);
            out << "partial video";
        }
        {
            std::ofstream out(fragment);
            out << "partial fragment";
        }
        for (int i = 0; i < 50; ++i) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/delete-indented-partial";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Delete Indented Partial");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Require(queue.Cancel(id), "running indented partial task should accept cancel");
    queue.WaitForIdle();
    Require(fs::is_regular_file(partial), "indented partial output file should exist before delete");
    Require(fs::is_regular_file(fragment), "fragment output file should exist before delete");
    Require(queue.DeleteFiles(id), "delete should remove canceled task with indented destination");
    Require(!fs::exists(partial), "delete should remove indented partial output file");
    Require(!fs::exists(fragment), "delete should remove fragment partial output file");
}

void TestDownloadQueueDeleteCanceledTaskFindsPartFileByVideoIdWhenDestinationWasNotCaptured() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_DeleteByVideoId");
    const fs::path orphanPart = root / L"Example Title [SegQA7FyGt0].f137.mp4.part";

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        {
            std::ofstream out(orphanPart);
            out << "partial video";
        }
        for (int i = 0; i < 50; ++i) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://www.youtube.com/watch?v=SegQA7FyGt0&list=playlist";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Example Title");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Require(queue.Cancel(id), "running by-id partial task should accept cancel");
    queue.WaitForIdle();
    Require(fs::is_regular_file(orphanPart), "orphan partial output file should exist before delete");
    Require(queue.DeleteFiles(id), "delete should remove canceled task with uncaptured destination");
    Require(!fs::exists(orphanPart), "delete should remove uncaptured partial output file by video id");
}

void TestDownloadQueueClearQueuedOnlyRemovesWaitingTasks() {
    std::atomic<bool> release = false;
    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        while (!release.load()) {
            if (stopToken.stop_requested()) {
                return DownloadTaskResult{false, L"canceled", {}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest firstRequest;
    firstRequest.url = L"https://example.invalid/active";
    firstRequest.outputDirectory = fs::temp_directory_path();
    YtDlpDownloadRequest secondRequest = firstRequest;
    secondRequest.url = L"https://example.invalid/queued-1";
    YtDlpDownloadRequest thirdRequest = firstRequest;
    thirdRequest.url = L"https://example.invalid/queued-2";

    const int active = queue.Enqueue(firstRequest, L"Active");
    const int queuedOne = queue.Enqueue(secondRequest, L"Queued 1");
    const int queuedTwo = queue.Enqueue(thirdRequest, L"Queued 2");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    Require(queue.ClearQueued() == 2, "clear queued should remove waiting tasks");
    Require(queue.GetTask(active).state != DownloadTaskState::Queued, "active task should not be cleared");
    Require(queue.GetTask(queuedOne).id == 0, "first queued task should be removed");
    Require(queue.GetTask(queuedTwo).id == 0, "second queued task should be removed");

    release = true;
    queue.WaitForIdle();
    Require(queue.GetTask(active).state == DownloadTaskState::Completed, "active task should continue after clearing queue");
}

void TestDownloadQueueClearFinishedKeepsQueuedTasks() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        UNREFERENCED_PARAMETER(callbacks);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/finished";
    request.outputDirectory = fs::temp_directory_path();

    const int completed = queue.Enqueue(request, L"Completed");
    queue.WaitForIdle();

    YtDlpDownloadRequest queuedRequest = request;
    queuedRequest.url = L"https://example.invalid/still-queued";
    const int queued = queue.Enqueue(queuedRequest, L"Still queued");

    Require(queue.ClearFinished() == 1, "clear finished should remove only completed tasks");
    Require(queue.GetTask(completed).id == 0, "completed task should be removed by clear finished");
    Require(queue.GetTask(queued).id == queued, "queued task should not be removed by clear finished");
    queue.WaitForIdle();
}

void TestDownloadQueueDownloadedBytesDoNotMoveBackward() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_MonotonicBytes");
    const fs::path output = root / L"video.mp4";

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        callbacks.onOutputLine(L"[download] Destination: " + output.wstring());

        {
            std::ofstream out(output, std::ios::binary);
            out << std::string(800, 'a');
        }
        YtDlpProgress first;
        first.recognized = true;
        first.stage = L"Скачивание видео:";
        first.percent = 50.0;
        first.downloadedBytes = 800;
        first.totalBytes = 1600;
        callbacks.onProgressDetails(first);

        {
            std::ofstream out(output, std::ios::binary | std::ios::trunc);
            out << std::string(70, 'b');
        }
        YtDlpProgress second;
        second.recognized = true;
        second.stage = L"Скачивание аудио:";
        second.percent = 10.0;
        second.downloadedBytes = 70;
        second.totalBytes = 700;
        callbacks.onProgressDetails(second);
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/monotonic";
    request.outputDirectory = root;

    const int id = queue.Enqueue(request, L"Monotonic");
    queue.WaitForIdle();
    const DownloadTaskSnapshot task = queue.GetTask(id);
    Require(task.downloadedBytes == 800, "displayed downloaded bytes should not move backward");
    Require(task.totalBytes == 1600, "displayed total bytes should not move backward");
}

void TestDownloadQueueProgressPercentDoesNotMoveBackwardWithinTrack() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        YtDlpProgress first;
        first.recognized = true;
        first.stage = L"Скачивание видео:";
        first.mediaKind = L"video";
        first.percent = 50.0;
        first.downloadedBytes = 500;
        first.totalBytes = 1000;
        callbacks.onProgressDetails(first);

        YtDlpProgress second = first;
        second.percent = 30.0;
        second.downloadedBytes = 300;
        callbacks.onProgressDetails(second);
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/percent-monotonic";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, L"Percent Monotonic");
    queue.WaitForIdle();
    const DownloadTaskSnapshot task = queue.GetTask(id);
    Require(task.percent == 100.0, "completed task should end at 100 percent");
    Require(task.downloadedBytes == 500, "downloaded bytes should stay monotonic within the same track");
}

void TestDownloadQueueIgnoresStaleVideoProgressAfterAudioStarts() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        YtDlpProgress video;
        video.recognized = true;
        video.stage = L"Скачивание видео:";
        video.mediaKind = L"video";
        video.percent = 100.0;
        video.downloadedBytes = 1000;
        video.totalBytes = 1000;
        callbacks.onProgressDetails(video);

        YtDlpProgress audio;
        audio.recognized = true;
        audio.stage = L"Скачивание аудио:";
        audio.mediaKind = L"audio";
        audio.percent = 25.0;
        audio.downloadedBytes = 250;
        audio.totalBytes = 1000;
        callbacks.onProgressDetails(audio);

        YtDlpProgress staleVideo = video;
        staleVideo.percent = 90.0;
        staleVideo.downloadedBytes = 900;
        callbacks.onProgressDetails(staleVideo);
        return DownloadTaskResult{false, L"planned stop", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/stale-video";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, L"Stale Video");
    queue.WaitForIdle();
    const DownloadTaskSnapshot task = queue.GetTask(id);
    Require(task.state == DownloadTaskState::Failed, "fixture task should fail after progress assertions");
    Require(task.mediaKind == L"audio", "stale video progress should not replace active audio track");
    Require(task.statusText == L"Ошибка", "failed task should keep final failed status");
}

void TestDownloadQueueIgnoresUnclassifiedFinishedProgressAfterAudioStarts() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot& task,
        std::stop_token,
        const DownloadTaskCallbacks& callbacks
    ) {
        UNREFERENCED_PARAMETER(task);
        YtDlpProgress video;
        video.recognized = true;
        video.stage = L"Загрузка завершена (часть)";
        video.mediaKind = L"video";
        video.percent = 100.0;
        video.downloadedBytes = 1000;
        video.totalBytes = 1000;
        callbacks.onProgressDetails(video);

        YtDlpProgress audio;
        audio.recognized = true;
        audio.stage = L"Скачивание аудио:";
        audio.mediaKind = L"audio";
        audio.percent = 25.0;
        audio.downloadedBytes = 250;
        audio.totalBytes = 1000;
        callbacks.onProgressDetails(audio);

        YtDlpProgress staleFinished;
        staleFinished.recognized = true;
        staleFinished.rawStatus = L"finished";
        staleFinished.stage = L"Загрузка завершена (часть)";
        staleFinished.percent = 100.0;
        staleFinished.downloadedBytes = 1000;
        staleFinished.totalBytes = 1000;
        callbacks.onProgressDetails(staleFinished);
        return DownloadTaskResult{false, L"planned stop", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/stale-finished";
    request.outputDirectory = fs::temp_directory_path();

    const int id = queue.Enqueue(request, L"Stale Finished");
    queue.WaitForIdle();
    const DownloadTaskSnapshot task = queue.GetTask(id);
    Require(task.state == DownloadTaskState::Failed, "fixture task should fail after stale finished progress");
    Require(task.mediaKind == L"audio", "unclassified finished progress should not replace active audio track");
    Require(task.percent == 25.0, "unclassified finished progress should not restore video percent during audio");
}

void TestDownloadQueueClearFinishedDeletesInvalidPartFilesOnly() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_ClearFinishedInvalid");
    const fs::path completedOutput = root / L"completed.mp4";
    const fs::path canceledPart = root / L"Canceled Title [clearInvalid1].f137.mp4.part";
    {
        std::ofstream out(completedOutput);
        out << "complete video";
    }

    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    ) {
        if (task.request.url.find(L"clearInvalid1") != std::wstring::npos) {
            {
                std::ofstream out(canceledPart);
                out << "partial video";
            }
            for (int i = 0; i < 50; ++i) {
                if (stopToken.stop_requested()) {
                    return DownloadTaskResult{false, L"canceled", {}};
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return DownloadTaskResult{true, L"", {}};
        }
        callbacks.onOutputLine(L"[download] Destination: " + completedOutput.wstring());
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest completedRequest;
    completedRequest.url = L"https://example.invalid/completed";
    completedRequest.outputDirectory = root;
    const int completed = queue.Enqueue(completedRequest, L"Completed");
    queue.WaitForIdle();
    Require(queue.GetTask(completed).state == DownloadTaskState::Completed, "completed fixture task should complete");

    YtDlpDownloadRequest canceledRequest = completedRequest;
    canceledRequest.url = L"https://www.youtube.com/watch?v=clearInvalid1";
    const int canceled = queue.Enqueue(canceledRequest, L"Canceled Title");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Require(queue.Cancel(canceled), "clear-finished canceled task should accept cancel");
    queue.WaitForIdle();
    Require(fs::is_regular_file(completedOutput), "completed output should exist before clear finished");
    Require(fs::is_regular_file(canceledPart), "canceled partial should exist before clear finished");

    Require(queue.ClearFinished() == 2, "clear finished should remove completed and invalid tasks");
    Require(fs::is_regular_file(completedOutput), "clear finished should keep completed output");
    Require(!fs::exists(canceledPart), "clear finished should delete canceled partial output");
    Require(queue.Snapshot().empty(), "clear finished should remove task rows");
}

void TestDownloadQueueImportsRestoredTasksWithoutStartingThem() {
    std::atomic<bool> executorRan = false;
    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot&,
        std::stop_token,
        const DownloadTaskCallbacks&
    ) {
        executorRan = true;
        return DownloadTaskResult{true, L"", {}};
    });

    DownloadTaskSnapshot restored;
    restored.id = 10;
    restored.request.url = L"https://example.invalid/restored";
    restored.title = L"Restored";
    restored.state = DownloadTaskState::Queued;
    restored.statusText = L"В очереди";

    queue.ImportSnapshots({restored});
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    const std::vector<DownloadTaskSnapshot> tasks = queue.Snapshot();
    Require(tasks.size() == 1, "import should restore one task");
    Require(tasks.front().id == 10, "restored task id mismatch");
    Require(tasks.front().state == DownloadTaskState::Canceled, "restored queued task should be stopped");
    Require(tasks.front().statusText == L"Остановлено", "restored queued task status mismatch");
    Require(!executorRan.load(), "restored tasks should not start automatically");
}

void TestDownloadQueueExportForShutdownStopsActiveTasks() {
    std::atomic<bool> started = false;
    DownloadQueue queue(1);
    queue.SetExecutor([&](
        const DownloadTaskSnapshot&,
        std::stop_token stopToken,
        const DownloadTaskCallbacks&
    ) {
        started = true;
        while (!stopToken.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return DownloadTaskResult{false, L"stopped", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/active";
    const int id = queue.Enqueue(request, L"Active");
    while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const std::vector<DownloadTaskSnapshot> shutdown = queue.ExportSnapshotsForShutdown();

    Require(shutdown.size() == 1, "shutdown export should include active task");
    Require(shutdown.front().id == id, "shutdown export id mismatch");
    Require(shutdown.front().state == DownloadTaskState::Canceled, "active task should persist as canceled on shutdown");
    Require(shutdown.front().statusText == L"Остановлено", "active task shutdown status mismatch");
}

void TestDownloadQueueExportSkipsCompletedTasks() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot&,
        std::stop_token,
        const DownloadTaskCallbacks&
    ) {
        return DownloadTaskResult{true, L"", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/completed-export";
    const int id = queue.Enqueue(request, L"Completed Export");
    queue.WaitForIdle();

    Require(queue.GetTask(id).state == DownloadTaskState::Completed, "completed export fixture should complete");
    Require(queue.Snapshot().size() == 1, "completed task should remain visible in the current UI snapshot");
    Require(queue.ExportSnapshots().empty(), "completed task should not be persisted during normal export");
    Require(queue.ExportSnapshotsForShutdown().empty(), "completed task should not be persisted during shutdown export");
}

void TestDownloadQueueDeletedTaskIsNotExported() {
    DownloadQueue queue(1);
    queue.SetExecutor([](
        const DownloadTaskSnapshot&,
        std::stop_token,
        const DownloadTaskCallbacks&
    ) {
        return DownloadTaskResult{false, L"not started", {}};
    });

    YtDlpDownloadRequest request;
    request.url = L"https://example.invalid/delete-export";
    const int id = queue.Enqueue(request, L"Delete Export");
    Require(queue.Cancel(id), "queued task should cancel before delete");
    Require(queue.DeleteFiles(id), "canceled task should delete");

    Require(queue.ExportSnapshots().empty(), "deleted task should not be exported");
    Require(queue.ExportSnapshotsForShutdown().empty(), "deleted task should not be exported for shutdown");
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--version") {
        std::cout << "2026.06.09\n";
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--process-tree-parent-fixture") {
        return RunProcessTreeParentFixture();
    }
    if (argc >= 2 && std::string(argv[1]) == "--process-tree-child-fixture") {
        return RunProcessTreeChildFixture();
    }

    TestAppPaths();
    TestDownloadQueueStoreRoundTripSnapshots();
    TestDownloadQueueStoreSkipsInvalidEntries();
    TestConfigDefaultsAndRoundTrip();
    TestConfigDropsLegacyFfmpegPromptFlag();
    TestMainWindowShortcutResolution();
    TestDownloadAttemptResolution();
    TestWhisperUtilityStatusText();
    TestEditContextMenuModel();
    TestPasteReplacesExistingEditText();
    TestPasteReplacingEditTextIgnoresInvalidHandles();
    TestModalOwnerRestorationPreservesEnabledState();
    TestRestoreModalOwnerIgnoresInvalidHandles();
    TestConfigParallelDownloadBounds();
    TestConfigUtf8RoundTrip();
    TestLoggerTruncatesAtStartupAndAppendsWithinRun();
    TestLoggerReadsCurrentLogText();
    TestCommitDownloadedFilePreservesTargetUntilValidated();
    TestWaitForDelayCompletesOrStopsPromptly();
    TestProgressPresentation();
    TestVersionCompare();
    TestYtDlpDownloadArguments();
    TestYtDlpOutputPathParsing();
    TestYtDlpOutputFileFallbackFindsNewMedia();
    TestTranscriptionCommandArguments();
    TestVoiceOverCommandArguments();
    TestTranscriptionUsesAsciiTemporaryWhisperPaths();
    TestWhisperProgressParsing();
    TestTranscriptionErrorSummaryUsesLastMeaningfulLine();
    TestYtDlpProgressParsing();
    TestYtDlpProcessLineParsing();
    TestGitHubReleaseParsing();
    TestWhisperReleaseParsing();
    TestWhisperBackendResolution();
    TestWhisperResolutionHonorsLegacyCpuSelectionAlongsideCuda();
    TestWhisperModelCatalog();
    TestWhisperExecutableDiscovery();
    TestVotCliInstallInvocation();
    TestTranscriptTextPathResolution();
    TestVoiceOverVideoPathResolution();
    TestYtDlpUpdateDecision();
    TestYtDlpExecutableVersionValidation();
    TestAppUpdateDecision();
    TestAppUpdatePromptMessage();
    TestFfmpegResolutionPrecedence();
    TestFfmpegUserPathAndExtractedTreeResolution();
    TestProcessRunnerCapturesOutputAndExitCode();
    TestProcessRunnerEmitsEachOutputLineOnce();
    TestProcessRunnerCancelKillsChildProcessTree();
    TestYtDlpMetadataParsing();
    TestDownloadQueueSchedulingAndRetry();
    TestDownloadQueuePostProcessingStatus();
    TestDownloadQueueCompletedTaskShowsElapsedTime();
    TestDownloadQueueProgressResetsForNextDownloadTrack();
    TestDownloadQueueStartsPostProcessingForCompletedTask();
    TestDownloadQueueLogsTaskLifecycle();
    TestDownloadQueueLogsPostProcessingProgress();
    TestDownloadQueueShutdownWaitsForActiveWorker();
    TestDownloadQueueUpdatesParallelismAtRuntime();
    TestDownloadQueueRejectsDuplicateVisibleUrl();
    TestDownloadQueueAllowsDuplicateUrlAfterCompletion();
    TestDownloadQueueEnrichesDuplicateVisibleUrl();
    TestDownloadQueueIgnoresOutputLinesWithoutPathsForRevision();
    TestDownloadQueueEnrichesExistingTaskAfterPreviewCompletes();
    TestDownloadQueueCancelAndDeleteTask();
    TestDownloadQueueCloseCompletedTaskKeepsOutputFile();
    TestDownloadQueueDeleteCanceledTaskRemovesPartialFile();
    TestDownloadQueueDeleteCanceledTaskHandlesIndentedDestinationAndFragments();
    TestDownloadQueueDeleteCanceledTaskFindsPartFileByVideoIdWhenDestinationWasNotCaptured();
    TestDownloadQueueClearQueuedOnlyRemovesWaitingTasks();
    TestDownloadQueueClearFinishedKeepsQueuedTasks();
    TestDownloadQueueDownloadedBytesDoNotMoveBackward();
    TestDownloadQueueProgressPercentDoesNotMoveBackwardWithinTrack();
    TestDownloadQueueIgnoresStaleVideoProgressAfterAudioStarts();
    TestDownloadQueueIgnoresUnclassifiedFinishedProgressAfterAudioStarts();
    TestDownloadQueueClearFinishedDeletesInvalidPartFilesOnly();
    TestDownloadQueueImportsRestoredTasksWithoutStartingThem();
    TestDownloadQueueExportForShutdownStopsActiveTasks();
    TestDownloadQueueExportSkipsCompletedTasks();
    TestDownloadQueueDeletedTaskIsNotExported();
    return 0;
}

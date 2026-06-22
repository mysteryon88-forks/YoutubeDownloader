# UI Behavior Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the obsolete FFmpeg prompt flag, eliminate modal-owner flashing, guard download attempts while yt-dlp is unavailable, and make every URL paste replace the current field contents.

**Architecture:** Keep configuration cleanup inside `ConfigStore`, and extract the small Win32 interaction policies into a focused `UiActions` unit shared by `Application.cpp`, `DialogWindows.cpp`, and the test executable. The existing modal dialog and download queue remain unchanged; only their entry and restoration behavior changes.

**Tech Stack:** C++20, Win32 API, CMake/Visual Studio 2022, existing custom assertion test executable and CTest.

---

## File structure

- Create `src/UI/UiActions.h`: declarations for download-attempt resolution, replacement paste, and modal-owner restoration.
- Create `src/UI/UiActions.cpp`: testable Win32 interaction implementations.
- Modify `src/Core/Config.h`: remove the obsolete configuration member.
- Modify `src/Core/Config.cpp`: stop reading and writing the obsolete JSON key.
- Modify `src/UI/Application.cpp`: use replacement paste, keep Download clickable, and show the yt-dlp readiness dialog.
- Modify `src/UI/DialogWindows.cpp`: restore modal owners without forced Z-order changes and remove skip-flag handling.
- Modify `tests/test_core.cpp`: add regression tests for all nonvisual behavior and remove obsolete flag assertions.
- Modify `CMakeLists.txt`: compile `UiActions.cpp` into both the application and test executable.

### Task 1: Remove `ffmpegPromptDismissed`

**Files:**
- Modify: `tests/test_core.cpp`
- Modify: `src/Core/Config.h`
- Modify: `src/Core/Config.cpp`
- Modify: `src/UI/DialogWindows.cpp`

- [ ] **Step 1: Add a failing legacy-key removal test**

Add this test to `tests/test_core.cpp` and call it from `main()`:

```cpp
void TestConfigDropsLegacyFfmpegPromptFlag() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_LegacyFfmpegPrompt");
    const AppPaths paths(root);
    fs::create_directories(paths.configPath().parent_path());
    {
        std::ofstream out(paths.configPath());
        out << R"json({"ffmpeg_prompt_dismissed":true,"quality":"720p"})json";
    }

    const AppConfig loaded = ConfigStore::Load(paths);
    Require(loaded.quality == L"720p", "legacy config should still load");
    ConfigStore::Save(paths, loaded);

    std::ifstream in(paths.configPath());
    const std::string saved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Require(saved.find("ffmpeg_prompt_dismissed") == std::string::npos,
            "legacy ffmpeg prompt flag should be removed on save");
}
```

- [ ] **Step 2: Build and verify RED**

Run:

```powershell
cmake --build build --config Debug --target YoutubeDownloaderTests
& .\build\bin\Debug\YoutubeDownloaderTests.exe
```

Expected: the executable fails with `legacy ffmpeg prompt flag should be removed on save`.

- [ ] **Step 3: Remove the obsolete field and persistence**

Delete `ffmpegPromptDismissed` from `AppConfig`, delete the `ffmpeg_prompt_dismissed` load/save statements, and remove the default/round-trip assertions and assignment from `TestConfigDefaultsAndRoundTrip`.

Change the FFmpeg Skip branch to close without mutating configuration:

```cpp
case IdSkip:
    DestroyWindow(window);
    return 0;
```

Separate the context check from the dialog result so Skip does not open a second fallback dialog:

```cpp
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
    } else {
        ShowFfmpegDialog(window, state->instance);
    }
    return 0;
```

- [ ] **Step 4: Build and verify GREEN**

Run:

```powershell
cmake --build build --config Debug --target YoutubeDownloaderTests YoutubeDownloader
& .\build\bin\Debug\YoutubeDownloaderTests.exe
```

Expected: build succeeds and the test executable exits with code 0.

- [ ] **Step 5: Commit**

```powershell
git add src/Core/Config.h src/Core/Config.cpp src/UI/DialogWindows.cpp tests/test_core.cpp
git commit -m "Remove obsolete FFmpeg prompt flag"
```

### Task 2: Add testable UI actions

**Files:**
- Create: `src/UI/UiActions.h`
- Create: `src/UI/UiActions.cpp`
- Modify: `tests/test_core.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Declare the intended UI-action API and write failing tests**

Create `src/UI/UiActions.h`:

```cpp
#pragma once

#include <windows.h>

enum class DownloadAttemptAction {
    Enqueue,
    ShowYtDlpNotReady
};

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady);
void PasteReplacingEditText(HWND editControl);
void RestoreModalOwner(HWND owner, bool ownerWasEnabled);
```

Add `src/UI` to the test include path in `CMakeLists.txt`, include `UiActions.h` and `<cstring>` in `tests/test_core.cpp`, then add and call these tests:

```cpp
void TestDownloadAttemptResolution() {
    Require(ResolveDownloadAttempt(false) == DownloadAttemptAction::ShowYtDlpNotReady,
            "download should be blocked until yt-dlp is ready");
    Require(ResolveDownloadAttempt(true) == DownloadAttemptAction::Enqueue,
            "download should proceed when yt-dlp is ready");
}

void TestPasteReplacesExistingEditText() {
    Require(OpenClipboard(nullptr) != FALSE, "failed to open clipboard for paste test");
    EmptyClipboard();
    constexpr wchar_t replacement[] = L"https://example.com/new";
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(replacement));
    Require(memory != nullptr, "failed to allocate clipboard memory");
    void* target = GlobalLock(memory);
    Require(target != nullptr, "failed to lock clipboard memory");
    std::memcpy(target, replacement, sizeof(replacement));
    GlobalUnlock(memory);
    Require(SetClipboardData(CF_UNICODETEXT, memory) != nullptr, "failed to set clipboard text");
    CloseClipboard();

    HWND edit = CreateWindowExW(0, L"EDIT", L"https://example.com/old", 0,
                                0, 0, 320, 24, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(edit != nullptr, "failed to create edit control");
    PasteReplacingEditText(edit);
    wchar_t text[128] = {};
    GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
    DestroyWindow(edit);
    Require(std::wstring(text) == replacement, "paste should replace the complete URL");
}

void TestModalOwnerRestorationPreservesEnabledState() {
    HWND owner = CreateWindowExW(0, L"STATIC", L"owner", 0,
                                 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Require(owner != nullptr, "failed to create modal owner fixture");

    EnableWindow(owner, FALSE);
    RestoreModalOwner(owner, false);
    Require(IsWindowEnabled(owner) == FALSE, "previously disabled modal owner must stay disabled");

    RestoreModalOwner(owner, true);
    Require(IsWindowEnabled(owner) != FALSE, "previously enabled modal owner must be restored");
    DestroyWindow(owner);
}
```

- [ ] **Step 2: Build and verify RED**

Run:

```powershell
cmake --build build --config Debug --target YoutubeDownloaderTests
```

Expected: link fails because `ResolveDownloadAttempt`, `PasteReplacingEditText`, and `RestoreModalOwner` are not implemented.

- [ ] **Step 3: Implement the minimal UI actions and wire the source into CMake**

Create `src/UI/UiActions.cpp`:

```cpp
#include "UiActions.h"

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady) {
    return ytDlpReady ? DownloadAttemptAction::Enqueue : DownloadAttemptAction::ShowYtDlpNotReady;
}

void PasteReplacingEditText(HWND editControl) {
    if (!editControl || !IsWindow(editControl)) {
        return;
    }
    SetFocus(editControl);
    SendMessageW(editControl, EM_SETSEL, 0, -1);
    SendMessageW(editControl, WM_PASTE, 0, 0);
}

void RestoreModalOwner(HWND owner, bool ownerWasEnabled) {
    if (!owner || !IsWindow(owner) || !ownerWasEnabled) {
        return;
    }
    EnableWindow(owner, TRUE);
    if (IsIconic(owner)) {
        ShowWindow(owner, SW_RESTORE);
    }
    SetActiveWindow(owner);
}
```

Add `src/UI/UiActions.cpp` and `src/UI/UiActions.h` to `YoutubeDownloader`, add `src/UI/UiActions.cpp` to `YoutubeDownloaderTests`, add `${CMAKE_CURRENT_SOURCE_DIR}/src/UI` to the test include directories, and link `user32` to the test target.

- [ ] **Step 4: Build and verify GREEN**

Run:

```powershell
cmake --build build --config Debug --target YoutubeDownloaderTests
& .\build\bin\Debug\YoutubeDownloaderTests.exe
```

Expected: build succeeds and all tests exit with code 0.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt src/UI/UiActions.h src/UI/UiActions.cpp tests/test_core.cpp
git commit -m "Add testable Win32 UI actions"
```

### Task 3: Wire paste, download guard, and modal restoration

**Files:**
- Modify: `src/UI/Application.cpp`
- Modify: `src/UI/DialogWindows.cpp`

- [ ] **Step 1: Replace both paste paths with the shared action**

Include `UiActions.h`. In `HandleMainWindowShortcut`, replace the focus and `WM_PASTE` calls with:

```cpp
PasteReplacingEditText(m_urlEdit);
return true;
```

In the Paste button command, replace direct `WM_PASTE` and the duplicate explicit preview start with:

```cpp
PasteReplacingEditText(m_urlEdit);
return 0;
```

The edit control's synchronous `EN_CHANGE` notification will invoke `StartPreviewFetch` once.

- [ ] **Step 2: Keep Download clickable and route attempts through the readiness policy**

Remove `EnableWindow(m_downloadButton, FALSE)` from `CreateControls`, and remove both Download-button `EnableWindow` calls from the tool-check completion handler.

At the start of `EnqueueCurrentUrl`, use:

```cpp
if (ResolveDownloadAttempt(m_ytDlpReady) == DownloadAttemptAction::ShowYtDlpNotReady || !m_downloadQueue) {
    ShowErrorDialog(
        m_window,
        m_instance,
        L"yt-dlp ещё не готов",
        L"Дождитесь завершения проверки, установки или обновления yt-dlp."
    );
    return;
}
```

- [ ] **Step 3: Restore modal owners without forced Z-order repaint**

Include `UiActions.h` in `DialogWindows.cpp`. Change `RunModal` to capture and restore the prior enabled state:

```cpp
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
```

This removes `BringWindowToTop` entirely.

- [ ] **Step 4: Run the regression suite**

Run:

```powershell
cmake --build build --config Debug --target YoutubeDownloaderTests YoutubeDownloader
ctest --test-dir build -C Debug --output-on-failure
```

Expected: build succeeds and `YoutubeDownloaderCoreTests` passes.

- [ ] **Step 5: Commit**

```powershell
git add src/UI/Application.cpp src/UI/DialogWindows.cpp
git commit -m "Fix modal restore and guarded URL actions"
```

### Task 4: Full verification

**Files:**
- Verify: all modified production and test files

- [ ] **Step 1: Build and test Debug**

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Expected: all targets build and all tests pass.

- [ ] **Step 2: Build and test Release**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: all targets build and all tests pass.

- [ ] **Step 3: Check source and diff consistency**

```powershell
rg -n "ffmpegPromptDismissed|ffmpeg_prompt_dismissed|BringWindowToTop" src tests
git diff --check
git status --short
```

Expected: `rg` finds no obsolete flag or forced owner raise; `git diff --check` reports no whitespace errors; status contains only intended files if commits were intentionally deferred.

- [ ] **Step 4: Exercise native UI behavior**

Run `build\bin\Debug\YoutubeDownloader.exe` and verify:

1. opening and closing Settings, About, FFmpeg, and nested dialogs does not flash the owner background;
2. clicking Download before yt-dlp is ready shows the custom modal error with an OK button and creates no queue item;
3. the Paste button replaces an existing URL;
4. `Ctrl+V` replaces an existing URL;
5. closing the FFmpeg dialog with Skip does not immediately open another FFmpeg dialog.

- [ ] **Step 5: Review final history and worktree**

```powershell
git log -5 --oneline
git status --short
```

Expected: the implementation commits are present and the worktree is clean.

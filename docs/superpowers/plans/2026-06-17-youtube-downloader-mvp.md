# YouTube Downloader MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a portable C++ Win32 YouTube Downloader that mirrors the existing Python app's core behavior while using external `yt-dlp.exe` and optional `ffmpeg`.

**Architecture:** The app is a Win32/GDI+ desktop program with a small testable core for paths, config, version comparison, command construction, HTTP downloads, process execution, and queue state. UI code follows the FileRenamer style and uses dark-mode helpers adapted from win32-darkmode; external tools live under `tools\`, runtime state under `stuff\`.

**Tech Stack:** CMake, C++20, MSVC/Visual Studio 2022, Win32 API, GDI+, WinHTTP, `nlohmann/json.hpp`, CTest.

---

## File Structure

- Create `CMakeLists.txt`: CMake project, app target, unit-test target, Windows libraries.
- Create `src/main.cpp`: `wWinMain` entrypoint.
- Create `src/Application.h` and `src/Application.cpp`: main Win32 window, message loop, UI wiring.
- Create `src/UiRenderer.h` and `src/UiRenderer.cpp`: GDI+ drawing for dark/red controls, cards, progress bars.
- Create `src/ToolTip.h` and `src/ToolTip.cpp`: tooltip relay helper adapted from FileRenamer.
- Create `src/AppPaths.h` and `src/AppPaths.cpp`: strict portable directory layout.
- Create `src/Config.h` and `src/Config.cpp`: JSON-backed `stuff\config.ini` load/save with defaults.
- Create `src/Version.h` and `src/Version.cpp`: semantic-ish version parsing and comparison.
- Create `src/Logger.h` and `src/Logger.cpp`: append UTF-8 logs to `stuff\ytdl.log`.
- Create `src/WinHttpClient.h` and `src/WinHttpClient.cpp`: GET string/file downloads with progress and cancel checks.
- Create `src/ProcessRunner.h` and `src/ProcessRunner.cpp`: hidden child process execution with stdout/stderr pipes.
- Create `src/ToolManagers.h` and `src/ToolManagers.cpp`: `YtDlpManager`, `FfmpegManager`, `AppUpdateService`.
- Create `src/YtDlpClient.h` and `src/YtDlpClient.cpp`: preview, playlist expansion, command construction, progress parsing.
- Create `src/DownloadQueue.h` and `src/DownloadQueue.cpp`: global max parallel downloads, task lifecycle.
- Create `src/DarkMode.h`, `src/IatHook.h`, `src/ListViewUtil.h`: adapted dark-mode helpers.
- Create `src/resource.h`, `src/resource.rc`, `assets/icon.ico`: app icon and resources.
- Create `tests/test_core.cpp`: unit tests for paths, config, versions, command construction, progress parsing.
- Vendor `third_party/nlohmann/json.hpp`: single-header JSON parser.

## Agreed Product Decisions

- Strictly portable app: no `%AppData%` fallback.
- Runtime layout:

```text
YoutubeDownloader.exe
assets\
stuff\
  config.ini
  ytdl.log
  thumb_cache\
tools\
  yt-dlp\
    yt-dlp.exe
    version.txt
  ffmpeg\
    bin\
      ffmpeg.exe
      ffprobe.exe
      ffplay.exe
```

- `yt-dlp` is not bundled; install/update from official `yt-dlp/yt-dlp` GitHub releases.
- `yt-dlp` readiness gates downloads; installation/checks run asynchronously.
- `yt-dlp` update check runs at most once per 24 hours, tracked in config.
- `ffmpeg` is optional; search PATH, then user path, then local install.
- If `ffmpeg` is missing, show `Install / Choose path / Skip`; missing `ffmpeg` limits quality/container behavior.
- First version task controls: cancel, retry, delete files; no real pause/resume.
- Quality modes: `audio`, `360p`, `480p`, `720p`, `1080p`, `max`.
- Containers: `auto`, `mp4`, `mkv`, `webm`; explicit containers require `ffmpeg`.
- Playlist expansion uses `yt-dlp --flat-playlist --dump-single-json`.
- Global parallel downloads default to `3`, saved as `max_parallel_downloads`.
- Preview uses `yt-dlp --dump-single-json --no-playlist`, thumbnail cache in `stuff\thumb_cache`.
- Cookies support is minimal: `cookies_path` setting and `--cookies <path>` when file exists.
- App update target: `Laynholt/YoutubeDownloader`, asset `YoutubeDownloader_windows.zip`.
- App updater preserves `tools\` and `stuff\`.
- Output template: `%(title).200s [%(id)s].%(ext)s`.
- Visual direction: FileRenamer code patterns, win32-darkmode system styling, `4_signal_red.html` visual language.

## Task 1: Core Test Harness and Portable Paths

**Files:**
- Create: `CMakeLists.txt`
- Create: `src/AppPaths.h`
- Create: `src/AppPaths.cpp`
- Create: `tests/test_core.cpp`

- [ ] **Step 1: Write failing path tests**

```cpp
#include "AppPaths.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

int main() {
    const fs::path root = fs::path("C:/Portable/YoutubeDownloader");
    const AppPaths paths(root);
    require(paths.root() == root, "root path mismatch");
    require(paths.assetsDir() == root / "assets", "assets path mismatch");
    require(paths.stuffDir() == root / "stuff", "stuff path mismatch");
    require(paths.configPath() == root / "stuff" / "config.ini", "config path mismatch");
    require(paths.logPath() == root / "stuff" / "ytdl.log", "log path mismatch");
    require(paths.thumbCacheDir() == root / "stuff" / "thumb_cache", "thumb cache path mismatch");
    require(paths.toolsDir() == root / "tools", "tools path mismatch");
    require(paths.ytDlpExePath() == root / "tools" / "yt-dlp" / "yt-dlp.exe", "yt-dlp path mismatch");
    require(paths.ytDlpVersionPath() == root / "tools" / "yt-dlp" / "version.txt", "yt-dlp version path mismatch");
    require(paths.localFfmpegBinDir() == root / "tools" / "ffmpeg" / "bin", "ffmpeg bin path mismatch");
    require(paths.localFfmpegExePath() == root / "tools" / "ffmpeg" / "bin" / "ffmpeg.exe", "ffmpeg path mismatch");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target YoutubeDownloaderTests
ctest --test-dir build -C Debug --output-on-failure
```

Expected: configure or compile fails because `AppPaths` does not exist.

- [ ] **Step 3: Implement `AppPaths`**

Create `AppPaths` with `std::filesystem::path` getters exactly matching the assertions above.

- [ ] **Step 4: Run test to verify it passes**

Run the same CMake/build/CTest commands.

Expected: `100% tests passed`.

## Task 2: Config, Versions, and Atomic Save

**Files:**
- Create: `src/Config.h`
- Create: `src/Config.cpp`
- Create: `src/Version.h`
- Create: `src/Version.cpp`
- Modify: `tests/test_core.cpp`
- Vendor: `third_party/nlohmann/json.hpp`

- [ ] **Step 1: Add failing tests for defaults, round-trip, and version compare**

Add tests that assert:
- default `download_dir` is the user's Downloads path when no config file exists;
- default `quality` is `max`;
- default `container` is `auto`;
- default `max_parallel_downloads` is `3`;
- saving then loading preserves `cookies_path`, `ffmpeg_path`, `auto_update`, and `last_ytdlp_version`;
- `CompareVersions("2026.06.09", "2026.06.10") < 0`;
- `CompareVersions("1.2.0", "1.2") == 0`.

- [ ] **Step 2: Run test to verify it fails**

Expected: compile fails because `Config` and `Version` do not exist.

- [ ] **Step 3: Implement config and versions**

Use `nlohmann::json`, `std::filesystem`, and write to `config.ini.tmp` before `replace`.

- [ ] **Step 4: Run tests to verify pass**

Expected: CTest passes.

## Task 3: Command Construction and Progress Parsing

**Files:**
- Create: `src/YtDlpClient.h`
- Create: `src/YtDlpClient.cpp`
- Modify: `tests/test_core.cpp`

- [ ] **Step 1: Add failing tests for `yt-dlp` arguments**

Assert command args include:
- `--cookies <path>` only when cookies file exists;
- `--ffmpeg-location <dir>` when ffmpeg is available;
- `--merge-output-format mp4` for container `mp4`;
- format string `bestaudio/best` for `audio`;
- format string with `height<=720` for `720p`;
- output template `%(title).200s [%(id)s].%(ext)s`.

- [ ] **Step 2: Add failing tests for progress parsing**

Use a machine-readable progress line and assert status, percent, speed, eta, and total are extracted.

- [ ] **Step 3: Implement minimal command builder and progress parser**

Keep process launching out of this task; return vectors and parsed structs.

- [ ] **Step 4: Run tests**

Expected: CTest passes.

## Task 4: WinHTTP, Process Runner, and Tool Managers

**Files:**
- Create: `src/WinHttpClient.h`
- Create: `src/WinHttpClient.cpp`
- Create: `src/ProcessRunner.h`
- Create: `src/ProcessRunner.cpp`
- Create: `src/ToolManagers.h`
- Create: `src/ToolManagers.cpp`
- Modify: `tests/test_core.cpp`

- [ ] **Step 1: Add isolated tests for version response parsing**

Use fixture JSON strings for GitHub release responses and assert tag/asset URL extraction.

- [ ] **Step 2: Implement HTTP and process wrappers**

Use RAII handles, hidden windows, UTF-8 output capture, cancel event checks.

- [ ] **Step 3: Implement managers**

`YtDlpManager` installs to `tools\yt-dlp`; `FfmpegManager` finds PATH/user/local; `AppUpdateService` parses `Laynholt/YoutubeDownloader` releases.

- [ ] **Step 4: Run tests**

Expected: CTest passes.

## Task 5: Win32 UI Shell

**Files:**
- Create: `src/main.cpp`
- Create: `src/Application.h`
- Create: `src/Application.cpp`
- Create: `src/UiRenderer.h`
- Create: `src/UiRenderer.cpp`
- Create: `src/ToolTip.h`
- Create: `src/ToolTip.cpp`
- Create: `src/resource.h`
- Create: `src/resource.rc`
- Create: `assets/icon.ico`

- [ ] **Step 1: Scaffold app window**

Create a 1000x640 dark Win32 window with URL entry, paste button, preview panel, folder chooser, download button, clear queue button, settings button, queue list area.

- [ ] **Step 2: Adapt owner-draw styles**

Use GDI+ buttons/cards/progress bars with Signal Red accent.

- [ ] **Step 3: Build**

Run:

```powershell
cmake --build build --config Debug --target YoutubeDownloader
```

Expected: app target builds.

## Task 6: Wire Preview, Settings, Queue, and Updates

**Files:**
- Modify: `src/Application.cpp`
- Modify: `src/Application.h`
- Modify: `src/DownloadQueue.h`
- Modify: `src/DownloadQueue.cpp`
- Modify: `src/YtDlpClient.cpp`
- Modify: `src/ToolManagers.cpp`

- [ ] **Step 1: Implement first-run dependency flow**

Open window immediately; disable downloads until `yt-dlp` is ready; show progress for first install.

- [ ] **Step 2: Implement optional ffmpeg dialog**

Show install/choose/skip when not found; persist dismissal; leave settings button for later install.

- [ ] **Step 3: Implement preview and queue**

Background JSON metadata fetch, stale result suppression, thumbnail cache, playlist expansion, max parallel downloads.

- [ ] **Step 4: Implement app update UI**

Manual check button plus optional auto-check setting; preserve `tools\` and `stuff\`.

- [ ] **Step 5: Full verification**

Run build, CTest, first-run dependency smoke test with a clean portable directory, and a no-network failure-path smoke test.

## Self-Review

- Spec coverage: portable layout, external tool install/update, ffmpeg optional flow, Python quality/container/cookies behavior, playlist batching/parallel limit, preview, update UI, visual direction, and MSVC/CMake target are all mapped to tasks.
- Placeholder scan: no `TBD`, `TODO`, or unconstrained "handle edge cases" steps remain.
- Type consistency: the named components are introduced before downstream tasks reference them.

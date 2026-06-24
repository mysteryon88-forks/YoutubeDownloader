# Reliability and Code Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the identified concurrency, process-output, file-integrity, runtime-settings, and updater defects; activate per-run logging; and remove dead or duplicated code.

**Architecture:** Long-lived work is owned by `std::jthread`; Win32 cancellation is bridged from `std::stop_token` to events only at API boundaries. File downloads are staged and validated before an atomic/preserving replacement. `DownloadQueue` owns one executor implementation, its workers, dynamic concurrency, and task lifecycle logging.

**Tech Stack:** C++20, Win32, WinHTTP, CMake/MSVC, the existing `YoutubeDownloaderTests` executable.

---

## File map

- `src/Core/FileOperations.h/.cpp`: validated staged-file commit and target-preserving replacement.
- `src/Core/AsyncWait.h/.cpp`: cancelable delay used by preview debounce.
- `src/Core/Logger.h/.cpp`: truncate-on-start UTF-8 logger.
- `src/Platform/ProcessRunner.cpp`: exact-once line emission.
- `src/Platform/WinHttpClient.cpp`: staged downloads, write/length validation, cancellation cleanup, URL hardening.
- `src/Core/DownloadQueue.h/.cpp`: owned `std::jthread` workers, stop-token executor, dynamic concurrency, terminal logging.
- `src/UI/Application.h/.cpp`: owned background operations, debounced preview, mailbox-style results, runtime setting updates, logging.
- `src/Core/AppPaths.h/.cpp`, `src/Core/ToolManagers.h/.cpp`, `src/UI/DialogWindows.h/.cpp`: safe updater integration and dead API removal.
- `tests/test_core.cpp`: regression tests for every non-UI behavior.
- `CMakeLists.txt`: register new core files.

### Task 1: Create an isolated execution workspace and verify baseline

**Files:** None.

- [ ] **Step 1: Detect worktree state and confirm `.worktrees` is ignored**

Run:

```powershell
git rev-parse --git-dir
git rev-parse --git-common-dir
git rev-parse --show-superproject-working-tree
git check-ignore .worktrees
```

Expected: the current checkout is not already linked, it is not a submodule, and `.worktrees` is ignored.

- [ ] **Step 2: Create the implementation worktree**

Run:

```powershell
git worktree add .worktrees/reliability-cleanup -b codex/reliability-cleanup
```

Expected: a clean worktree on `codex/reliability-cleanup` containing the approved design and this plan.

- [ ] **Step 3: Configure, build, and test the baseline**

Run inside the worktree:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: Release build succeeds and `YoutubeDownloaderCoreTests` passes.

### Task 2: Make the logger a real per-run facility

**Files:**
- Modify: `src/Core/Logger.cpp`
- Modify: `src/UI/Application.cpp`
- Test: `tests/test_core.cpp`

- [ ] **Step 1: Write the failing logger lifecycle test**

Add `TestLoggerTruncatesAtStartupAndAppendsWithinRun()` which writes `old-run`, constructs `Logger`, asserts the file is empty, calls `Info(L"startup")` and `Error(L"failure")`, then asserts the file contains both new records and not `old-run`:

```cpp
void TestLoggerTruncatesAtStartupAndAppendsWithinRun() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_Logger");
    const AppPaths paths(root);
    fs::create_directories(paths.stuffDir());
    { std::ofstream old(paths.logPath()); old << "old-run"; }

    Logger logger(paths);
    {
        std::ifstream truncated(paths.logPath(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(truncated), {}};
        Require(text.empty(), "logger should truncate the previous run");
    }

    logger.Info(L"startup");
    logger.Error(L"failure");
    std::ifstream current(paths.logPath(), std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(current), {}};
    Require(text.find("old-run") == std::string::npos, "old log content should be removed");
    Require(text.find("startup") != std::string::npos, "startup log entry missing");
    Require(text.find("failure") != std::string::npos, "error log entry missing");
}
```

- [ ] **Step 2: Run tests and verify RED**

Run: `cmake --build build --config Release; ctest --test-dir build -C Release --output-on-failure`

Expected: failure `logger should truncate the previous run`.

- [ ] **Step 3: Truncate the log in the constructor**

In `Logger::Logger(std::filesystem::path)`, create the parent directory and open the file with `std::ios::trunc`. Keep `Append` mutex-protected and append-only after construction.

- [ ] **Step 4: Add application lifecycle log calls**

Immediately after constructing `m_logger`, log startup and resolved root. In idempotent shutdown, log orderly shutdown before releasing the logger. Do not log raw yt-dlp output.

- [ ] **Step 5: Run tests and verify GREEN**

Run: `cmake --build build --config Release; ctest --test-dir build -C Release --output-on-failure`

Expected: all tests pass.

- [ ] **Step 6: Commit**

```powershell
git add src/Core/Logger.cpp src/UI/Application.cpp tests/test_core.cpp
git commit -m "feat: initialize a fresh application log per run"
```

### Task 3: Emit process output lines exactly once

**Files:**
- Modify: `src/Platform/ProcessRunner.cpp`
- Test: `tests/test_core.cpp`

- [ ] **Step 1: Write the failing multi-chunk callback test**

Run `cmd.exe /C for /L %i in (1,1,1000) do @echo line-%i`, collect callback lines, and assert exactly 1,000 callbacks with `line-1` and `line-1000` each occurring once.

```cpp
void TestProcessRunnerEmitsEachOutputLineOnce() {
    std::vector<std::wstring> lines;
    ProcessRunOptions options;
    options.executable = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = {L"/C", L"for /L %i in (1,1,1000) do @echo line-%i"};
    options.timeoutMs = 10000;
    options.onStdoutLine = [&](const std::wstring& line) { if (!line.empty()) lines.push_back(line); };

    const ProcessRunResult result = ProcessRunner::Run(options);
    Require(result.exitCode == 0, "line fixture process failed");
    Require(lines.size() == 1000, "stdout callback should emit each line once");
    Require(std::ranges::count(lines, L"line-1") == 1, "first line was duplicated");
    Require(std::ranges::count(lines, L"line-1000") == 1, "last line was duplicated");
}
```

- [ ] **Step 2: Run and verify RED**

Expected: `stdout callback should emit each line once`.

- [ ] **Step 3: Preserve decoder and line offsets across reads**

Keep one `emittedOffset` outside the `ReadFile` loop. Decode accumulated bytes, call `EmitLines(target, emittedOffset, callback)`, and after EOF emit only `target.substr(emittedOffset)` when non-empty. Do not reset the offset or re-emit the full buffer.

- [ ] **Step 4: Run and verify GREEN, then commit**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git add src/Platform/ProcessRunner.cpp tests/test_core.cpp
git commit -m "fix: emit process output lines exactly once"
```

### Task 4: Own queue workers and apply concurrency changes immediately

**Files:**
- Modify: `src/Core/DownloadQueue.h`
- Modify: `src/Core/DownloadQueue.cpp`
- Modify: `src/UI/Application.cpp`
- Test: `tests/test_core.cpp`

- [ ] **Step 1: Write the failing shutdown-waits test**

Create a queue whose executor sets `started`, sleeps briefly, sets `finished`, and ignores stop. Destroy the queue after `started`; assert `finished` is true after destruction.

```cpp
void TestDownloadQueueShutdownWaitsForActiveWorker() {
    std::atomic<bool> started = false;
    std::atomic<bool> finished = false;
    {
        DownloadQueue queue(1);
        queue.SetExecutor([&](const DownloadTaskSnapshot&, std::stop_token, const DownloadTaskCallbacks&) {
            started = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            finished = true;
            return DownloadTaskResult{false, L"stopped", {}};
        });
        YtDlpDownloadRequest request;
        request.url = L"https://example.invalid/shutdown";
        queue.Enqueue(request, L"Shutdown");
        while (!started.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    Require(finished.load(), "queue destruction should join active workers");
}
```

- [ ] **Step 2: Write the failing dynamic-concurrency test**

Start with limit 1 and three blocking tasks, call `SetMaxParallelDownloads(2)`, assert two tasks become active, lower to 1, release one task, and assert no third task starts until active count drops below one.

- [ ] **Step 3: Run and verify RED**

Expected: compilation fails because the executor lacks `std::stop_token` and `SetMaxParallelDownloads` does not exist.

- [ ] **Step 4: Replace detached workers with owned `std::jthread` objects**

Change the executor contract to:

```cpp
using DownloadTaskExecutor = std::function<DownloadTaskResult(
    const DownloadTaskSnapshot&,
    std::stop_token,
    const DownloadTaskCallbacks&
)>;
```

Store active workers in `std::map<int, std::jthread> m_workers`. The scheduler reaps completed workers from a finished-ID list. `Cancel` calls `request_stop()` for the matching active worker. `Shutdown` marks shutdown, requests all worker stops, joins the scheduler, moves workers out under the mutex, releases the mutex, and lets owned `jthread` instances join before returning.

- [ ] **Step 5: Add runtime concurrency setter**

```cpp
void DownloadQueue::SetMaxParallelDownloads(int value) {
    {
        std::lock_guard lock(m_mutex);
        m_maxParallelDownloads = std::max(1, value);
    }
    m_cv.notify_all();
}
```

Call it after settings are saved. Active work may temporarily exceed a reduced limit; no new work starts until permitted.

- [ ] **Step 6: Consolidate the executor**

Move the stop-event bridge currently in `Application::InitializeBackend` into `DownloadQueue::DefaultExecutor`. Use `std::stop_callback` to set the event. Remove `DownloadTaskCallbacks::isCanceled`, the unused `onProgress` callback, and the application `SetExecutor` duplicate.

- [ ] **Step 7: Run and verify GREEN, then commit**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git add src/Core/DownloadQueue.h src/Core/DownloadQueue.cpp src/UI/Application.cpp tests/test_core.cpp
git commit -m "fix: own download workers and update concurrency live"
```

### Task 5: Validate downloads and preserve existing targets

**Files:**
- Create: `src/Core/FileOperations.h`
- Create: `src/Core/FileOperations.cpp`
- Modify: `src/Platform/WinHttpClient.cpp`
- Modify: `src/Core/ToolManagers.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/test_core.cpp`

- [ ] **Step 1: Write failing file-commit tests**

Declare the wished-for API:

```cpp
void CommitDownloadedFile(
    const std::filesystem::path& staged,
    const std::filesystem::path& target,
    std::uint64_t downloaded,
    std::uint64_t expectedSize
);
```

Test that a short staged file throws, deletes the staged file, and leaves existing target contents unchanged. Test that a complete staged file replaces the target.

- [ ] **Step 2: Run and verify RED**

Expected: link failure for `CommitDownloadedFile`.

- [ ] **Step 3: Implement target-preserving commit**

Validate `expectedSize == 0 || downloaded == expectedSize`. For an existing target, use `ReplaceFileW` with write-through semantics; for a missing target, use `MoveFileExW(..., MOVEFILE_WRITE_THROUGH)`. On failure, remove only the staged file and throw. Never delete the working target first.

- [ ] **Step 4: Make WinHTTP download through a staged file**

Use `<target>.download` in the same directory. Check `create_directories`, `out.write`, `out.flush`, and final stream state. On every exception remove the staged file. Call `CommitDownloadedFile` only after successful close and length validation. Harden `CrackUrl` by rejecting missing host/path pointers before assigning them.

- [ ] **Step 5: Update tool installers**

Download yt-dlp into `yt-dlp.exe.new`, then commit it to `yt-dlp.exe` through the preserving helper. Remove the old `remove(target); rename(staged,target)` sequence. Simplify FFmpeg archive finalization to use the already committed download target.

- [ ] **Step 6: Run and verify GREEN, then commit**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git add CMakeLists.txt src/Core/FileOperations.h src/Core/FileOperations.cpp src/Platform/WinHttpClient.cpp src/Core/ToolManagers.cpp tests/test_core.cpp
git commit -m "fix: validate downloads before preserving replacement"
```

### Task 6: Own and debounce application background work

**Files:**
- Create: `src/Core/AsyncWait.h`
- Create: `src/Core/AsyncWait.cpp`
- Modify: `src/UI/Application.h`
- Modify: `src/UI/Application.cpp`
- Modify: `src/Core/ToolManagers.h`
- Modify: `src/Core/ToolManagers.cpp`
- Test: `tests/test_core.cpp`

- [ ] **Step 1: Write a failing cancelable-delay test**

Add a small UI-independent helper `bool WaitForDelay(std::stop_token, std::chrono::milliseconds)` and test both timeout completion and immediate stop. The helper uses `condition_variable_any::wait_for(lock, token, delay, [] { return false; })`.

- [ ] **Step 2: Run and verify RED**

Expected: compilation fails because `WaitForDelay` does not exist.

- [ ] **Step 3: Add owned `std::jthread` members and result mailboxes**

Add `m_toolCheckWorker`, `m_appUpdateWorker`, and `m_previewWorker`. Store optional result values under `m_asyncResultMutex`; posted messages contain no raw pointer. Handlers move the optional result out under the mutex. Failed `PostMessageW` therefore cannot leak memory.

- [ ] **Step 4: Debounce and cancel preview**

`StartPreviewFetch` requests stop on the previous worker, starts a new worker, waits 300 ms through `WaitForDelay`, then bridges its stop token to a Win32 event passed to `FetchPreview` and `CacheThumbnail`. Only the current request ID is accepted.

- [ ] **Step 5: Make tool/update checks cancelable**

Extend `YtDlpManager::CheckLatestRelease` and `AppUpdateService::CheckLatestRelease` with optional cancel events and pass them to `WinHttpClient::GetString`. Bridge each worker stop token to its event.

- [ ] **Step 6: Make shutdown idempotent and join first**

Add `m_shutdownStarted` and `m_comInitialized`. Request stop and join application workers before releasing queue, logger, GDI+, windows, or COM. Call `CoUninitialize` only when `CoInitializeEx` succeeded.

- [ ] **Step 7: Run and verify GREEN, then commit**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git add src/UI/Application.h src/UI/Application.cpp src/Core/ToolManagers.h src/Core/ToolManagers.cpp tests/test_core.cpp
git commit -m "fix: own and debounce background application work"
```

### Task 7: Integrate operational logging and remove dead code

**Files:**
- Modify: `src/Core/AppPaths.h`
- Modify: `src/Core/AppPaths.cpp`
- Modify: `src/Core/DownloadQueue.h`
- Modify: `src/Core/DownloadQueue.cpp`
- Modify: `src/Core/ToolManagers.h`
- Modify: `src/Core/ToolManagers.cpp`
- Modify: `src/UI/Application.h`
- Modify: `src/UI/Application.cpp`
- Modify: `src/UI/DialogWindows.h`
- Modify: `src/UI/DialogWindows.cpp`
- Modify: `tests/test_core.cpp`

- [ ] **Step 1: Add queue lifecycle logging**

Pass an optional `Logger*` to `DownloadQueue`. Log enqueue, cancellation, retry, completion, and failure. Truncate failure text to a bounded 1,000 characters before logging.

- [ ] **Step 2: Add application operation logging**

Log tool checks, update checks, preview start/cancel/success/failure, settings saves, and runtime concurrency changes. Do not pass raw process output to the logger.

- [ ] **Step 3: Remove dead state and fields**

Remove `PostProcessing`, `lastOutputLine`, `ReleaseAssetInfo::pageUrl`, `FfmpegStatus::ffprobeExe`, `FfmpegStatus::binDir`, `AppPaths::assetsDir()`, unused result fields/assignments, and the unused `m_ytDlpClient` member. Update tests that asserted removed representation rather than behavior.

- [ ] **Step 4: Remove unused dialog overloads**

Keep only the path-aware settings/about/FFmpeg APIs used by the application. Remove paths-empty adapters and unreachable fallback branches.

- [ ] **Step 5: Resolve analyzer lifetime warning in metadata enrichment**

Match the URL first, then move title/thumbnail once and return immediately, preventing loop continuation with moved-from values.

- [ ] **Step 6: Build and test, then commit**

```powershell
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
git add src tests/test_core.cpp
git commit -m "refactor: log operations and remove dead application code"
```

### Task 8: Full verification and review

**Files:** Modify only if verification exposes a regression.

- [ ] **Step 1: Run the clean Release build and tests**

```powershell
cmake --build build --config Release --clean-first
ctest --test-dir build -C Release --output-on-failure
```

Expected: build exit 0; all tests pass.

- [ ] **Step 2: Run MSVC Code Analysis**

```powershell
cmake --build build --config Release --target YoutubeDownloader -- /p:RunCodeAnalysis=true /p:EnableCppCoreCheck=true /t:Rebuild
```

Expected: no warnings from project sources. Existing warnings originating solely inside `third_party/nlohmann/json.hpp` are recorded separately.

- [ ] **Step 3: Audit dead symbols and detached threads**

```powershell
rg -n "\.detach\(|PostProcessing|lastOutputLine|pageUrl|ffprobeExe|binDir|assetsDir|m_ytDlpClient" src tests
```

Expected: no matches, except legitimate FFprobe executable path helpers used by FFmpeg installation.

- [ ] **Step 4: Inspect repository state and diff**

```powershell
git status --short
git diff --check
git log --oneline --decorate -8
```

Expected: clean worktree, no whitespace errors, intentional task commits only.

- [ ] **Step 5: Request code review**

Invoke `superpowers:requesting-code-review`, address any validated findings, and rerun the complete verification gate before integration handoff.

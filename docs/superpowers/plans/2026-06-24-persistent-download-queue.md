# Persistent Download Queue Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop all owned process trees on shutdown and persist all non-deleted download tasks so users can resume them after restart.

**Architecture:** `ProcessRunner` launches each process inside a kill-on-close Windows Job Object. `DownloadQueueStore` serializes queue snapshots to `stuff/download_queue.json`, while `DownloadQueue` exposes explicit import/export hooks and keeps scheduling semantics separate from storage. `Application` loads once during backend initialization and saves after queue mutations and during shutdown.

**Tech Stack:** C++20, Win32 Job Objects, nlohmann/json, CMake/MSVC, existing `YoutubeDownloaderTests`.

---

## File map

- `src/Platform/ProcessRunner.cpp`: create and assign a kill-on-close Job Object before resuming child processes.
- `src/Core/AppPaths.h/.cpp`: expose `downloadQueuePath()` returning `stuff/download_queue.json`.
- `src/Core/DownloadQueueStore.h/.cpp`: load/save queue snapshots as JSON.
- `src/Core/DownloadQueue.h/.cpp`: import restored tasks, export persistence snapshots, mark active tasks stopped for shutdown persistence.
- `src/UI/Application.h/.cpp`: load queue at startup, save after queue mutations, save stopped queue during shutdown.
- `CMakeLists.txt`: compile `DownloadQueueStore.cpp`.
- `tests/test_core.cpp`: regression tests for process-tree cancellation and queue persistence.

## Task 1: ProcessRunner kills child processes

- [ ] Add a failing test that starts `cmd.exe` which launches a long-running child `powershell.exe`, cancels the run, and asserts the child PID exits.
- [ ] Implement Job Object creation with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
- [ ] Start child processes suspended, assign them to the job, then resume the primary thread.
- [ ] Run the process test and full core test suite.
- [ ] Commit: `fix: kill process trees for canceled runs`.

## Task 2: Queue store round-trip

- [ ] Add `AppPaths::downloadQueuePath()` test expecting `stuff/download_queue.json`.
- [ ] Add `DownloadQueueStore` tests for round-tripping snapshots with request fields, state, progress, output files, and thumbnail path.
- [ ] Implement `DownloadQueueStore::Save()` with temp-file replacement and `DownloadQueueStore::Load()` with schema validation and invalid-entry skipping.
- [ ] Register the new files in CMake.
- [ ] Run tests.
- [ ] Commit: `feat: persist download queue snapshots`.

## Task 3: DownloadQueue import/export and stopped shutdown state

- [ ] Add tests for importing snapshots without auto-starting them.
- [ ] Add tests that exporting for shutdown turns `Preparing`/`Downloading` tasks into `Canceled` with `Остановлено`.
- [ ] Add tests that deleting a task removes it from subsequent persistence export.
- [ ] Implement `ImportSnapshots`, `ExportSnapshots`, and `ExportSnapshotsForShutdown`.
- [ ] Run tests.
- [ ] Commit: `feat: restore download queue tasks`.

## Task 4: Application persistence integration

- [ ] Load persisted snapshots after queue construction during backend initialization.
- [ ] Save queue snapshots after enqueue, cancel, retry, delete, clear queued, clear finished, and metadata enrichment that changes the queue.
- [ ] During shutdown, save `ExportSnapshotsForShutdown()` before destroying the queue, then stop workers.
- [ ] Log load/save failures without blocking application shutdown.
- [ ] Run build and tests.
- [ ] Commit: `feat: restore queued downloads on startup`.

## Task 5: Verification

- [ ] Run `cmake --build build --config Release --clean-first`.
- [ ] Run `ctest --test-dir build -C Release --output-on-failure`.
- [ ] Run MSVC Code Analysis and record only third-party warnings if any.
- [ ] Run `rg -n "\\.detach\\(|download_queue\\.json|JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|ExportSnapshotsForShutdown|ImportSnapshots" src tests`.
- [ ] Run `git diff --check` and inspect `git status --short`.


#pragma once

#include "YtDlpClient.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

class Logger;

enum class DownloadTaskState {
    Queued,
    Preparing,
    Downloading,
    PostProcessing,
    Completed,
    Failed,
    Canceled
};

struct DownloadTaskSnapshot {
    int id = 0;
    YtDlpDownloadRequest request;
    std::wstring title;
    std::filesystem::path thumbnailPath;
    DownloadTaskState state = DownloadTaskState::Queued;
    double percent = 0.0;
    std::wstring statusText;
    std::wstring errorText;
    std::uint64_t downloadedBytes = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t speedBytesPerSecond = 0;
    std::uint64_t etaSeconds = 0;
    std::wstring mediaKind;
    std::wstring formatId;
    std::wstring extension;
    std::wstring resolution;
    std::vector<std::filesystem::path> outputFiles;
};

struct DownloadTaskCallbacks {
    std::function<void(const YtDlpProgress& progress)> onProgressDetails;
    std::function<void(double percent, const std::wstring& status)> onPostProcessing;
    std::function<void(const std::wstring& line)> onOutputLine;
};

struct DownloadTaskResult {
    bool success = false;
    std::wstring errorText;
    std::vector<std::filesystem::path> outputFiles;
    std::wstring statusText;
};

using DownloadTaskExecutor = std::function<DownloadTaskResult(
    const DownloadTaskSnapshot& task,
    std::stop_token stopToken,
    const DownloadTaskCallbacks& callbacks
)>;

class DownloadQueue {
public:
    explicit DownloadQueue(int maxParallelDownloads, Logger* logger = nullptr);
    ~DownloadQueue();

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    void SetExecutor(DownloadTaskExecutor executor);
    void SetMaxParallelDownloads(int maxParallelDownloads);
    int Enqueue(const YtDlpDownloadRequest& request, std::wstring title, std::filesystem::path thumbnailPath = {});
    bool EnrichMetadata(const std::wstring& url, std::wstring title, std::filesystem::path thumbnailPath = {});
    bool Cancel(int id);
    bool Retry(int id);
    bool StartPostProcessing(int id, DownloadTaskExecutor executor, std::wstring statusText);
    bool DeleteFiles(int id);
    size_t ClearQueued();
    size_t ClearFinished();

    DownloadTaskSnapshot GetTask(int id) const;
    std::vector<DownloadTaskSnapshot> Snapshot() const;
    void ImportSnapshots(const std::vector<DownloadTaskSnapshot>& tasks);
    std::vector<DownloadTaskSnapshot> ExportSnapshots() const;
    std::vector<DownloadTaskSnapshot> ExportSnapshotsForShutdown() const;
    std::uint64_t Revision() const;
    void WaitForIdle();
    void Shutdown();

private:
    struct TaskRecord {
        DownloadTaskSnapshot snapshot;
        std::chrono::steady_clock::time_point startedAt{};
        bool cancelRequested = false;
        bool active = false;
        bool postProcessingOnly = false;
    };

    void SchedulerLoop();
    void StartTask(int id, std::stop_token stopToken);
    void RunTask(int id, DownloadTaskExecutor executor, std::stop_token stopToken);
    void UpdateTaskProgress(int id, const YtDlpProgress& progress);
    void RecordTaskOutput(int id, const std::wstring& line);
    void FinishTask(int id, std::stop_token stopToken, const DownloadTaskResult& result);
    DownloadTaskResult DefaultExecutor(
        const DownloadTaskSnapshot& task,
        std::stop_token stopToken,
        const DownloadTaskCallbacks& callbacks
    );

    int m_maxParallelDownloads = 1;
    int m_nextId = 1;
    int m_activeCount = 0;
    std::uint64_t m_revision = 0;
    bool m_shutdown = false;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<int, TaskRecord> m_tasks;
    std::map<int, std::jthread> m_workers;
    std::vector<int> m_finishedWorkerIds;
    DownloadTaskExecutor m_executor;
    Logger* m_logger = nullptr;
    std::jthread m_scheduler;
};

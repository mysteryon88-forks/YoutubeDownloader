#pragma once

#include "YtDlpClient.h"

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
    DownloadTaskState state = DownloadTaskState::Queued;
    double percent = 0.0;
    std::wstring statusText;
    std::wstring lastOutputLine;
    std::wstring errorText;
    std::vector<std::filesystem::path> outputFiles;
};

struct DownloadTaskCallbacks {
    std::function<void(double percent, const std::wstring& status)> onProgress;
    std::function<void(const std::wstring& line)> onOutputLine;
    std::function<bool()> isCanceled;
};

struct DownloadTaskResult {
    bool success = false;
    std::wstring errorText;
    std::vector<std::filesystem::path> outputFiles;
};

using DownloadTaskExecutor = std::function<DownloadTaskResult(
    const DownloadTaskSnapshot& task,
    const DownloadTaskCallbacks& callbacks
)>;

class DownloadQueue {
public:
    explicit DownloadQueue(int maxParallelDownloads);
    ~DownloadQueue();

    DownloadQueue(const DownloadQueue&) = delete;
    DownloadQueue& operator=(const DownloadQueue&) = delete;

    void SetExecutor(DownloadTaskExecutor executor);
    int Enqueue(const YtDlpDownloadRequest& request, std::wstring title);
    bool Cancel(int id);
    bool Retry(int id);
    bool DeleteFiles(int id);
    void ClearFinished();

    DownloadTaskSnapshot GetTask(int id) const;
    std::vector<DownloadTaskSnapshot> Snapshot() const;
    void WaitForIdle();
    void Shutdown();

private:
    struct TaskRecord {
        DownloadTaskSnapshot snapshot;
        bool cancelRequested = false;
        bool active = false;
    };

    void SchedulerLoop();
    void StartTask(int id);
    DownloadTaskResult DefaultExecutor(const DownloadTaskSnapshot& task, const DownloadTaskCallbacks& callbacks);

    int m_maxParallelDownloads = 1;
    int m_nextId = 1;
    int m_activeCount = 0;
    bool m_shutdown = false;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<int, TaskRecord> m_tasks;
    std::thread m_scheduler;
    DownloadTaskExecutor m_executor;
};


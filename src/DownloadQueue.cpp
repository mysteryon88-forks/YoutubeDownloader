#include "DownloadQueue.h"

#include "ProcessRunner.h"

#include <algorithm>
#include <cstring>
#include <system_error>

DownloadQueue::DownloadQueue(int maxParallelDownloads)
    : m_maxParallelDownloads(std::max(1, maxParallelDownloads)),
      m_executor([this](const DownloadTaskSnapshot& task, const DownloadTaskCallbacks& callbacks) {
          return DefaultExecutor(task, callbacks);
      }),
      m_scheduler(&DownloadQueue::SchedulerLoop, this) {
}

DownloadQueue::~DownloadQueue() {
    Shutdown();
}

void DownloadQueue::SetExecutor(DownloadTaskExecutor executor) {
    std::lock_guard lock(m_mutex);
    m_executor = std::move(executor);
}

int DownloadQueue::Enqueue(const YtDlpDownloadRequest& request, std::wstring title) {
    std::lock_guard lock(m_mutex);
    const int id = m_nextId++;
    TaskRecord record;
    record.snapshot.id = id;
    record.snapshot.request = request;
    record.snapshot.title = std::move(title);
    record.snapshot.state = DownloadTaskState::Queued;
    record.snapshot.statusText = L"В очереди";
    m_tasks[id] = std::move(record);
    m_cv.notify_all();
    return id;
}

bool DownloadQueue::Cancel(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return false;
    }
    TaskRecord& task = it->second;
    if (task.snapshot.state == DownloadTaskState::Queued) {
        task.snapshot.state = DownloadTaskState::Canceled;
        task.snapshot.statusText = L"Отменено";
        m_cv.notify_all();
        return true;
    }
    if (task.active) {
        task.cancelRequested = true;
        task.snapshot.statusText = L"Отмена...";
        return true;
    }
    return false;
}

bool DownloadQueue::Retry(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return false;
    }
    TaskRecord& task = it->second;
    if (task.active ||
        (task.snapshot.state != DownloadTaskState::Failed && task.snapshot.state != DownloadTaskState::Canceled)) {
        return false;
    }
    task.cancelRequested = false;
    task.snapshot.state = DownloadTaskState::Queued;
    task.snapshot.percent = 0.0;
    task.snapshot.errorText.clear();
    task.snapshot.statusText = L"В очереди";
    m_cv.notify_all();
    return true;
}

bool DownloadQueue::DeleteFiles(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end() || it->second.active) {
        return false;
    }

    bool removedAny = false;
    for (const std::filesystem::path& path : it->second.snapshot.outputFiles) {
        std::error_code ec;
        removedAny = std::filesystem::remove(path, ec) || removedAny;
    }
    it->second.snapshot.outputFiles.clear();
    return removedAny;
}

void DownloadQueue::ClearFinished() {
    std::lock_guard lock(m_mutex);
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        const DownloadTaskState state = it->second.snapshot.state;
        if (!it->second.active &&
            (state == DownloadTaskState::Completed || state == DownloadTaskState::Failed || state == DownloadTaskState::Canceled)) {
            it = m_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

DownloadTaskSnapshot DownloadQueue::GetTask(int id) const {
    std::lock_guard lock(m_mutex);
    const auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return {};
    }
    return it->second.snapshot;
}

std::vector<DownloadTaskSnapshot> DownloadQueue::Snapshot() const {
    std::lock_guard lock(m_mutex);
    std::vector<DownloadTaskSnapshot> result;
    result.reserve(m_tasks.size());
    for (const auto& [id, task] : m_tasks) {
        UNREFERENCED_PARAMETER(id);
        result.push_back(task.snapshot);
    }
    return result;
}

void DownloadQueue::WaitForIdle() {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this]() {
        if (m_activeCount > 0) {
            return false;
        }
        return std::none_of(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
            return item.second.snapshot.state == DownloadTaskState::Queued;
        });
    });
}

void DownloadQueue::Shutdown() {
    {
        std::lock_guard lock(m_mutex);
        if (m_shutdown) {
            return;
        }
        m_shutdown = true;
        for (auto& [id, task] : m_tasks) {
            UNREFERENCED_PARAMETER(id);
            task.cancelRequested = true;
        }
    }
    m_cv.notify_all();
    if (m_scheduler.joinable()) {
        m_scheduler.join();
    }
}

void DownloadQueue::SchedulerLoop() {
    while (true) {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this]() {
            if (m_shutdown) {
                return true;
            }
            if (m_activeCount >= m_maxParallelDownloads) {
                return false;
            }
            return std::any_of(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
                return item.second.snapshot.state == DownloadTaskState::Queued;
            });
        });

        if (m_shutdown) {
            break;
        }

        while (m_activeCount < m_maxParallelDownloads) {
            auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
                return item.second.snapshot.state == DownloadTaskState::Queued;
            });
            if (it == m_tasks.end()) {
                break;
            }
            const int id = it->first;
            it->second.active = true;
            it->second.snapshot.state = DownloadTaskState::Preparing;
            it->second.snapshot.statusText = L"Подготовка";
            ++m_activeCount;
            std::thread(&DownloadQueue::StartTask, this, id).detach();
        }
    }
}

void DownloadQueue::StartTask(int id) {
    DownloadTaskSnapshot task;
    DownloadTaskExecutor executor;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it == m_tasks.end()) {
            return;
        }
        task = it->second.snapshot;
        executor = m_executor;
    }

    DownloadTaskCallbacks callbacks;
    callbacks.onProgress = [this, id](double percent, const std::wstring& status) {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it == m_tasks.end()) {
            return;
        }
        it->second.snapshot.state = DownloadTaskState::Downloading;
        it->second.snapshot.percent = percent;
        it->second.snapshot.statusText = status;
    };
    callbacks.onOutputLine = [this, id](const std::wstring& line) {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it != m_tasks.end()) {
            it->second.snapshot.lastOutputLine = line;
        }
    };
    callbacks.isCanceled = [this, id]() {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        return it == m_tasks.end() || it->second.cancelRequested || m_shutdown;
    };

    DownloadTaskResult result;
    try {
        result = executor(task, callbacks);
    } catch (const std::exception& ex) {
        result.success = false;
        result.errorText.assign(ex.what(), ex.what() + std::strlen(ex.what()));
    } catch (...) {
        result.success = false;
        result.errorText = L"Неизвестная ошибка";
    }

    {
        std::lock_guard lock(m_mutex);
        auto it = m_tasks.find(id);
        if (it != m_tasks.end()) {
            it->second.active = false;
            --m_activeCount;
            if (it->second.cancelRequested) {
                it->second.snapshot.state = DownloadTaskState::Canceled;
                it->second.snapshot.statusText = L"Отменено";
            } else if (result.success) {
                it->second.snapshot.state = DownloadTaskState::Completed;
                it->second.snapshot.percent = 100.0;
                it->second.snapshot.statusText = L"Готово";
                it->second.snapshot.outputFiles = std::move(result.outputFiles);
            } else {
                it->second.snapshot.state = DownloadTaskState::Failed;
                it->second.snapshot.errorText = result.errorText;
                it->second.snapshot.statusText = L"Ошибка";
            }
        }
    }
    m_cv.notify_all();
}

DownloadTaskResult DownloadQueue::DefaultExecutor(
    const DownloadTaskSnapshot& task,
    const DownloadTaskCallbacks& callbacks
) {
    ProcessRunOptions options;
    options.executable = task.request.ytDlpExePath.empty() ? std::filesystem::path(L"yt-dlp.exe") : task.request.ytDlpExePath;
    options.arguments = BuildDownloadArguments(task.request);
    options.timeoutMs = INFINITE;
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

    const ProcessRunResult result = ProcessRunner::Run(options);
    if (callbacks.isCanceled && callbacks.isCanceled()) {
        return {false, L"Отменено", {}};
    }
    if (result.exitCode != 0) {
        return {false, result.stderrText.empty() ? result.stdoutText : result.stderrText, {}};
    }
    return {true, L"", {}};
}

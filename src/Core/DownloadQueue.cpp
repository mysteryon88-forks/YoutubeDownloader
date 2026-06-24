#include "DownloadQueue.h"

#include "Logger.h"
#include "ProcessRunner.h"

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <ranges>
#include <string_view>
#include <system_error>

namespace {

std::filesystem::path ExtractQuotedPath(const std::wstring& line) {
    const size_t firstQuote = line.find(L'"');
    if (firstQuote == std::wstring::npos) {
        return {};
    }
    const size_t lastQuote = line.find_last_of(L'"');
    if (lastQuote == firstQuote || lastQuote == std::wstring::npos) {
        return {};
    }
    return std::filesystem::path(line.substr(firstQuote + 1, lastQuote - firstQuote - 1));
}

std::filesystem::path ExtractKnownOutputPath(const std::wstring& line) {
    constexpr const wchar_t* kDestination = L"[download] Destination:";
    constexpr const wchar_t* kMerging = L"[Merger] Merging formats into";

    std::wstring normalized = line;
    while (!normalized.empty() && iswspace(normalized.front())) {
        normalized.erase(normalized.begin());
    }
    if (!normalized.empty() && normalized.front() == L'\r') {
        normalized.erase(normalized.begin());
    }
    while (!normalized.empty() && iswspace(normalized.front())) {
        normalized.erase(normalized.begin());
    }

    if (normalized.starts_with(kDestination)) {
        std::wstring value = normalized.substr(std::wcslen(kDestination));
        while (!value.empty() && iswspace(value.front())) {
            value.erase(value.begin());
        }
        return std::filesystem::path(value);
    }
    if (normalized.starts_with(kMerging)) {
        return ExtractQuotedPath(normalized);
    }
    return {};
}

void AddUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    if (std::ranges::find(paths, path) == paths.end()) {
        paths.push_back(path);
    }
}

std::uint64_t FileSizeIfExists(const std::filesystem::path& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
}

std::uint64_t DiskBytesForPaths(const std::vector<std::filesystem::path>& paths) {
    std::uint64_t total = 0;
    for (const std::filesystem::path& path : paths) {
        total += FileSizeIfExists(path);
        total += FileSizeIfExists(std::filesystem::path(path.wstring() + L".part"));
    }
    return total;
}

std::wstring ExtractQueryValue(const std::wstring& url, std::wstring_view key) {
    const std::wstring needle = std::wstring(key) + L"=";
    size_t pos = url.find(needle);
    if (pos == std::wstring::npos) {
        return {};
    }
    pos += needle.size();
    const size_t end = url.find_first_of(L"&#?", pos);
    return url.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
}

std::wstring ExtractLikelyVideoId(const std::wstring& url) {
    std::wstring id = ExtractQueryValue(url, L"v");
    if (!id.empty()) {
        return id;
    }

    const std::array<std::wstring_view, 3> markers = {
        L"youtu.be/",
        L"/shorts/",
        L"/embed/"
    };
    for (std::wstring_view marker : markers) {
        const size_t markerPos = url.find(marker);
        if (markerPos == std::wstring::npos) {
            continue;
        }
        const size_t idStart = markerPos + marker.size();
        const size_t idEnd = url.find_first_of(L"/?&#", idStart);
        id = url.substr(idStart, idEnd == std::wstring::npos ? std::wstring::npos : idEnd - idStart);
        if (!id.empty()) {
            return id;
        }
    }
    return {};
}

void RemoveKnownPartialFilesFor(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(std::filesystem::path(path.wstring() + L".part"), ec);

    const std::filesystem::path parent = path.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, ec)) {
        return;
    }

    const std::wstring prefix = path.filename().wstring() + L".part";
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (ec) {
            break;
        }
        const std::wstring name = entry.path().filename().wstring();
        if (name.starts_with(prefix)) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

void RemovePartialFilesByVideoId(const std::filesystem::path& outputDirectory, const std::wstring& videoId) {
    if (outputDirectory.empty() || videoId.empty()) {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(outputDirectory, ec)) {
        return;
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(outputDirectory, ec)) {
        if (ec) {
            break;
        }
        const std::filesystem::path path = entry.path();
        const std::wstring name = path.filename().wstring();
        if (name.find(videoId) == std::wstring::npos) {
            continue;
        }
        if (name.ends_with(L".part") || name.find(L".part-") != std::wstring::npos) {
            std::filesystem::remove(path, ec);
        }
    }
}

void RemoveInvalidTaskFiles(const DownloadTaskSnapshot& task) {
    for (const std::filesystem::path& path : task.outputFiles) {
        RemoveKnownPartialFilesFor(path);
    }
    RemovePartialFilesByVideoId(task.request.outputDirectory, ExtractLikelyVideoId(task.request.url));
}

bool IsPlaceholderTitle(const DownloadTaskSnapshot& task) {
    return task.title.empty() || task.title == task.request.url;
}

bool EnrichTaskMetadata(DownloadTaskSnapshot& task, std::wstring title, std::filesystem::path thumbnailPath) {
    bool changed = false;
    if (!title.empty() && title != task.request.url && IsPlaceholderTitle(task)) {
        task.title = std::move(title);
        changed = true;
    }
    if (!thumbnailPath.empty() && task.thumbnailPath.empty()) {
        task.thumbnailPath = std::move(thumbnailPath);
        changed = true;
    }
    return changed;
}

bool IsPersistedRunningState(DownloadTaskState state) {
    return state == DownloadTaskState::Queued ||
           state == DownloadTaskState::Preparing ||
           state == DownloadTaskState::Downloading;
}

DownloadTaskSnapshot NormalizeRestoredSnapshot(DownloadTaskSnapshot snapshot) {
    if (IsPersistedRunningState(snapshot.state)) {
        snapshot.state = DownloadTaskState::Canceled;
        snapshot.statusText = L"Остановлено";
        snapshot.speedBytesPerSecond = 0;
        snapshot.etaSeconds = 0;
    }
    if (snapshot.statusText.empty()) {
        switch (snapshot.state) {
        case DownloadTaskState::Completed:
            snapshot.statusText = L"Готово";
            break;
        case DownloadTaskState::Failed:
            snapshot.statusText = L"Ошибка";
            break;
        case DownloadTaskState::Canceled:
            snapshot.statusText = L"Остановлено";
            break;
        case DownloadTaskState::Queued:
            snapshot.statusText = L"В очереди";
            break;
        case DownloadTaskState::Preparing:
            snapshot.statusText = L"Подготовка";
            break;
        case DownloadTaskState::Downloading:
            snapshot.statusText = L"Загрузка";
            break;
        }
    }
    return snapshot;
}

DownloadTaskSnapshot SnapshotForShutdown(DownloadTaskSnapshot snapshot) {
    if (IsPersistedRunningState(snapshot.state)) {
        snapshot.state = DownloadTaskState::Canceled;
        snapshot.statusText = L"Остановлено";
        snapshot.speedBytesPerSecond = 0;
        snapshot.etaSeconds = 0;
    }
    return snapshot;
}

} // namespace

DownloadQueue::DownloadQueue(int maxParallelDownloads, Logger* logger)
    : m_maxParallelDownloads(std::max(1, maxParallelDownloads)),
      m_executor([this](
          const DownloadTaskSnapshot& task,
          std::stop_token stopToken,
          const DownloadTaskCallbacks& callbacks
      ) {
          return DefaultExecutor(task, stopToken, callbacks);
      }),
      m_logger(logger),
      m_scheduler(&DownloadQueue::SchedulerLoop, this) {
}

DownloadQueue::~DownloadQueue() {
    Shutdown();
}

void DownloadQueue::SetExecutor(DownloadTaskExecutor executor) {
    std::lock_guard lock(m_mutex);
    m_executor = std::move(executor);
}

void DownloadQueue::SetMaxParallelDownloads(int maxParallelDownloads) {
    {
        std::lock_guard lock(m_mutex);
        m_maxParallelDownloads = std::max(1, maxParallelDownloads);
    }
    m_cv.notify_all();
}

int DownloadQueue::Enqueue(const YtDlpDownloadRequest& request, std::wstring title, std::filesystem::path thumbnailPath) {
    std::lock_guard lock(m_mutex);
    for (auto& [id, task] : m_tasks) {
        if (task.snapshot.request.url == request.url) {
            if (EnrichTaskMetadata(task.snapshot, std::move(title), std::move(thumbnailPath))) {
                ++m_revision;
            }
            return id;
        }
    }

    const int id = m_nextId++;
    TaskRecord record;
    record.snapshot.id = id;
    record.snapshot.request = request;
    record.snapshot.title = std::move(title);
    record.snapshot.thumbnailPath = std::move(thumbnailPath);
    record.snapshot.state = DownloadTaskState::Queued;
    record.snapshot.statusText = L"В очереди";
    m_tasks[id] = std::move(record);
    if (m_logger) {
        m_logger->Info(L"Download task enqueued: id=" + std::to_wstring(id) + L" url=" + request.url);
    }
    ++m_revision;
    m_cv.notify_all();
    return id;
}

bool DownloadQueue::EnrichMetadata(const std::wstring& url, std::wstring title, std::filesystem::path thumbnailPath) {
    std::lock_guard lock(m_mutex);
    for (auto& [id, task] : m_tasks) {
        UNREFERENCED_PARAMETER(id);
        if (task.snapshot.request.url != url) {
            continue;
        }
        if (EnrichTaskMetadata(task.snapshot, std::move(title), std::move(thumbnailPath))) {
            ++m_revision;
            return true;
        }
        return false;
    }
    return false;
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
        if (m_logger) {
            m_logger->Info(L"Download task canceled while queued: id=" + std::to_wstring(id));
        }
        ++m_revision;
        m_cv.notify_all();
        return true;
    }
    if (task.active) {
        task.cancelRequested = true;
        task.snapshot.statusText = L"Отмена...";
        if (m_logger) {
            m_logger->Info(L"Download task cancellation requested: id=" + std::to_wstring(id));
        }
        const auto worker = m_workers.find(id);
        if (worker != m_workers.end()) {
            worker->second.request_stop();
        }
        ++m_revision;
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
    task.snapshot.downloadedBytes = 0;
    task.snapshot.totalBytes = 0;
    task.snapshot.speedBytesPerSecond = 0;
    task.snapshot.etaSeconds = 0;
    task.snapshot.mediaKind.clear();
    task.snapshot.formatId.clear();
    task.snapshot.extension.clear();
    task.snapshot.resolution.clear();
    if (m_logger) {
        m_logger->Info(L"Download task retried: id=" + std::to_wstring(id));
    }
    ++m_revision;
    m_cv.notify_all();
    return true;
}

bool DownloadQueue::DeleteFiles(int id) {
    std::lock_guard lock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end() || it->second.active) {
        return false;
    }

    const DownloadTaskState state = it->second.snapshot.state;
    if (state == DownloadTaskState::Canceled || state == DownloadTaskState::Failed) {
        RemoveInvalidTaskFiles(it->second.snapshot);
    }
    m_tasks.erase(it);
    ++m_revision;
    m_cv.notify_all();
    return true;
}

size_t DownloadQueue::ClearQueued() {
    std::lock_guard lock(m_mutex);
    size_t removed = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        if (!it->second.active && it->second.snapshot.state == DownloadTaskState::Queued) {
            it = m_tasks.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        ++m_revision;
        m_cv.notify_all();
    }
    return removed;
}

size_t DownloadQueue::ClearFinished() {
    std::lock_guard lock(m_mutex);
    size_t removed = 0;
    for (auto it = m_tasks.begin(); it != m_tasks.end();) {
        const DownloadTaskState state = it->second.snapshot.state;
        if (!it->second.active &&
            (state == DownloadTaskState::Completed || state == DownloadTaskState::Failed || state == DownloadTaskState::Canceled)) {
            if (state == DownloadTaskState::Failed || state == DownloadTaskState::Canceled) {
                RemoveInvalidTaskFiles(it->second.snapshot);
            }
            it = m_tasks.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    if (removed > 0) {
        ++m_revision;
        m_cv.notify_all();
    }
    return removed;
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

void DownloadQueue::ImportSnapshots(const std::vector<DownloadTaskSnapshot>& tasks) {
    std::lock_guard lock(m_mutex);
    if (m_activeCount > 0) {
        return;
    }

    m_tasks.clear();
    m_workers.clear();
    m_finishedWorkerIds.clear();
    m_nextId = 1;
    for (const DownloadTaskSnapshot& task : tasks) {
        if (task.id <= 0 || task.request.url.empty()) {
            continue;
        }
        TaskRecord record;
        record.snapshot = NormalizeRestoredSnapshot(task);
        record.active = false;
        record.cancelRequested = false;
        m_tasks[record.snapshot.id] = std::move(record);
        m_nextId = std::max(m_nextId, task.id + 1);
    }
    ++m_revision;
}

std::vector<DownloadTaskSnapshot> DownloadQueue::ExportSnapshots() const {
    std::lock_guard lock(m_mutex);
    std::vector<DownloadTaskSnapshot> result;
    result.reserve(m_tasks.size());
    for (const auto& [id, task] : m_tasks) {
        UNREFERENCED_PARAMETER(id);
        if (task.snapshot.state != DownloadTaskState::Completed) {
            result.push_back(task.snapshot);
        }
    }
    return result;
}

std::vector<DownloadTaskSnapshot> DownloadQueue::ExportSnapshotsForShutdown() const {
    std::lock_guard lock(m_mutex);
    std::vector<DownloadTaskSnapshot> result;
    result.reserve(m_tasks.size());
    for (const auto& [id, task] : m_tasks) {
        UNREFERENCED_PARAMETER(id);
        if (task.snapshot.state != DownloadTaskState::Completed) {
            result.push_back(SnapshotForShutdown(task.snapshot));
        }
    }
    return result;
}

std::uint64_t DownloadQueue::Revision() const {
    std::lock_guard lock(m_mutex);
    return m_revision;
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
    std::map<int, std::jthread> workers;
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
        for (auto& [id, worker] : m_workers) {
            UNREFERENCED_PARAMETER(id);
            worker.request_stop();
        }
    }
    m_cv.notify_all();
    if (m_scheduler.joinable()) {
        m_scheduler.request_stop();
        m_scheduler.join();
    }
    {
        std::lock_guard lock(m_mutex);
        workers = std::move(m_workers);
        m_finishedWorkerIds.clear();
    }
    for (auto& [id, worker] : workers) {
        UNREFERENCED_PARAMETER(id);
        worker.request_stop();
    }
    workers.clear();
}

void DownloadQueue::SchedulerLoop() {
    while (true) {
        std::vector<std::jthread> finished;
        {
            std::unique_lock schedulerLock(m_mutex);
            m_cv.wait(schedulerLock, [this]() {
                if (m_shutdown || !m_finishedWorkerIds.empty()) {
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

            finished.reserve(m_finishedWorkerIds.size());
            for (int id : m_finishedWorkerIds) {
                auto worker = m_workers.find(id);
                if (worker == m_workers.end()) {
                    continue;
                }
                finished.push_back(std::move(worker->second));
                m_workers.erase(worker);
            }
            m_finishedWorkerIds.clear();
        }

        finished.clear();

        std::lock_guard schedulerLock(m_mutex);
        if (m_shutdown) {
            break;
        }
        while (m_activeCount < m_maxParallelDownloads) {
            auto nextTask = std::find_if(m_tasks.begin(), m_tasks.end(), [](const auto& item) {
                return item.second.snapshot.state == DownloadTaskState::Queued;
            });
            if (nextTask == m_tasks.end()) {
                break;
            }
            const int id = nextTask->first;
            nextTask->second.active = true;
            nextTask->second.snapshot.state = DownloadTaskState::Preparing;
            nextTask->second.snapshot.statusText = L"Подготовка";
            ++m_revision;
            ++m_activeCount;
            m_workers.emplace(id, std::jthread([this, id](std::stop_token stopToken) {
                StartTask(id, stopToken);
            }));
        }
    }
}

void DownloadQueue::StartTask(int id, std::stop_token stopToken) {
    DownloadTaskSnapshot task;
    DownloadTaskExecutor executor;
    bool found = false;
    {
        std::lock_guard taskLock(m_mutex);
        auto it = m_tasks.find(id);
        if (it != m_tasks.end()) {
            task = it->second.snapshot;
            executor = m_executor;
            found = true;
        }
    }
    if (!found) {
        return;
    }

    DownloadTaskCallbacks callbacks;
    callbacks.onProgressDetails = [this, id](const YtDlpProgress& progress) {
        UpdateTaskProgress(id, progress);
    };
    callbacks.onOutputLine = [this, id](const std::wstring& line) {
        RecordTaskOutput(id, line);
    };
    DownloadTaskResult result;
    try {
        result = executor(task, stopToken, callbacks);
    } catch (const std::exception& ex) {
        result.success = false;
        result.errorText.assign(ex.what(), ex.what() + std::strlen(ex.what()));
    } catch (...) {
        result.success = false;
        result.errorText = L"Неизвестная ошибка";
    }

    FinishTask(id, stopToken, result);
    m_cv.notify_all();
}

void DownloadQueue::UpdateTaskProgress(int id, const YtDlpProgress& progress) {
    std::lock_guard progressLock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return;
    }
    DownloadTaskSnapshot& snapshot = it->second.snapshot;
    if (snapshot.mediaKind == L"audio" && progress.mediaKind != L"audio") {
        return;
    }
    const bool switchedTrack =
        !snapshot.mediaKind.empty() &&
        !progress.mediaKind.empty() &&
        snapshot.mediaKind != progress.mediaKind;

    snapshot.state = DownloadTaskState::Downloading;
    snapshot.statusText = progress.stage;
    const std::uint64_t diskBytes = DiskBytesForPaths(snapshot.outputFiles);
    const std::uint64_t reportedBytes = diskBytes > 0 ? diskBytes : progress.downloadedBytes;
    if (switchedTrack) {
        snapshot.percent = std::clamp(progress.percent, 0.0, 100.0);
        snapshot.downloadedBytes = reportedBytes;
        snapshot.totalBytes = std::max(progress.totalBytes, reportedBytes);
    } else {
        snapshot.downloadedBytes = std::max(snapshot.downloadedBytes, reportedBytes);
        snapshot.totalBytes = std::max({snapshot.totalBytes, progress.totalBytes, snapshot.downloadedBytes});
        double normalizedPercent = std::clamp(progress.percent, 0.0, 100.0);
        if (snapshot.totalBytes > 0) {
            normalizedPercent = std::max(
                normalizedPercent,
                (static_cast<double>(snapshot.downloadedBytes) / static_cast<double>(snapshot.totalBytes)) * 100.0
            );
        }
        snapshot.percent = std::max(snapshot.percent, std::clamp(normalizedPercent, 0.0, 100.0));
    }
    snapshot.speedBytesPerSecond = progress.speedBytesPerSecond;
    snapshot.etaSeconds = progress.etaSeconds;
    if (!progress.mediaKind.empty()) {
        snapshot.mediaKind = progress.mediaKind;
    }
    snapshot.formatId = progress.formatId;
    snapshot.extension = progress.extension;
    snapshot.resolution = progress.resolution;
    ++m_revision;
}

void DownloadQueue::RecordTaskOutput(int id, const std::wstring& line) {
    std::lock_guard outputLock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return;
    }
    AddUniquePath(it->second.snapshot.outputFiles, ExtractKnownOutputPath(line));
    ++m_revision;
}

void DownloadQueue::FinishTask(int id, std::stop_token stopToken, const DownloadTaskResult& result) {
    std::lock_guard finishLock(m_mutex);
    auto it = m_tasks.find(id);
    if (it == m_tasks.end()) {
        return;
    }

    it->second.active = false;
    --m_activeCount;
    if (it->second.cancelRequested || stopToken.stop_requested()) {
        it->second.snapshot.state = DownloadTaskState::Canceled;
        it->second.snapshot.statusText = L"Отменено";
        if (m_logger) {
            m_logger->Info(L"Download task canceled: id=" + std::to_wstring(id));
        }
        ++m_revision;
    } else if (result.success) {
        it->second.snapshot.state = DownloadTaskState::Completed;
        it->second.snapshot.percent = 100.0;
        it->second.snapshot.statusText = L"Готово";
        for (const std::filesystem::path& path : result.outputFiles) {
            AddUniquePath(it->second.snapshot.outputFiles, path);
        }
        if (m_logger) {
            m_logger->Info(L"Download task completed: id=" + std::to_wstring(id));
        }
        ++m_revision;
    } else {
        it->second.snapshot.state = DownloadTaskState::Failed;
        it->second.snapshot.errorText = result.errorText;
        it->second.snapshot.statusText = L"Ошибка";
        if (m_logger) {
            std::wstring error = result.errorText.substr(0, 1000);
            m_logger->Error(
                L"Download task failed: id=" + std::to_wstring(id) +
                (error.empty() ? L"" : L" error=" + error)
            );
        }
        ++m_revision;
    }
    m_finishedWorkerIds.push_back(id);
}

DownloadTaskResult DownloadQueue::DefaultExecutor(
    const DownloadTaskSnapshot& task,
    std::stop_token stopToken,
    const DownloadTaskCallbacks& callbacks
) {
    ProcessRunOptions options;
    options.executable = task.request.ytDlpExePath.empty() ? std::filesystem::path(L"yt-dlp.exe") : task.request.ytDlpExePath;
    options.arguments = BuildDownloadArguments(task.request);
    options.timeoutMs = INFINITE;
    HANDLE cancelEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!cancelEvent) {
        throw std::runtime_error("failed to create download cancellation event");
    }
    options.cancelEvent = cancelEvent;
    options.onStdoutLine = [callbacks](const std::wstring& line) {
        if (callbacks.onOutputLine) {
            callbacks.onOutputLine(line);
        }
        const YtDlpProgress progress = ParseYtDlpProgressLine(line);
        if (progress.recognized && callbacks.onProgressDetails) {
            callbacks.onProgressDetails(progress);
        }
    };
    options.onStderrLine = callbacks.onOutputLine;

    ProcessRunResult result;
    try {
        std::stop_callback stopCallback(stopToken, [cancelEvent]() {
            SetEvent(cancelEvent);
        });
        result = ProcessRunner::Run(options);
    } catch (...) {
        CloseHandle(cancelEvent);
        throw;
    }
    CloseHandle(cancelEvent);
    if (result.canceled || stopToken.stop_requested()) {
        return {false, L"Отменено", {}};
    }
    if (result.exitCode != 0) {
        return {false, result.stderrText.empty() ? result.stdoutText : result.stderrText, {}};
    }
    return {true, L"", {}};
}

#include "DownloadQueueStore.h"

#include "BackendText.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace {

std::string PathToJsonString(const std::filesystem::path& path) {
    return PathToUtf8(path);
}

std::filesystem::path PathFromJsonString(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return {};
    }
    return PathFromUtf8(it->get<std::string>());
}

std::wstring WStringFromJson(const nlohmann::json& json, const char* key, const std::wstring& fallback = L"") {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return Utf8ToWide(it->get<std::string>());
}

std::uint64_t UInt64FromJson(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_unsigned()) {
        return 0;
    }
    return it->get<std::uint64_t>();
}

double DoubleFromJson(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number()) {
        return 0.0;
    }
    return it->get<double>();
}

bool BoolFromJson(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    return it != json.end() && it->is_boolean() && it->get<bool>();
}

std::string StateToString(DownloadTaskState state) {
    switch (state) {
    case DownloadTaskState::Queued:
        return "Queued";
    case DownloadTaskState::Preparing:
        return "Preparing";
    case DownloadTaskState::Downloading:
        return "Downloading";
    case DownloadTaskState::PostProcessing:
        return "PostProcessing";
    case DownloadTaskState::Completed:
        return "Completed";
    case DownloadTaskState::Failed:
        return "Failed";
    case DownloadTaskState::Canceled:
        return "Canceled";
    }
    return "Canceled";
}

DownloadTaskState StateFromString(const std::string& state) {
    if (state == "Queued") {
        return DownloadTaskState::Queued;
    }
    if (state == "Preparing") {
        return DownloadTaskState::Preparing;
    }
    if (state == "Downloading") {
        return DownloadTaskState::Downloading;
    }
    if (state == "PostProcessing") {
        return DownloadTaskState::PostProcessing;
    }
    if (state == "Completed") {
        return DownloadTaskState::Completed;
    }
    if (state == "Failed") {
        return DownloadTaskState::Failed;
    }
    return DownloadTaskState::Canceled;
}

nlohmann::json PathArrayToJson(const std::vector<std::filesystem::path>& paths) {
    nlohmann::json array = nlohmann::json::array();
    for (const std::filesystem::path& path : paths) {
        array.push_back(PathToJsonString(path));
    }
    return array;
}

std::vector<std::filesystem::path> PathArrayFromJson(const nlohmann::json& json, const char* key) {
    std::vector<std::filesystem::path> paths;
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array()) {
        return paths;
    }
    for (const nlohmann::json& item : *it) {
        if (item.is_string()) {
            paths.push_back(PathFromUtf8(item.get<std::string>()));
        }
    }
    return paths;
}

nlohmann::json TaskToJson(const DownloadTaskSnapshot& task) {
    nlohmann::json json;
    json["id"] = task.id;
    json["yt_dlp_exe_path"] = PathToJsonString(task.request.ytDlpExePath);
    json["url"] = WideToUtf8(task.request.url);
    json["output_directory"] = PathToJsonString(task.request.outputDirectory);
    json["cookies_path"] = PathToJsonString(task.request.cookiesPath);
    json["ffmpeg_exe_path"] = PathToJsonString(task.request.ffmpegExePath);
    json["whisper_exe_path"] = PathToJsonString(task.request.whisperExePath);
    json["whisper_model_path"] = PathToJsonString(task.request.whisperModelPath);
    json["transcription_temp_dir"] = PathToJsonString(task.request.transcriptionTempDir);
    json["quality"] = WideToUtf8(task.request.quality);
    json["container"] = WideToUtf8(task.request.container);
    json["whisper_language"] = WideToUtf8(task.request.whisperLanguage);
    json["ffmpeg_available"] = task.request.ffmpegAvailable;
    json["transcribe_after_download"] = task.request.transcribeAfterDownload;
    json["title"] = WideToUtf8(task.title);
    json["thumbnail_path"] = PathToJsonString(task.thumbnailPath);
    json["state"] = StateToString(task.state);
    json["percent"] = task.percent;
    json["status_text"] = WideToUtf8(task.statusText);
    json["error_text"] = WideToUtf8(task.errorText);
    json["downloaded_bytes"] = task.downloadedBytes;
    json["total_bytes"] = task.totalBytes;
    json["speed_bytes_per_second"] = task.speedBytesPerSecond;
    json["eta_seconds"] = task.etaSeconds;
    json["media_kind"] = WideToUtf8(task.mediaKind);
    json["format_id"] = WideToUtf8(task.formatId);
    json["extension"] = WideToUtf8(task.extension);
    json["resolution"] = WideToUtf8(task.resolution);
    json["output_files"] = PathArrayToJson(task.outputFiles);
    return json;
}

std::optional<DownloadTaskSnapshot> TaskFromJson(const nlohmann::json& json) {
    if (!json.is_object()) {
        return std::nullopt;
    }
    const auto id = json.find("id");
    const auto url = json.find("url");
    if (id == json.end() || !id->is_number_integer() || id->get<int>() <= 0 ||
        url == json.end() || !url->is_string() || url->get<std::string>().empty()) {
        return std::nullopt;
    }

    DownloadTaskSnapshot task;
    task.id = id->get<int>();
    task.request.ytDlpExePath = PathFromJsonString(json, "yt_dlp_exe_path");
    task.request.url = Utf8ToWide(url->get<std::string>());
    task.request.outputDirectory = PathFromJsonString(json, "output_directory");
    task.request.cookiesPath = PathFromJsonString(json, "cookies_path");
    task.request.ffmpegExePath = PathFromJsonString(json, "ffmpeg_exe_path");
    task.request.whisperExePath = PathFromJsonString(json, "whisper_exe_path");
    task.request.whisperModelPath = PathFromJsonString(json, "whisper_model_path");
    task.request.transcriptionTempDir = PathFromJsonString(json, "transcription_temp_dir");
    task.request.quality = WStringFromJson(json, "quality", L"max");
    task.request.container = WStringFromJson(json, "container", L"auto");
    task.request.whisperLanguage = WStringFromJson(json, "whisper_language", L"auto");
    task.request.ffmpegAvailable = BoolFromJson(json, "ffmpeg_available");
    task.request.transcribeAfterDownload = BoolFromJson(json, "transcribe_after_download");
    task.title = WStringFromJson(json, "title");
    task.thumbnailPath = PathFromJsonString(json, "thumbnail_path");
    task.state = StateFromString(json.value("state", "Canceled"));
    task.percent = DoubleFromJson(json, "percent");
    task.statusText = WStringFromJson(json, "status_text");
    task.errorText = WStringFromJson(json, "error_text");
    task.downloadedBytes = UInt64FromJson(json, "downloaded_bytes");
    task.totalBytes = UInt64FromJson(json, "total_bytes");
    task.speedBytesPerSecond = UInt64FromJson(json, "speed_bytes_per_second");
    task.etaSeconds = UInt64FromJson(json, "eta_seconds");
    task.mediaKind = WStringFromJson(json, "media_kind");
    task.formatId = WStringFromJson(json, "format_id");
    task.extension = WStringFromJson(json, "extension");
    task.resolution = WStringFromJson(json, "resolution");
    task.outputFiles = PathArrayFromJson(json, "output_files");
    return task;
}

} // namespace

std::vector<DownloadTaskSnapshot> DownloadQueueStore::Load(const AppPaths& paths) {
    std::ifstream in(paths.downloadQueuePath(), std::ios::binary);
    if (!in) {
        return {};
    }

    const nlohmann::json root = nlohmann::json::parse(in, nullptr, true, true);
    if (!root.is_object() || root.value("version", 0) != 1) {
        return {};
    }

    const auto tasks = root.find("tasks");
    if (tasks == root.end() || !tasks->is_array()) {
        return {};
    }

    std::vector<DownloadTaskSnapshot> result;
    for (const nlohmann::json& item : *tasks) {
        try {
            std::optional<DownloadTaskSnapshot> task = TaskFromJson(item);
            if (task) {
                result.push_back(std::move(*task));
            }
        } catch (...) {
        }
    }
    return result;
}

void DownloadQueueStore::Save(const AppPaths& paths, const std::vector<DownloadTaskSnapshot>& tasks) {
    std::error_code ec;
    std::filesystem::create_directories(paths.stuffDir(), ec);
    if (ec) {
        throw std::runtime_error("failed to create queue store directory");
    }

    nlohmann::json root;
    root["version"] = 1;
    root["tasks"] = nlohmann::json::array();
    for (const DownloadTaskSnapshot& task : tasks) {
        root["tasks"].push_back(TaskToJson(task));
    }

    const std::filesystem::path tmpPath = paths.downloadQueuePath().wstring() + L".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open temporary queue store file");
        }
        out << root.dump(2);
        out << "\n";
    }

    std::filesystem::rename(tmpPath, paths.downloadQueuePath(), ec);
    if (ec) {
        std::filesystem::remove(paths.downloadQueuePath(), ec);
        ec.clear();
        std::filesystem::rename(tmpPath, paths.downloadQueuePath(), ec);
    }
    if (ec) {
        throw std::runtime_error("failed to replace queue store file");
    }
}

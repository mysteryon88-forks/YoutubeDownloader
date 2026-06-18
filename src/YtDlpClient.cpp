#include "YtDlpClient.h"

#include "BackendText.h"
#include "ProcessRunner.h"
#include "WinHttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwchar>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>

namespace {

constexpr const wchar_t* kOutputTemplate = L"%(title).200s [%(id)s].%(ext)s";
constexpr const wchar_t* kProgressPrefix = L"__YTDLP_PROGRESS__";

std::optional<int> HeightFromQuality(const std::wstring& quality) {
    if (quality.size() < 2 || quality.back() != L'p') {
        return std::nullopt;
    }

    int value = 0;
    for (size_t i = 0; i + 1 < quality.size(); ++i) {
        if (quality[i] < L'0' || quality[i] > L'9') {
            return std::nullopt;
        }
        value = (value * 10) + static_cast<int>(quality[i] - L'0');
    }
    return value;
}

std::wstring BuildFormatString(const std::wstring& quality, bool ffmpegAvailable) {
    if (quality == L"audio") {
        return L"bestaudio/best";
    }

    const std::optional<int> height = HeightFromQuality(quality);
    if (ffmpegAvailable) {
        if (!height.has_value()) {
            return L"bestvideo+bestaudio/best";
        }
        const std::wstring heightText = std::to_wstring(*height);
        return L"bestvideo[height<=" + heightText + L"]+bestaudio/best[height<=" + heightText + L"]/best[height<=" + heightText + L"]";
    }

    if (!height.has_value()) {
        return L"best";
    }
    const std::wstring heightText = std::to_wstring(*height);
    return L"best[height<=" + heightText + L"]/best";
}

bool IsForcedContainer(const std::wstring& container) {
    return container == L"mp4" || container == L"mkv" || container == L"webm";
}

std::uint64_t ParseUnsigned(const std::wstring& value) {
    if (value.empty() || value == L"NA" || value == L"None") {
        return 0;
    }

    wchar_t* end = nullptr;
    const unsigned long long parsed = std::wcstoull(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return 0;
    }
    return static_cast<std::uint64_t>(parsed);
}

std::map<std::wstring, std::wstring> ParseKeyValues(const std::wstring& payload) {
    std::map<std::wstring, std::wstring> values;
    std::wistringstream stream(payload);
    std::wstring token;
    while (stream >> token) {
        const size_t equals = token.find(L'=');
        if (equals == std::wstring::npos || equals == 0) {
            continue;
        }
        values[token.substr(0, equals)] = token.substr(equals + 1);
    }
    return values;
}

std::wstring StageFor(const std::wstring& status, const std::wstring& part) {
    if (status == L"finished") {
        return L"Загрузка завершена (часть)";
    }
    if (part == L"video") {
        return L"Скачивание видео:";
    }
    if (part == L"audio") {
        return L"Скачивание аудио:";
    }
    return L"Скачивание:";
}

std::wstring JsonWide(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return {};
    }
    try {
        return Utf8ToWide(it->get<std::string>());
    } catch (...) {
        return {};
    }
}

std::uint64_t JsonUInt64(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end()) {
        return 0;
    }
    if (it->is_number_unsigned()) {
        return it->get<std::uint64_t>();
    }
    if (it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();
        return value < 0 ? 0 : static_cast<std::uint64_t>(value);
    }
    return 0;
}

VideoPreview ParsePreviewObject(const nlohmann::json& object) {
    VideoPreview preview;
    if (!object.is_object()) {
        return preview;
    }

    preview.id = JsonWide(object, "id");
    preview.title = JsonWide(object, "title");
    preview.uploader = JsonWide(object, "uploader");
    preview.durationSeconds = JsonUInt64(object, "duration");
    preview.thumbnailUrl = JsonWide(object, "thumbnail");
    preview.webpageUrl = JsonWide(object, "webpage_url");
    if (preview.webpageUrl.empty()) {
        preview.webpageUrl = JsonWide(object, "url");
    }
    preview.isPlaylist = JsonWide(object, "_type") == L"playlist";

    const auto entries = object.find("entries");
    if (entries != object.end() && entries->is_array()) {
        preview.isPlaylist = true;
        for (const nlohmann::json& entry : *entries) {
            VideoPreview item = ParsePreviewObject(entry);
            if (item.webpageUrl.empty() && !item.id.empty()) {
                item.webpageUrl = L"https://www.youtube.com/watch?v=" + item.id;
            }
            preview.entries.push_back(std::move(item));
        }
    }

    return preview;
}

std::vector<std::wstring> BuildMetadataArguments(const std::wstring& url, const std::filesystem::path& cookiesPath, bool playlist) {
    std::vector<std::wstring> args;
    args.push_back(L"--dump-single-json");
    if (playlist) {
        args.push_back(L"--flat-playlist");
    } else {
        args.push_back(L"--no-playlist");
    }
    args.push_back(L"--no-warnings");
    if (!cookiesPath.empty() && std::filesystem::is_regular_file(cookiesPath)) {
        args.push_back(L"--cookies");
        args.push_back(cookiesPath.wstring());
    }
    args.push_back(url);
    return args;
}

std::filesystem::path ThumbnailPathFor(const std::filesystem::path& dir, const VideoPreview& preview) {
    if (preview.id.empty() || preview.thumbnailUrl.empty()) {
        return {};
    }

    std::wstring extension = L".jpg";
    const size_t query = preview.thumbnailUrl.find(L'?');
    const std::wstring cleanUrl = preview.thumbnailUrl.substr(0, query);
    const size_t dot = cleanUrl.find_last_of(L'.');
    if (dot != std::wstring::npos && dot + 1 < cleanUrl.size()) {
        std::wstring candidate = cleanUrl.substr(dot);
        if (candidate.size() <= 6) {
            extension = candidate;
        }
    }
    return dir / (preview.id + extension);
}

} // namespace

std::vector<std::wstring> BuildDownloadArguments(const YtDlpDownloadRequest& request) {
    std::vector<std::wstring> args;
    args.push_back(L"--newline");
    args.push_back(L"--no-warnings");
    args.push_back(L"--retries");
    args.push_back(L"10");
    args.push_back(L"--fragment-retries");
    args.push_back(L"10");

    args.push_back(L"--format");
    args.push_back(BuildFormatString(request.quality, request.ffmpegAvailable));

    args.push_back(L"--output");
    args.push_back((request.outputDirectory / kOutputTemplate).wstring());

    args.push_back(L"--progress-template");
    args.push_back(L"__YTDLP_PROGRESS__ status=%(progress.status)s downloaded=%(progress.downloaded_bytes)s total=%(progress.total_bytes)s total_estimate=%(progress.total_bytes_estimate)s speed=%(progress.speed)s eta=%(progress.eta)s part=%(info.format_note)s");

    if (!request.cookiesPath.empty() && std::filesystem::is_regular_file(request.cookiesPath)) {
        args.push_back(L"--cookies");
        args.push_back(request.cookiesPath.wstring());
    }

    if (request.ffmpegAvailable && !request.ffmpegExePath.empty()) {
        args.push_back(L"--ffmpeg-location");
        args.push_back(request.ffmpegExePath.parent_path().wstring());
    }

    if (request.ffmpegAvailable && IsForcedContainer(request.container)) {
        args.push_back(L"--merge-output-format");
        args.push_back(request.container);
    }

    args.push_back(request.url);
    return args;
}

YtDlpProgress ParseYtDlpProgressLine(const std::wstring& line) {
    YtDlpProgress progress;
    if (!line.starts_with(kProgressPrefix)) {
        return progress;
    }

    progress.recognized = true;
    std::wstring payload = line.substr(std::wcslen(kProgressPrefix));
    if (!payload.empty() && payload.front() == L' ') {
        payload.erase(payload.begin());
    }

    const std::map<std::wstring, std::wstring> values = ParseKeyValues(payload);
    auto get = [&values](const std::wstring& key) -> std::wstring {
        const auto it = values.find(key);
        return it == values.end() ? L"" : it->second;
    };

    progress.rawStatus = get(L"status");
    progress.downloadedBytes = ParseUnsigned(get(L"downloaded"));
    progress.totalBytes = ParseUnsigned(get(L"total"));
    if (progress.totalBytes == 0) {
        progress.totalBytes = ParseUnsigned(get(L"total_estimate"));
    }
    progress.speedBytesPerSecond = ParseUnsigned(get(L"speed"));
    progress.etaSeconds = ParseUnsigned(get(L"eta"));

    if (progress.totalBytes > 0 && progress.downloadedBytes <= progress.totalBytes) {
        progress.percent = (static_cast<double>(progress.downloadedBytes) / static_cast<double>(progress.totalBytes)) * 100.0;
    }

    progress.stage = StageFor(progress.rawStatus, get(L"part"));
    return progress;
}

VideoPreview ParseVideoPreviewJson(const std::string& jsonText) {
    try {
        const nlohmann::json json = nlohmann::json::parse(jsonText);
        return ParsePreviewObject(json);
    } catch (...) {
        return {};
    }
}

YtDlpClient::YtDlpClient(YtDlpClientOptions options)
    : m_options(std::move(options)) {
}

VideoPreview YtDlpClient::FetchPreview(const std::wstring& url, HANDLE cancelEvent) const {
    ProcessRunOptions options;
    options.executable = m_options.ytDlpExePath;
    options.arguments = BuildMetadataArguments(url, m_options.cookiesPath, false);
    options.timeoutMs = 30000;
    options.cancelEvent = cancelEvent;

    const ProcessRunResult result = ProcessRunner::Run(options);
    if (result.exitCode != 0) {
        throw std::runtime_error("yt-dlp preview failed");
    }
    return ParseVideoPreviewJson(WideToUtf8(result.stdoutText));
}

VideoPreview YtDlpClient::ExpandPlaylist(const std::wstring& url, HANDLE cancelEvent) const {
    ProcessRunOptions options;
    options.executable = m_options.ytDlpExePath;
    options.arguments = BuildMetadataArguments(url, m_options.cookiesPath, true);
    options.timeoutMs = 60000;
    options.cancelEvent = cancelEvent;

    const ProcessRunResult result = ProcessRunner::Run(options);
    if (result.exitCode != 0) {
        throw std::runtime_error("yt-dlp playlist expansion failed");
    }
    return ParseVideoPreviewJson(WideToUtf8(result.stdoutText));
}

std::filesystem::path YtDlpClient::CacheThumbnail(const VideoPreview& preview, HANDLE cancelEvent) const {
    const std::filesystem::path target = ThumbnailPathFor(m_options.thumbCacheDir, preview);
    if (target.empty()) {
        return {};
    }

    std::error_code ec;
    if (std::filesystem::is_regular_file(target, ec)) {
        return target;
    }
    WinHttpClient::DownloadFile(preview.thumbnailUrl, target, {}, cancelEvent);
    return target;
}

#include "YtDlpClient.h"

#include "BackendText.h"
#include "ProcessRunner.h"
#include "WinHttpClient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr const wchar_t* kOutputTemplate = L"%(title).200s [%(id)s].%(ext)s";
constexpr const wchar_t* kProgressPrefix = L"__YTDLP_PROGRESS__";
constexpr const wchar_t* kOutputPathPrefix = L"__YTDLP_OUTPUT__:";

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

std::wstring MediaKindFor(const std::wstring& part, const std::wstring& vcodec, const std::wstring& acodec) {
    if (part == L"video" || part == L"audio") {
        return part;
    }
    if (!vcodec.empty() && vcodec != L"none" && (acodec.empty() || acodec == L"none")) {
        return L"video";
    }
    if (!acodec.empty() && acodec != L"none" && (vcodec.empty() || vcodec == L"none")) {
        return L"audio";
    }
    return {};
}

std::wstring StageFor(const std::wstring& status, const std::wstring& mediaKind) {
    if (status == L"finished") {
        return L"Загрузка завершена (часть)";
    }
    if (mediaKind == L"video") {
        return L"Скачивание видео:";
    }
    if (mediaKind == L"audio") {
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

std::wstring ResolutionForHeight(const std::wstring& height) {
    if (height.empty() || height == L"NA" || height == L"none") {
        return {};
    }
    return height + L"p";
}

std::wstring LowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsLikelyMediaFile(const std::filesystem::path& path) {
    const std::wstring extension = LowerCopy(path.extension().wstring());
    static constexpr std::array<std::wstring_view, 18> kMediaExtensions = {
        L".mp4",
        L".mkv",
        L".webm",
        L".mov",
        L".avi",
        L".m4v",
        L".mpg",
        L".mpeg",
        L".ts",
        L".mp3",
        L".m4a",
        L".opus",
        L".ogg",
        L".wav",
        L".flac",
        L".aac",
        L".wma",
        L".weba"
    };
    return std::find(kMediaExtensions.begin(), kMediaExtensions.end(), extension) != kMediaExtensions.end();
}

std::wstring PathKey(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    if (ec) {
        absolutePath = path;
    }
    return LowerCopy(absolutePath.lexically_normal().wstring());
}

const OutputDirectoryFile* FindSnapshotEntry(
    const std::vector<OutputDirectoryFile>& snapshot,
    const std::filesystem::path& path
) {
    const std::wstring key = PathKey(path);
    const auto it = std::find_if(snapshot.begin(), snapshot.end(), [&key](const OutputDirectoryFile& file) {
        return PathKey(file.path) == key;
    });
    return it == snapshot.end() ? nullptr : &(*it);
}

bool LooksGdiFriendlyImageUrl(const std::wstring& url) {
    const std::wstring lower = LowerCopy(url);
    return lower.find(L".jpg") != std::wstring::npos ||
           lower.find(L".jpeg") != std::wstring::npos ||
           lower.find(L".png") != std::wstring::npos;
}

std::wstring ChooseThumbnailUrl(const nlohmann::json& object) {
    const std::wstring direct = JsonWide(object, "thumbnail");
    if (LooksGdiFriendlyImageUrl(direct)) {
        return direct;
    }

    const auto thumbnails = object.find("thumbnails");
    if (thumbnails != object.end() && thumbnails->is_array()) {
        std::wstring fallback;
        for (auto it = thumbnails->rbegin(); it != thumbnails->rend(); ++it) {
            const std::wstring url = JsonWide(*it, "url");
            if (url.empty()) {
                continue;
            }
            if (fallback.empty()) {
                fallback = url;
            }
            if (LooksGdiFriendlyImageUrl(url)) {
                return url;
            }
        }
        if (!fallback.empty()) {
            return fallback;
        }
    }

    return direct;
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
    preview.thumbnailUrl = ChooseThumbnailUrl(object);
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

std::vector<std::wstring> BuildMetadataArguments(const std::wstring& url, const std::filesystem::path& cookiesPath) {
    std::vector<std::wstring> args;
    args.push_back(L"--dump-single-json");
    args.push_back(L"--no-playlist");
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
    args.push_back(L"__YTDLP_PROGRESS__ status=%(progress.status)s downloaded=%(progress.downloaded_bytes)s total=%(progress.total_bytes)s total_estimate=%(progress.total_bytes_estimate)s speed=%(progress.speed)s eta=%(progress.eta)s part=%(info.format_note)s vcodec=%(info.vcodec)s acodec=%(info.acodec)s ext=%(info.ext)s format=%(info.format_id)s height=%(info.height)s");

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

std::filesystem::path ExtractYtDlpOutputPath(const std::wstring& line) {
    constexpr const wchar_t* kDestination = L"[download] Destination:";
    constexpr const wchar_t* kMerging = L"[Merger] Merging formats into";
    constexpr const wchar_t* kDownloadPrefix = L"[download] ";
    constexpr const wchar_t* kAlreadyDownloaded = L" has already been downloaded";

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

    if (normalized.starts_with(kOutputPathPrefix)) {
        return std::filesystem::path(normalized.substr(std::wcslen(kOutputPathPrefix)));
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
    if (normalized.starts_with(kDownloadPrefix)) {
        const size_t alreadyDownloaded = normalized.find(kAlreadyDownloaded, std::wcslen(kDownloadPrefix));
        if (alreadyDownloaded != std::wstring::npos) {
            return std::filesystem::path(normalized.substr(
                std::wcslen(kDownloadPrefix),
                alreadyDownloaded - std::wcslen(kDownloadPrefix)
            ));
        }
    }
    return {};
}

std::vector<OutputDirectoryFile> SnapshotOutputDirectory(const std::filesystem::path& directory) {
    std::vector<OutputDirectoryFile> files;
    std::error_code ec;
    if (directory.empty() || !std::filesystem::is_directory(directory, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            ec.clear();
            continue;
        }

        OutputDirectoryFile file;
        file.path = entry.path();
        file.lastWriteTime = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            file.lastWriteTime = {};
        }
        file.size = entry.file_size(ec);
        if (ec) {
            ec.clear();
            file.size = 0;
        }
        files.push_back(std::move(file));
    }
    return files;
}

std::filesystem::path FindDownloadedMediaFile(
    const std::vector<std::filesystem::path>& reportedOutputFiles,
    const std::filesystem::path& outputDirectory,
    const std::vector<OutputDirectoryFile>& beforeDownload
) {
    std::error_code ec;
    for (auto it = reportedOutputFiles.rbegin(); it != reportedOutputFiles.rend(); ++it) {
        if (IsLikelyMediaFile(*it) && std::filesystem::is_regular_file(*it, ec)) {
            return *it;
        }
        ec.clear();
    }

    if (outputDirectory.empty() || !std::filesystem::is_directory(outputDirectory, ec)) {
        return {};
    }

    std::filesystem::path bestPath;
    std::filesystem::file_time_type bestWriteTime = {};
    for (const auto& entry : std::filesystem::directory_iterator(outputDirectory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || !IsLikelyMediaFile(entry.path())) {
            ec.clear();
            continue;
        }

        const std::filesystem::file_time_type lastWriteTime = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::uintmax_t size = entry.file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }

        const OutputDirectoryFile* before = FindSnapshotEntry(beforeDownload, entry.path());
        const bool isNewOrChanged =
            before == nullptr ||
            before->lastWriteTime != lastWriteTime ||
            before->size != size;
        if (!isNewOrChanged) {
            continue;
        }
        if (bestPath.empty() || lastWriteTime > bestWriteTime) {
            bestPath = entry.path();
            bestWriteTime = lastWriteTime;
        }
    }
    return bestPath;
}

YtDlpProgress ParseYtDlpProgressLine(const std::wstring& line) {
    YtDlpProgress progress;
    std::wstring normalized = line;
    while (!normalized.empty() && std::iswspace(normalized.front())) {
        normalized.erase(normalized.begin());
    }
    if (!normalized.starts_with(kProgressPrefix)) {
        return progress;
    }

    progress.recognized = true;
    std::wstring payload = normalized.substr(std::wcslen(kProgressPrefix));
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
    if (progress.rawStatus == L"finished") {
        progress.percent = 100.0;
    }

    progress.mediaKind = MediaKindFor(get(L"part"), get(L"vcodec"), get(L"acodec"));
    progress.extension = get(L"ext");
    progress.formatId = get(L"format");
    progress.resolution = ResolutionForHeight(get(L"height"));
    progress.stage = StageFor(progress.rawStatus, progress.mediaKind);
    return progress;
}

YtDlpProcessLine ParseYtDlpProcessLine(const std::wstring& line) {
    YtDlpProcessLine parsed;
    parsed.outputPath = ExtractYtDlpOutputPath(line);
    parsed.progress = ParseYtDlpProgressLine(line);
    return parsed;
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
    options.arguments = BuildMetadataArguments(url, m_options.cookiesPath);
    options.timeoutMs = 30000;
    options.cancelEvent = cancelEvent;

    const ProcessRunResult result = ProcessRunner::Run(options);
    if (result.exitCode != 0) {
        throw std::runtime_error("yt-dlp preview failed");
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

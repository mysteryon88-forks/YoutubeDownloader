#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

struct YtDlpDownloadRequest {
    std::filesystem::path ytDlpExePath;
    std::wstring url;
    std::filesystem::path outputDirectory;
    std::filesystem::path cookiesPath;
    std::filesystem::path ffmpegExePath;
    std::wstring quality = L"max";
    std::wstring container = L"auto";
    bool ffmpegAvailable = false;
};

struct YtDlpProgress {
    bool recognized = false;
    std::wstring rawStatus;
    std::wstring stage;
    double percent = 0.0;
    std::uint64_t downloadedBytes = 0;
    std::uint64_t totalBytes = 0;
    std::uint64_t speedBytesPerSecond = 0;
    std::uint64_t etaSeconds = 0;
    std::wstring mediaKind;
    std::wstring formatId;
    std::wstring extension;
    std::wstring resolution;
};

struct VideoPreview {
    std::wstring id;
    std::wstring title;
    std::wstring uploader;
    std::uint64_t durationSeconds = 0;
    std::wstring thumbnailUrl;
    std::wstring webpageUrl;
    bool isPlaylist = false;
    std::vector<VideoPreview> entries;
    std::filesystem::path cachedThumbnailPath;
};

struct YtDlpClientOptions {
    std::filesystem::path ytDlpExePath;
    std::filesystem::path thumbCacheDir;
    std::filesystem::path cookiesPath;
};

std::vector<std::wstring> BuildDownloadArguments(const YtDlpDownloadRequest& request);
YtDlpProgress ParseYtDlpProgressLine(const std::wstring& line);
VideoPreview ParseVideoPreviewJson(const std::string& jsonText);

class YtDlpClient {
public:
    explicit YtDlpClient(YtDlpClientOptions options);

    VideoPreview FetchPreview(const std::wstring& url, HANDLE cancelEvent = nullptr) const;
    std::filesystem::path CacheThumbnail(const VideoPreview& preview, HANDLE cancelEvent = nullptr) const;

private:
    YtDlpClientOptions m_options;
};

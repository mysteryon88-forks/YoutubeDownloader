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
    std::filesystem::path whisperExePath;
    std::filesystem::path whisperModelPath;
    std::filesystem::path transcriptionTempDir;
    std::wstring quality = L"max";
    std::wstring container = L"auto";
    std::wstring whisperLanguage = L"auto";
    bool ffmpegAvailable = false;
    bool transcribeAfterDownload = false;
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

struct YtDlpProcessLine {
    std::filesystem::path outputPath;
    YtDlpProgress progress;
};

struct OutputDirectoryFile {
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime = {};
    std::uintmax_t size = 0;
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
std::filesystem::path ExtractYtDlpOutputPath(const std::wstring& line);
std::vector<OutputDirectoryFile> SnapshotOutputDirectory(const std::filesystem::path& directory);
std::filesystem::path FindDownloadedMediaFile(
    const std::vector<std::filesystem::path>& reportedOutputFiles,
    const std::filesystem::path& outputDirectory,
    const std::vector<OutputDirectoryFile>& beforeDownload
);
YtDlpProgress ParseYtDlpProgressLine(const std::wstring& line);
YtDlpProcessLine ParseYtDlpProcessLine(const std::wstring& line);
VideoPreview ParseVideoPreviewJson(const std::string& jsonText);

class YtDlpClient {
public:
    explicit YtDlpClient(YtDlpClientOptions options);

    VideoPreview FetchPreview(const std::wstring& url, HANDLE cancelEvent = nullptr) const;
    std::filesystem::path CacheThumbnail(const VideoPreview& preview, HANDLE cancelEvent = nullptr) const;

private:
    YtDlpClientOptions m_options;
};

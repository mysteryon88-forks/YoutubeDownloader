#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct YtDlpDownloadRequest {
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
};

std::vector<std::wstring> BuildDownloadArguments(const YtDlpDownloadRequest& request);
YtDlpProgress ParseYtDlpProgressLine(const std::wstring& line);

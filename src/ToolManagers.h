#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"

#include <filesystem>
#include <string>

struct ReleaseAssetInfo {
    bool found = false;
    std::wstring version;
    std::wstring pageUrl;
    std::wstring downloadUrl;
};

ReleaseAssetInfo ParseGitHubReleaseAsset(const std::string& releaseJson, const std::string& assetName);

enum class FfmpegSource {
    Missing,
    ConfiguredPath,
    LocalTools,
    Path
};

struct FfmpegStatus {
    bool available = false;
    FfmpegSource source = FfmpegSource::Missing;
    std::filesystem::path ffmpegExe;
    std::filesystem::path ffprobeExe;
    std::filesystem::path binDir;
    std::wstring message;
};

class FfmpegManager {
public:
    static FfmpegStatus Resolve(const AppPaths& paths, const AppConfig& config);
};

struct ToolInstallStatus {
    bool installed = false;
    std::filesystem::path executable;
    std::wstring version;
};

class YtDlpManager {
public:
    explicit YtDlpManager(AppPaths paths);

    ToolInstallStatus Status() const;
    ReleaseAssetInfo CheckLatestRelease() const;
    ToolInstallStatus InstallOrUpdate(HANDLE cancelEvent = nullptr) const;

private:
    AppPaths m_paths;
};

class AppUpdateService {
public:
    static ReleaseAssetInfo CheckLatestRelease();
    static std::filesystem::path DownloadUpdateZip(
        const AppPaths& paths,
        const ReleaseAssetInfo& release,
        HANDLE cancelEvent = nullptr
    );
};

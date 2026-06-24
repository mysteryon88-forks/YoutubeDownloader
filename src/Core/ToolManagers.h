#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"

#include <filesystem>
#include <functional>
#include <string>

struct ReleaseAssetInfo {
    bool found = false;
    std::wstring version;
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
    std::wstring message;
};

class FfmpegManager {
public:
    static FfmpegStatus Resolve(const AppPaths& paths, const AppConfig& config);
    static FfmpegStatus ResolveUserPath(const std::filesystem::path& path);
    static std::filesystem::path FindExtractedBinDir(const std::filesystem::path& extractedRoot);
    static std::wstring EssentialsDownloadUrl();
    static FfmpegStatus InstallEssentials(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};

struct ToolInstallStatus {
    bool installed = false;
    std::filesystem::path executable;
    std::wstring version;
};

bool ShouldInstallYtDlpUpdate(const ToolInstallStatus& current, const ReleaseAssetInfo& latest);
bool ValidateYtDlpExecutableVersion(const std::filesystem::path& executable, const std::wstring& expectedVersion);
bool ShouldInstallAppUpdate(const ReleaseAssetInfo& latest);
std::wstring BuildAppUpdatePromptMessage(const ReleaseAssetInfo& release);

class YtDlpManager {
public:
    explicit YtDlpManager(AppPaths paths);

    ToolInstallStatus Status() const;
    ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr) const;
    ToolInstallStatus InstallOrUpdate(HANDLE cancelEvent = nullptr) const;
    ToolInstallStatus InstallOrUpdate(const ReleaseAssetInfo& release, HANDLE cancelEvent = nullptr) const;

private:
    AppPaths m_paths;
};

class AppUpdateService {
public:
    static ReleaseAssetInfo CheckLatestRelease(HANDLE cancelEvent = nullptr);
    static std::filesystem::path DownloadUpdateExe(
        const AppPaths& paths,
        const ReleaseAssetInfo& release,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
    static void StartDownloadedUpdate(
        const AppPaths& paths,
        const std::filesystem::path& downloadedExe
    );
};

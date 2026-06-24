#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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
    WhisperBackend whisperBackend = WhisperBackend::Auto;
};

struct VotCliStatus {
    bool available = false;
    bool nodeAvailable = false;
    std::filesystem::path executable;
    std::filesystem::path nodeExecutable;
    std::wstring message;
};

struct ToolProcessInvocation {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
};

ToolProcessInvocation BuildVotCliInstallInvocation(const std::filesystem::path& npmExecutable);

struct WhisperModelInfo {
    std::wstring id;
    std::wstring name;
    std::wstring fileName;
    std::wstring downloadUrl;
    std::uint64_t sizeBytes = 0;
    std::wstring tags;
    std::wstring description;
    bool recommended = false;
    bool bestQuality = false;
};

bool ShouldInstallYtDlpUpdate(const ToolInstallStatus& current, const ReleaseAssetInfo& latest);
bool ValidateYtDlpExecutableVersion(const std::filesystem::path& executable, const std::wstring& expectedVersion);
bool ShouldInstallAppUpdate(const ReleaseAssetInfo& latest);
std::wstring BuildAppUpdatePromptMessage(const ReleaseAssetInfo& release);

class WhisperManager {
public:
    static const char* WindowsCpuAssetName();
    static const char* WindowsCudaAssetName();
    static const char* BackendAssetName(WhisperBackend backend);
    static std::wstring BackendDisplayName(WhisperBackend backend);
    static std::vector<WhisperModelInfo> ModelCatalog();
    static std::filesystem::path ModelPath(const AppPaths& paths, const WhisperModelInfo& model);
    static std::filesystem::path BackendInstallDir(const AppPaths& paths, WhisperBackend backend);
    static std::filesystem::path BackendExecutablePath(const AppPaths& paths, WhisperBackend backend);
    static std::filesystem::path FindExecutableDir(const std::filesystem::path& extractedRoot);
    static ToolInstallStatus ResolveBackend(const AppPaths& paths, WhisperBackend backend);
    static ToolInstallStatus Resolve(const AppPaths& paths, const AppConfig& config);
    static ReleaseAssetInfo CheckLatestRelease(WhisperBackend backend = WhisperBackend::Cpu);
    static ToolInstallStatus Install(
        const AppPaths& paths,
        WhisperBackend backend,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
    static ToolInstallStatus Install(
        const AppPaths& paths,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
    static std::filesystem::path DownloadModel(
        const AppPaths& paths,
        const WhisperModelInfo& model,
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};

class VotCliManager {
public:
    static VotCliStatus Resolve(const AppConfig& config);
    static VotCliStatus ResolveUserPath(const std::filesystem::path& path);
    static VotCliStatus InstallGlobal(
        const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};

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

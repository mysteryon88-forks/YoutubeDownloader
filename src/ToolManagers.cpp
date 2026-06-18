#include "ToolManagers.h"

#include "BackendText.h"
#include "ProcessRunner.h"
#include "WinHttpClient.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>
#include <system_error>
#include <windows.h>

namespace {

std::wstring AsciiToWide(const std::string& value) {
    try {
        return Utf8ToWide(value);
    } catch (...) {
        std::wstring out;
        out.reserve(value.size());
        for (unsigned char ch : value) {
            out.push_back(static_cast<wchar_t>(ch));
        }
        return out;
    }
}

std::wstring NormalizeVersion(std::wstring version) {
    if (!version.empty() && (version.front() == L'v' || version.front() == L'V')) {
        version.erase(version.begin());
    }
    return version;
}

bool IsExecutableFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path SiblingExe(const std::filesystem::path& ffmpegExe, const wchar_t* name) {
    return ffmpegExe.parent_path() / name;
}

FfmpegStatus MakeFfmpegStatus(FfmpegSource source, const std::filesystem::path& exe) {
    FfmpegStatus status;
    status.available = true;
    status.source = source;
    status.ffmpegExe = exe;
    status.ffprobeExe = SiblingExe(exe, L"ffprobe.exe");
    status.binDir = exe.parent_path();
    status.message = L"FFmpeg найден";
    return status;
}

FfmpegStatus MakeMissingFfmpegStatus(const std::wstring& message) {
    FfmpegStatus missing;
    missing.message = message;
    return missing;
}

std::filesystem::path SearchPathExe(const wchar_t* exeName) {
    DWORD required = SearchPathW(nullptr, exeName, nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        return {};
    }

    std::wstring buffer(required, L'\0');
    DWORD written = SearchPathW(nullptr, exeName, nullptr, required, buffer.data(), nullptr);
    if (written == 0 || written >= required) {
        return {};
    }
    buffer.resize(written);
    return std::filesystem::path(buffer);
}

std::wstring ReadTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    try {
        return Utf8ToWide(text);
    } catch (...) {
        return {};
    }
}

void WriteTextFile(const std::filesystem::path& path, const std::wstring& text) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out) {
        out << WideToUtf8(text);
    }
}

void CopyIfExists(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(source, ec)) {
        std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("failed to copy FFmpeg binary");
        }
    }
}

std::wstring QuotePowerShellLiteral(const std::filesystem::path& path) {
    std::wstring value = path.wstring();
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    escaped.push_back(L'\'');
    for (wchar_t ch : value) {
        if (ch == L'\'') {
            escaped += L"''";
        } else {
            escaped.push_back(ch);
        }
    }
    escaped.push_back(L'\'');
    return escaped;
}

} // namespace

ReleaseAssetInfo ParseGitHubReleaseAsset(const std::string& releaseJson, const std::string& assetName) {
    ReleaseAssetInfo info;

    try {
        const nlohmann::json json = nlohmann::json::parse(releaseJson);
        if (!json.is_object()) {
            return info;
        }

        info.version = NormalizeVersion(AsciiToWide(json.value("tag_name", json.value("name", ""))));
        info.pageUrl = AsciiToWide(json.value("html_url", ""));

        const auto assets = json.find("assets");
        if (assets == json.end() || !assets->is_array()) {
            return info;
        }

        for (const nlohmann::json& asset : *assets) {
            if (!asset.is_object()) {
                continue;
            }
            if (asset.value("name", "") != assetName) {
                continue;
            }
            info.downloadUrl = AsciiToWide(asset.value("browser_download_url", ""));
            info.found = !info.downloadUrl.empty();
            return info;
        }
    } catch (...) {
        return ReleaseAssetInfo{};
    }

    return info;
}

FfmpegStatus FfmpegManager::Resolve(const AppPaths& paths, const AppConfig& config) {
    const FfmpegStatus configured = ResolveUserPath(config.ffmpegPath);
    if (configured.available) {
        FfmpegStatus status = configured;
        status.source = FfmpegSource::ConfiguredPath;
        return status;
    }

    if (IsExecutableFile(paths.localFfmpegExePath())) {
        return MakeFfmpegStatus(FfmpegSource::LocalTools, paths.localFfmpegExePath());
    }

    const std::filesystem::path pathExe = SearchPathExe(L"ffmpeg.exe");
    if (IsExecutableFile(pathExe)) {
        return MakeFfmpegStatus(FfmpegSource::Path, pathExe);
    }

    return MakeMissingFfmpegStatus(L"FFmpeg не найден");
}

FfmpegStatus FfmpegManager::ResolveUserPath(const std::filesystem::path& path) {
    if (path.empty()) {
        return MakeMissingFfmpegStatus(L"Путь FFmpeg не задан");
    }

    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && path.filename() == L"ffmpeg.exe") {
        return MakeFfmpegStatus(FfmpegSource::ConfiguredPath, path);
    }

    if (std::filesystem::is_directory(path, ec)) {
        const std::filesystem::path direct = path / L"ffmpeg.exe";
        if (IsExecutableFile(direct)) {
            return MakeFfmpegStatus(FfmpegSource::ConfiguredPath, direct);
        }

        const std::filesystem::path bin = path / L"bin" / L"ffmpeg.exe";
        if (IsExecutableFile(bin)) {
            return MakeFfmpegStatus(FfmpegSource::ConfiguredPath, bin);
        }
    }

    return MakeMissingFfmpegStatus(L"В выбранном пути не найден ffmpeg.exe");
}

std::filesystem::path FfmpegManager::FindExtractedBinDir(const std::filesystem::path& extractedRoot) {
    std::error_code ec;
    if (!std::filesystem::is_directory(extractedRoot, ec)) {
        return {};
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(extractedRoot, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (entry.path().filename() == L"ffmpeg.exe") {
            return entry.path().parent_path();
        }
    }
    return {};
}

std::wstring FfmpegManager::EssentialsDownloadUrl() {
    return L"https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip";
}

FfmpegStatus FfmpegManager::InstallEssentials(
    const AppPaths& paths,
    const std::function<void(std::uint64_t downloaded, std::uint64_t total, const std::wstring& status)>& onProgress,
    HANDLE cancelEvent
) {
    const std::filesystem::path archiveTmp = paths.stuffDir() / L"ffmpeg-release-essentials.zip.tmp";
    const std::filesystem::path archive = paths.stuffDir() / L"ffmpeg-release-essentials.zip";
    const std::filesystem::path extractDir = paths.stuffDir() / L"ffmpeg_extract";

    std::error_code ec;
    std::filesystem::create_directories(paths.stuffDir(), ec);
    std::filesystem::remove(archiveTmp, ec);
    std::filesystem::remove(archive, ec);
    std::filesystem::remove_all(extractDir, ec);

    if (onProgress) {
        onProgress(0, 0, L"Скачивание FFmpeg...");
    }

    WinHttpClient::DownloadFile(
        EssentialsDownloadUrl(),
        archiveTmp,
        [onProgress](std::uint64_t downloaded, std::uint64_t total) {
            if (onProgress) {
                onProgress(downloaded, total, L"Скачивание FFmpeg...");
            }
        },
        cancelEvent
    );

    if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
        throw std::runtime_error("operation canceled");
    }

    std::filesystem::rename(archiveTmp, archive, ec);
    if (ec) {
        throw std::runtime_error("failed to finalize FFmpeg archive");
    }

    std::filesystem::create_directories(extractDir, ec);
    if (onProgress) {
        onProgress(0, 0, L"Распаковка FFmpeg...");
    }

    ProcessRunOptions options;
    options.executable = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    if (!std::filesystem::is_regular_file(options.executable, ec)) {
        options.executable = L"powershell.exe";
    }
    options.arguments = {
        L"-NoProfile",
        L"-ExecutionPolicy",
        L"Bypass",
        L"-Command",
        L"Expand-Archive -LiteralPath " + QuotePowerShellLiteral(archive) +
            L" -DestinationPath " + QuotePowerShellLiteral(extractDir) + L" -Force"
    };
    options.timeoutMs = 120000;
    options.cancelEvent = cancelEvent;

    const ProcessRunResult extract = ProcessRunner::Run(options);
    if (extract.canceled) {
        throw std::runtime_error("operation canceled");
    }
    if (extract.exitCode != 0) {
        throw std::runtime_error("failed to extract FFmpeg archive");
    }

    const std::filesystem::path extractedBin = FindExtractedBinDir(extractDir);
    if (extractedBin.empty()) {
        throw std::runtime_error("ffmpeg.exe was not found in extracted archive");
    }

    if (onProgress) {
        onProgress(0, 0, L"Установка FFmpeg...");
    }

    std::filesystem::create_directories(paths.localFfmpegBinDir(), ec);
    CopyIfExists(extractedBin / L"ffmpeg.exe", paths.localFfmpegExePath());
    CopyIfExists(extractedBin / L"ffprobe.exe", paths.localFfprobeExePath());
    CopyIfExists(extractedBin / L"ffplay.exe", paths.localFfplayExePath());

    FfmpegStatus status = ResolveUserPath(paths.localFfmpegExePath());
    if (!status.available) {
        throw std::runtime_error("installed FFmpeg could not be resolved");
    }
    status.source = FfmpegSource::LocalTools;
    return status;
}

YtDlpManager::YtDlpManager(AppPaths paths)
    : m_paths(std::move(paths)) {
}

ToolInstallStatus YtDlpManager::Status() const {
    ToolInstallStatus status;
    status.executable = m_paths.ytDlpExePath();
    status.version = ReadTextFile(m_paths.ytDlpVersionPath());
    status.installed = IsExecutableFile(status.executable) && !status.version.empty();
    return status;
}

ReleaseAssetInfo YtDlpManager::CheckLatestRelease() const {
    const std::string json = WinHttpClient::GetString(L"https://api.github.com/repos/yt-dlp/yt-dlp/releases/latest");
    return ParseGitHubReleaseAsset(json, "yt-dlp.exe");
}

ToolInstallStatus YtDlpManager::InstallOrUpdate(HANDLE cancelEvent) const {
    const ReleaseAssetInfo release = CheckLatestRelease();
    if (!release.found) {
        throw std::runtime_error("yt-dlp release asset was not found");
    }

    const std::filesystem::path tmpPath = m_paths.ytDlpExePath().wstring() + L".tmp";
    std::error_code ec;
    std::filesystem::remove(tmpPath, ec);

    WinHttpClient::DownloadFile(release.downloadUrl, tmpPath, {}, cancelEvent);

    std::filesystem::remove(m_paths.ytDlpExePath(), ec);
    ec.clear();
    std::filesystem::rename(tmpPath, m_paths.ytDlpExePath(), ec);
    if (ec) {
        throw std::runtime_error("failed to replace yt-dlp executable");
    }
    WriteTextFile(m_paths.ytDlpVersionPath(), release.version);
    return Status();
}

ReleaseAssetInfo AppUpdateService::CheckLatestRelease() {
    const std::string json = WinHttpClient::GetString(L"https://api.github.com/repos/Laynholt/YoutubeDownloader/releases/latest");
    return ParseGitHubReleaseAsset(json, "YoutubeDownloader_windows.zip");
}

std::filesystem::path AppUpdateService::DownloadUpdateZip(
    const AppPaths& paths,
    const ReleaseAssetInfo& release,
    HANDLE cancelEvent
) {
    if (!release.found || release.downloadUrl.empty()) {
        throw std::runtime_error("app update release asset was not found");
    }

    const std::filesystem::path target = paths.stuffDir() / L"updates" / L"YoutubeDownloader_windows.zip";
    WinHttpClient::DownloadFile(release.downloadUrl, target, {}, cancelEvent);
    return target;
}

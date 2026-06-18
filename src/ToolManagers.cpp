#include "ToolManagers.h"

#include "BackendText.h"
#include "WinHttpClient.h"

#include <nlohmann/json.hpp>

#include <fstream>
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
    if (IsExecutableFile(config.ffmpegPath)) {
        return MakeFfmpegStatus(FfmpegSource::ConfiguredPath, config.ffmpegPath);
    }

    if (IsExecutableFile(paths.localFfmpegExePath())) {
        return MakeFfmpegStatus(FfmpegSource::LocalTools, paths.localFfmpegExePath());
    }

    const std::filesystem::path pathExe = SearchPathExe(L"ffmpeg.exe");
    if (IsExecutableFile(pathExe)) {
        return MakeFfmpegStatus(FfmpegSource::Path, pathExe);
    }

    FfmpegStatus missing;
    missing.message = L"FFmpeg не найден";
    return missing;
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

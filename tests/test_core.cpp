#include "AppPaths.h"
#include "Config.h"
#include "ToolManagers.h"
#include "Version.h"
#include "YtDlpClient.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

void Require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

fs::path MakeTempRoot(const std::wstring& name) {
    std::error_code ec;
    fs::path root = fs::temp_directory_path(ec) / name;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    Require(!ec, "failed to create temp root");
    return root;
}

void TestAppPaths() {
    const fs::path root = fs::path(L"C:/Portable/YoutubeDownloader");
    const AppPaths paths(root);

    Require(paths.root() == root, "root path mismatch");
    Require(paths.assetsDir() == root / L"assets", "assets path mismatch");
    Require(paths.stuffDir() == root / L"stuff", "stuff path mismatch");
    Require(paths.configPath() == root / L"stuff" / L"config.ini", "config path mismatch");
    Require(paths.logPath() == root / L"stuff" / L"ytdl.log", "log path mismatch");
    Require(paths.thumbCacheDir() == root / L"stuff" / L"thumb_cache", "thumb cache path mismatch");
    Require(paths.toolsDir() == root / L"tools", "tools path mismatch");
    Require(paths.ytDlpDir() == root / L"tools" / L"yt-dlp", "yt-dlp dir path mismatch");
    Require(paths.ytDlpExePath() == root / L"tools" / L"yt-dlp" / L"yt-dlp.exe", "yt-dlp path mismatch");
    Require(paths.ytDlpVersionPath() == root / L"tools" / L"yt-dlp" / L"version.txt", "yt-dlp version path mismatch");
    Require(paths.localFfmpegBinDir() == root / L"tools" / L"ffmpeg" / L"bin", "ffmpeg bin path mismatch");
    Require(paths.localFfmpegExePath() == root / L"tools" / L"ffmpeg" / L"bin" / L"ffmpeg.exe", "ffmpeg path mismatch");
    Require(paths.localFfprobeExePath() == root / L"tools" / L"ffmpeg" / L"bin" / L"ffprobe.exe", "ffprobe path mismatch");
}

void TestConfigDefaultsAndRoundTrip() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_Config");
    const AppPaths paths(root);
    const AppConfig defaults = ConfigStore::Load(paths);

    Require(defaults.quality == L"max", "default quality mismatch");
    Require(defaults.container == L"auto", "default container mismatch");
    Require(defaults.maxParallelDownloads == 3, "default max parallel mismatch");
    Require(defaults.autoUpdateApp == false, "default app auto update mismatch");
    Require(defaults.ffmpegPromptDismissed == false, "default ffmpeg prompt mismatch");
    Require(!defaults.downloadDir.empty(), "default download dir is empty");

    AppConfig saved = defaults;
    saved.downloadDir = root / L"Downloads";
    saved.cookiesPath = root / L"cookies.txt";
    saved.ffmpegPath = root / L"ffmpeg.exe";
    saved.quality = L"720p";
    saved.container = L"mp4";
    saved.maxParallelDownloads = 5;
    saved.autoUpdateApp = true;
    saved.lastYtDlpCheckAt = L"2026-06-17T20:00:00Z";
    saved.lastYtDlpVersion = L"2026.06.09";
    saved.ffmpegPromptDismissed = true;

    ConfigStore::Save(paths, saved);
    Require(fs::is_regular_file(paths.configPath()), "config file was not written");

    const AppConfig loaded = ConfigStore::Load(paths);
    Require(loaded.downloadDir == saved.downloadDir, "download dir round-trip mismatch");
    Require(loaded.cookiesPath == saved.cookiesPath, "cookies path round-trip mismatch");
    Require(loaded.ffmpegPath == saved.ffmpegPath, "ffmpeg path round-trip mismatch");
    Require(loaded.quality == L"720p", "quality round-trip mismatch");
    Require(loaded.container == L"mp4", "container round-trip mismatch");
    Require(loaded.maxParallelDownloads == 5, "max parallel round-trip mismatch");
    Require(loaded.autoUpdateApp == true, "auto update round-trip mismatch");
    Require(loaded.lastYtDlpCheckAt == L"2026-06-17T20:00:00Z", "yt-dlp check timestamp mismatch");
    Require(loaded.lastYtDlpVersion == L"2026.06.09", "yt-dlp version mismatch");
    Require(loaded.ffmpegPromptDismissed == true, "ffmpeg prompt round-trip mismatch");
}

void TestVersionCompare() {
    Require(CompareVersions(L"2026.06.09", L"2026.06.10") < 0, "date version compare mismatch");
    Require(CompareVersions(L"1.2.0", L"1.2") == 0, "missing patch compare mismatch");
    Require(CompareVersions(L"v1.10.0", L"1.2.99") > 0, "numeric compare mismatch");
    Require(CompareVersions(L"2025.01.01", L"2025.01.01") == 0, "equal compare mismatch");
}

bool ContainsArg(const std::vector<std::wstring>& args, const std::wstring& value) {
    return std::ranges::find(args, value) != args.end();
}

size_t ArgIndex(const std::vector<std::wstring>& args, const std::wstring& value) {
    const auto it = std::ranges::find(args, value);
    Require(it != args.end(), "argument not found");
    return static_cast<size_t>(std::distance(args.begin(), it));
}

void TestYtDlpDownloadArguments() {
    const fs::path root = MakeTempRoot(L"YoutubeDownloaderTests_YtDlpArgs");
    const fs::path cookies = root / L"cookies.txt";
    {
        std::ofstream out(cookies);
        out << "# Netscape HTTP Cookie File\n";
    }

    YtDlpDownloadRequest request;
    request.url = L"https://www.youtube.com/watch?v=test";
    request.outputDirectory = root / L"Downloads";
    request.quality = L"720p";
    request.container = L"mp4";
    request.cookiesPath = cookies;
    request.ffmpegExePath = root / L"tools" / L"ffmpeg" / L"bin" / L"ffmpeg.exe";
    request.ffmpegAvailable = true;

    const std::vector<std::wstring> args = BuildDownloadArguments(request);

    Require(ContainsArg(args, L"--cookies"), "cookies argument missing");
    Require(args.at(ArgIndex(args, L"--cookies") + 1) == cookies.wstring(), "cookies path argument mismatch");
    Require(ContainsArg(args, L"--ffmpeg-location"), "ffmpeg location argument missing");
    Require(args.at(ArgIndex(args, L"--ffmpeg-location") + 1) == request.ffmpegExePath.parent_path().wstring(), "ffmpeg location mismatch");
    Require(ContainsArg(args, L"--merge-output-format"), "merge output format missing");
    Require(args.at(ArgIndex(args, L"--merge-output-format") + 1) == L"mp4", "merge output format value mismatch");
    Require(ContainsArg(args, L"--progress-template"), "progress template missing");
    Require(ContainsArg(args, L"--output"), "output template missing");
    Require(args.at(ArgIndex(args, L"--output") + 1).find(L"%(title).200s [%(id)s].%(ext)s") != std::wstring::npos, "output template mismatch");
    Require(args.at(ArgIndex(args, L"--format") + 1).find(L"height<=720") != std::wstring::npos, "720p format mismatch");
    Require(args.back() == request.url, "url should be last argument");

    request.quality = L"audio";
    request.container = L"auto";
    request.cookiesPath.clear();
    const std::vector<std::wstring> audioArgs = BuildDownloadArguments(request);
    Require(args.at(ArgIndex(args, L"--format") + 1) != audioArgs.at(ArgIndex(audioArgs, L"--format") + 1), "audio format did not change");
    Require(audioArgs.at(ArgIndex(audioArgs, L"--format") + 1) == L"bestaudio/best", "audio format mismatch");
    Require(!ContainsArg(audioArgs, L"--cookies"), "cookies should be omitted when path is empty");
    Require(!ContainsArg(audioArgs, L"--merge-output-format"), "auto container should not force remux");
}

void TestYtDlpProgressParsing() {
    const std::wstring line =
        L"__YTDLP_PROGRESS__ status=downloading downloaded=512 total=1024 total_estimate=1024 speed=2048 eta=5 part=video";
    const YtDlpProgress progress = ParseYtDlpProgressLine(line);

    Require(progress.recognized, "progress line was not recognized");
    Require(progress.rawStatus == L"downloading", "progress status mismatch");
    Require(progress.stage == L"Скачивание видео:", "progress stage mismatch");
    Require(progress.percent == 50.0, "progress percent mismatch");
    Require(progress.speedBytesPerSecond == 2048, "progress speed mismatch");
    Require(progress.etaSeconds == 5, "progress eta mismatch");
    Require(progress.totalBytes == 1024, "progress total mismatch");

    const YtDlpProgress ignored = ParseYtDlpProgressLine(L"[download] 50.0% of 1.00MiB");
    Require(!ignored.recognized, "plain yt-dlp line should not be recognized as machine progress");
}

void TestGitHubReleaseParsing() {
    const std::string ytDlpRelease = R"json(
{
  "tag_name": "2026.06.09",
  "html_url": "https://github.com/yt-dlp/yt-dlp/releases/tag/2026.06.09",
  "assets": [
    {"name": "SHA2-256SUMS", "browser_download_url": "https://example.invalid/sums"},
    {"name": "yt-dlp.exe", "browser_download_url": "https://example.invalid/yt-dlp.exe"}
  ]
}
)json";

    const ReleaseAssetInfo ytDlp = ParseGitHubReleaseAsset(ytDlpRelease, "yt-dlp.exe");
    Require(ytDlp.found, "yt-dlp asset not found");
    Require(ytDlp.version == L"2026.06.09", "yt-dlp release version mismatch");
    Require(ytDlp.pageUrl == L"https://github.com/yt-dlp/yt-dlp/releases/tag/2026.06.09", "yt-dlp page url mismatch");
    Require(ytDlp.downloadUrl == L"https://example.invalid/yt-dlp.exe", "yt-dlp asset url mismatch");

    const std::string appRelease = R"json(
{
  "tag_name": "v1.0.4",
  "html_url": "https://github.com/Laynholt/YoutubeDownloader/releases/tag/v1.0.4",
  "assets": [
    {"name": "YoutubeDownloader_windows.zip", "browser_download_url": "https://example.invalid/YoutubeDownloader_windows.zip"}
  ]
}
)json";

    const ReleaseAssetInfo app = ParseGitHubReleaseAsset(appRelease, "YoutubeDownloader_windows.zip");
    Require(app.found, "app update asset not found");
    Require(app.version == L"1.0.4", "app release version should be normalized");
    Require(app.downloadUrl == L"https://example.invalid/YoutubeDownloader_windows.zip", "app asset url mismatch");

    const ReleaseAssetInfo missing = ParseGitHubReleaseAsset(appRelease, "missing.zip");
    Require(!missing.found, "missing asset should not be found");
}

} // namespace

int main() {
    TestAppPaths();
    TestConfigDefaultsAndRoundTrip();
    TestVersionCompare();
    TestYtDlpDownloadArguments();
    TestYtDlpProgressParsing();
    TestGitHubReleaseParsing();
    return 0;
}

#include "Config.h"

#include "BackendText.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace {

std::string PathToJsonString(const std::filesystem::path& path) {
    return PathToUtf8(path);
}

std::filesystem::path PathFromJsonString(const nlohmann::json& json, const char* key, const std::filesystem::path& fallback = {}) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return PathFromUtf8(it->get<std::string>());
}

std::wstring WStringFromJson(const nlohmann::json& json, const char* key, const std::wstring& fallback = L"") {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return Utf8ToWide(it->get<std::string>());
}

int IntFromJson(const nlohmann::json& json, const char* key, int fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) {
        return fallback;
    }
    return it->get<int>();
}

bool BoolFromJson(const nlohmann::json& json, const char* key, bool fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::filesystem::path DefaultDownloadDir() {
    wchar_t* profile = nullptr;
    size_t profileLength = 0;
    if (_wdupenv_s(&profile, &profileLength, L"USERPROFILE") == 0 && profile && profileLength > 0) {
        std::filesystem::path result = std::filesystem::path(profile) / L"Downloads";
        free(profile);
        return result;
    }
    free(profile);
    return std::filesystem::current_path() / L"Downloads";
}

} // namespace

AppConfig ConfigStore::Defaults() {
    AppConfig config;
    config.downloadDir = DefaultDownloadDir();
    return config;
}

AppConfig ConfigStore::Load(const AppPaths& paths) {
    AppConfig config = Defaults();

    std::ifstream in(paths.configPath(), std::ios::binary);
    if (!in) {
        return config;
    }

    try {
        const nlohmann::json json = nlohmann::json::parse(in, nullptr, true, true);
        if (!json.is_object()) {
            return config;
        }

        config.downloadDir = PathFromJsonString(json, "download_dir", config.downloadDir);
        config.cookiesPath = PathFromJsonString(json, "cookies_path", config.cookiesPath);
        config.ffmpegPath = PathFromJsonString(json, "ffmpeg_path", config.ffmpegPath);
        config.quality = WStringFromJson(json, "quality", config.quality);
        config.container = WStringFromJson(json, "container", config.container);
        config.maxParallelDownloads = IntFromJson(json, "max_parallel_downloads", config.maxParallelDownloads);
        config.maxParallelDownloads = std::clamp(config.maxParallelDownloads, 3, 10);
        config.autoUpdateApp = BoolFromJson(json, "auto_update_app", config.autoUpdateApp);
        config.lastYtDlpCheckAt = WStringFromJson(json, "last_ytdlp_check_at", config.lastYtDlpCheckAt);
        config.lastYtDlpVersion = WStringFromJson(json, "last_ytdlp_version", config.lastYtDlpVersion);
        config.ffmpegPromptDismissed = BoolFromJson(json, "ffmpeg_prompt_dismissed", config.ffmpegPromptDismissed);
    } catch (...) {
        return Defaults();
    }

    return config;
}

void ConfigStore::Save(const AppPaths& paths, const AppConfig& config) {
    std::error_code ec;
    std::filesystem::create_directories(paths.stuffDir(), ec);
    if (ec) {
        throw std::runtime_error("failed to create config directory");
    }

    nlohmann::json json;
    json["download_dir"] = PathToJsonString(config.downloadDir);
    json["cookies_path"] = PathToJsonString(config.cookiesPath);
    json["ffmpeg_path"] = PathToJsonString(config.ffmpegPath);
    json["quality"] = WideToUtf8(config.quality);
    json["container"] = WideToUtf8(config.container);
    json["max_parallel_downloads"] = config.maxParallelDownloads;
    json["auto_update_app"] = config.autoUpdateApp;
    json["last_ytdlp_check_at"] = WideToUtf8(config.lastYtDlpCheckAt);
    json["last_ytdlp_version"] = WideToUtf8(config.lastYtDlpVersion);
    json["ffmpeg_prompt_dismissed"] = config.ffmpegPromptDismissed;

    const std::filesystem::path tmpPath = paths.configPath().wstring() + L".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("failed to open temporary config file");
        }
        out << json.dump(2);
        out << "\n";
    }

    std::filesystem::rename(tmpPath, paths.configPath(), ec);
    if (ec) {
        std::filesystem::remove(paths.configPath(), ec);
        ec.clear();
        std::filesystem::rename(tmpPath, paths.configPath(), ec);
    }
    if (ec) {
        throw std::runtime_error("failed to replace config file");
    }
}

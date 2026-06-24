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

WhisperBackend WhisperBackendFromJson(const nlohmann::json& json, const char* key, WhisperBackend fallback) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return fallback;
    }
    return WhisperBackendFromConfigValue(Utf8ToWide(it->get<std::string>()));
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

std::wstring WhisperBackendToConfigValue(WhisperBackend backend) {
    switch (backend) {
    case WhisperBackend::Cpu:
        return L"cpu";
    case WhisperBackend::Cuda:
        return L"cuda";
    case WhisperBackend::Custom:
        return L"custom";
    case WhisperBackend::Auto:
    default:
        return L"auto";
    }
}

WhisperBackend WhisperBackendFromConfigValue(const std::wstring& value) {
    if (value == L"cpu") {
        return WhisperBackend::Cpu;
    }
    if (value == L"cuda") {
        return WhisperBackend::Cuda;
    }
    if (value == L"custom") {
        return WhisperBackend::Custom;
    }
    return WhisperBackend::Auto;
}

AppConfig ConfigStore::Defaults() {
    AppConfig config;
    config.downloadDir = DefaultDownloadDir();
    return config;
}

AppConfig ConfigStore::Load(const AppPaths& paths) {
    AppConfig config = Defaults();

    std::ifstream in(paths.configPath(), std::ios::binary);
    if (!in) {
        Save(paths, config);
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
        config.whisperPath = PathFromJsonString(json, "whisper_path", config.whisperPath);
        config.whisperModelPath = PathFromJsonString(json, "whisper_model_path", config.whisperModelPath);
        config.whisperBackend = WhisperBackendFromJson(json, "whisper_backend", config.whisperBackend);
        config.votCliPath = PathFromJsonString(json, "vot_cli_path", config.votCliPath);
        config.quality = WStringFromJson(json, "quality", config.quality);
        config.container = WStringFromJson(json, "container", config.container);
        config.whisperLanguage = WStringFromJson(json, "whisper_language", config.whisperLanguage);
        config.voiceOverLanguage = WStringFromJson(json, "voice_over_language", config.voiceOverLanguage);
        config.voiceOverMode = WStringFromJson(json, "voice_over_mode", config.voiceOverMode);
        if (config.voiceOverMode != L"mixed") {
            config.voiceOverMode = L"separate";
        }
        config.originalVolumePercent = IntFromJson(json, "original_volume_percent", config.originalVolumePercent);
        config.originalVolumePercent = std::clamp(config.originalVolumePercent, 0, 100);
        config.maxParallelDownloads = IntFromJson(json, "max_parallel_downloads", config.maxParallelDownloads);
        config.maxParallelDownloads = std::clamp(config.maxParallelDownloads, 3, 10);
        config.autoUpdateApp = BoolFromJson(json, "auto_update_app", config.autoUpdateApp);
        config.transcribeAfterDownload = BoolFromJson(json, "transcribe_after_download", config.transcribeAfterDownload);
        config.lastYtDlpCheckAt = WStringFromJson(json, "last_ytdlp_check_at", config.lastYtDlpCheckAt);
        config.lastYtDlpVersion = WStringFromJson(json, "last_ytdlp_version", config.lastYtDlpVersion);
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
    json["whisper_path"] = PathToJsonString(config.whisperPath);
    json["whisper_model_path"] = PathToJsonString(config.whisperModelPath);
    json["whisper_backend"] = WideToUtf8(WhisperBackendToConfigValue(config.whisperBackend));
    json["vot_cli_path"] = PathToJsonString(config.votCliPath);
    json["quality"] = WideToUtf8(config.quality);
    json["container"] = WideToUtf8(config.container);
    json["whisper_language"] = WideToUtf8(config.whisperLanguage);
    json["voice_over_language"] = WideToUtf8(config.voiceOverLanguage);
    json["voice_over_mode"] = WideToUtf8(config.voiceOverMode == L"mixed" ? L"mixed" : L"separate");
    json["original_volume_percent"] = std::clamp(config.originalVolumePercent, 0, 100);
    json["max_parallel_downloads"] = config.maxParallelDownloads;
    json["auto_update_app"] = config.autoUpdateApp;
    json["transcribe_after_download"] = config.transcribeAfterDownload;
    json["last_ytdlp_check_at"] = WideToUtf8(config.lastYtDlpCheckAt);
    json["last_ytdlp_version"] = WideToUtf8(config.lastYtDlpVersion);

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

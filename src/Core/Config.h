#pragma once

#include "AppPaths.h"

#include <filesystem>
#include <string>

enum class WhisperBackend {
    Auto,
    Cpu,
    Cuda,
    Custom
};

std::wstring WhisperBackendToConfigValue(WhisperBackend backend);
WhisperBackend WhisperBackendFromConfigValue(const std::wstring& value);

struct AppConfig {
    std::filesystem::path downloadDir;
    std::filesystem::path cookiesPath;
    std::filesystem::path ffmpegPath;
    std::filesystem::path whisperPath;
    std::filesystem::path whisperModelPath;
    WhisperBackend whisperBackend = WhisperBackend::Auto;
    std::filesystem::path votCliPath;
    std::wstring quality = L"max";
    std::wstring container = L"auto";
    std::wstring whisperLanguage = L"auto";
    std::wstring voiceOverLanguage = L"ru";
    std::wstring voiceOverMode = L"separate";
    int originalVolumePercent = 25;
    int maxParallelDownloads = 3;
    bool autoUpdateApp = true;
    bool transcribeAfterDownload = false;
    std::wstring lastYtDlpCheckAt;
    std::wstring lastYtDlpVersion;
};

class ConfigStore {
public:
    static AppConfig Load(const AppPaths& paths);
    static void Save(const AppPaths& paths, const AppConfig& config);

private:
    static AppConfig Defaults();
};

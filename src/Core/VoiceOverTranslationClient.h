#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct VoiceOverTranslationRequest {
    std::filesystem::path mediaPath;
    std::filesystem::path tempDirectory;
    std::filesystem::path ffmpegExePath;
    std::filesystem::path votCliPath;
    std::wstring youtubeUrl;
    std::wstring language = L"ru";
    std::wstring mode = L"separate";
    int originalVolumePercent = 25;
};

struct VoiceOverTranslationCallbacks {
    std::function<void(const std::wstring&)> onStatus;
    std::function<void(double percent, const std::wstring& status)> onProgress;
};

struct VoiceOverTranslationResult {
    bool success = false;
    bool canceled = false;
    std::filesystem::path audioPath;
    std::filesystem::path videoPath;
    std::wstring errorText;
};

struct VoiceOverTranslationPaths {
    std::filesystem::path tempAudioPath;
    std::filesystem::path finalAudioPath;
    std::filesystem::path finalVideoPath;
};

struct VoiceOverProcessInvocation {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
};

VoiceOverTranslationPaths BuildVoiceOverPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    const std::wstring& language,
    const std::wstring& mode
);
std::vector<std::wstring> BuildVotCliArguments(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
);
VoiceOverProcessInvocation BuildVotCliInvocation(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
);
std::vector<std::wstring> BuildVoiceOverMuxArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    const std::wstring& language
);
std::vector<std::wstring> BuildVoiceOverMixArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    int originalVolumePercent
);

class VoiceOverTranslationClient {
public:
    static VoiceOverTranslationResult Translate(
        const VoiceOverTranslationRequest& request,
        const VoiceOverTranslationCallbacks& callbacks = {},
        HANDLE cancelEvent = nullptr
    );
};

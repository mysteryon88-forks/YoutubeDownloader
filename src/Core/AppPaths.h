#pragma once

#include <filesystem>

class AppPaths {
public:
    explicit AppPaths(std::filesystem::path root);

    const std::filesystem::path& root() const;
    std::filesystem::path stuffDir() const;
    std::filesystem::path configPath() const;
    std::filesystem::path logPath() const;
    std::filesystem::path downloadQueuePath() const;
    std::filesystem::path thumbCacheDir() const;
    std::filesystem::path toolsDir() const;
    std::filesystem::path ytDlpDir() const;
    std::filesystem::path ytDlpExePath() const;
    std::filesystem::path ytDlpVersionPath() const;
    std::filesystem::path localFfmpegBinDir() const;
    std::filesystem::path localFfmpegExePath() const;
    std::filesystem::path localFfprobeExePath() const;
    std::filesystem::path localFfplayExePath() const;
    std::filesystem::path localWhisperDir() const;
    std::filesystem::path localWhisperExePath() const;
    std::filesystem::path localWhisperCpuDir() const;
    std::filesystem::path localWhisperCudaDir() const;
    std::filesystem::path localWhisperCpuExePath() const;
    std::filesystem::path localWhisperCudaExePath() const;
    std::filesystem::path localWhisperModelsDir() const;
    std::filesystem::path localWhisperModelPath() const;
    std::filesystem::path transcriptionTempDir() const;
    std::filesystem::path voiceOverTempDir() const;

private:
    std::filesystem::path m_root;
};

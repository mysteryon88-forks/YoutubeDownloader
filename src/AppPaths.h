#pragma once

#include <filesystem>

class AppPaths {
public:
    explicit AppPaths(std::filesystem::path root);

    const std::filesystem::path& root() const;
    std::filesystem::path assetsDir() const;
    std::filesystem::path stuffDir() const;
    std::filesystem::path configPath() const;
    std::filesystem::path logPath() const;
    std::filesystem::path thumbCacheDir() const;
    std::filesystem::path toolsDir() const;
    std::filesystem::path ytDlpDir() const;
    std::filesystem::path ytDlpExePath() const;
    std::filesystem::path ytDlpVersionPath() const;
    std::filesystem::path localFfmpegBinDir() const;
    std::filesystem::path localFfmpegExePath() const;
    std::filesystem::path localFfprobeExePath() const;
    std::filesystem::path localFfplayExePath() const;

private:
    std::filesystem::path m_root;
};

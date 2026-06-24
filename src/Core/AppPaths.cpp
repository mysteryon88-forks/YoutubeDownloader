#include "AppPaths.h"

AppPaths::AppPaths(std::filesystem::path root)
    : m_root(std::move(root)) {
}

const std::filesystem::path& AppPaths::root() const {
    return m_root;
}

std::filesystem::path AppPaths::stuffDir() const {
    return m_root / L"stuff";
}

std::filesystem::path AppPaths::configPath() const {
    return stuffDir() / L"config.ini";
}

std::filesystem::path AppPaths::logPath() const {
    return stuffDir() / L"ytdl.log";
}

std::filesystem::path AppPaths::downloadQueuePath() const {
    return stuffDir() / L"download_queue.json";
}

std::filesystem::path AppPaths::thumbCacheDir() const {
    return stuffDir() / L"thumb_cache";
}

std::filesystem::path AppPaths::toolsDir() const {
    return m_root / L"tools";
}

std::filesystem::path AppPaths::ytDlpDir() const {
    return toolsDir() / L"yt-dlp";
}

std::filesystem::path AppPaths::ytDlpExePath() const {
    return ytDlpDir() / L"yt-dlp.exe";
}

std::filesystem::path AppPaths::ytDlpVersionPath() const {
    return ytDlpDir() / L"version.txt";
}

std::filesystem::path AppPaths::localFfmpegBinDir() const {
    return toolsDir() / L"ffmpeg" / L"bin";
}

std::filesystem::path AppPaths::localFfmpegExePath() const {
    return localFfmpegBinDir() / L"ffmpeg.exe";
}

std::filesystem::path AppPaths::localFfprobeExePath() const {
    return localFfmpegBinDir() / L"ffprobe.exe";
}

std::filesystem::path AppPaths::localFfplayExePath() const {
    return localFfmpegBinDir() / L"ffplay.exe";
}

std::filesystem::path AppPaths::localWhisperDir() const {
    return toolsDir() / L"whisper";
}

std::filesystem::path AppPaths::localWhisperExePath() const {
    return localWhisperDir() / L"whisper-cli.exe";
}

std::filesystem::path AppPaths::localWhisperCpuDir() const {
    return localWhisperDir() / L"cpu";
}

std::filesystem::path AppPaths::localWhisperCudaDir() const {
    return localWhisperDir() / L"cuda";
}

std::filesystem::path AppPaths::localWhisperCpuExePath() const {
    return localWhisperCpuDir() / L"whisper-cli.exe";
}

std::filesystem::path AppPaths::localWhisperCudaExePath() const {
    return localWhisperCudaDir() / L"whisper-cli.exe";
}

std::filesystem::path AppPaths::localWhisperModelsDir() const {
    return localWhisperDir() / L"models";
}

std::filesystem::path AppPaths::localWhisperModelPath() const {
    return localWhisperModelsDir() / L"ggml-large-v3-turbo.bin";
}

std::filesystem::path AppPaths::transcriptionTempDir() const {
    return stuffDir() / L"transcription_tmp";
}

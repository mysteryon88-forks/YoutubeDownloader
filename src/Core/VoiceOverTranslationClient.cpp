#include "VoiceOverTranslationClient.h"

#include "ProcessRunner.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace {

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec);
}

std::wstring LowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring SafeLanguageForFile(std::wstring language) {
    std::wstring out;
    out.reserve(language.size());
    for (wchar_t ch : language) {
        if ((ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            ch == L'-' ||
            ch == L'_') {
            out.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }
    return out.empty() ? L"ru" : out;
}

std::wstring NormalizedMode(const std::wstring& mode) {
    return mode == L"mixed" ? L"mixed" : L"separate";
}

std::wstring VoiceOverSuffix(const std::wstring& language, const std::wstring& mode) {
    const std::wstring lang = SafeLanguageForFile(language);
    return NormalizedMode(mode) == L"mixed"
        ? L".vot-mixed." + lang
        : L".vot." + lang;
}

std::wstring OutputVideoExtensionFor(const std::filesystem::path& mediaPath) {
    return LowerCopy(mediaPath.extension().wstring()) == L".mp4" ? L".mp4" : L".mkv";
}

std::filesystem::path CmdExePath() {
    wchar_t* comspec = nullptr;
    size_t comspecLength = 0;
    if (_wdupenv_s(&comspec, &comspecLength, L"COMSPEC") == 0 && comspec && comspecLength > 0) {
        std::filesystem::path result = comspec;
        free(comspec);
        return result;
    }
    free(comspec);

    std::filesystem::path systemCmd = L"C:\\Windows\\System32\\cmd.exe";
    std::error_code ec;
    if (std::filesystem::is_regular_file(systemCmd, ec)) {
        return systemCmd;
    }
    return L"cmd.exe";
}

std::filesystem::path PathWithSuffix(
    const std::filesystem::path& parent,
    const std::wstring& stem,
    const std::wstring& suffix,
    const std::wstring& extension
) {
    return parent / (stem + suffix + extension);
}

std::wstring LanguageMetadataCode(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForFile(language);
    if (lang == L"ru" || lang == L"rus") {
        return L"rus";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"eng";
    }
    if (lang == L"kk" || lang == L"kaz") {
        return L"kaz";
    }
    return lang;
}

std::wstring LanguageTitle(const std::wstring& language) {
    const std::wstring lang = SafeLanguageForFile(language);
    if (lang == L"ru" || lang == L"rus") {
        return L"Russian";
    }
    if (lang == L"en" || lang == L"eng") {
        return L"English";
    }
    if (lang == L"kk" || lang == L"kaz") {
        return L"Kazakh";
    }
    return lang;
}

std::wstring VolumeValue(int originalVolumePercent) {
    const double value = static_cast<double>(std::clamp(originalVolumePercent, 0, 100)) / 100.0;
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::wstring text = stream.str();
    while (text.size() > 1 && text.back() == L'0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == L'.') {
        text.pop_back();
    }
    return text.empty() ? L"0" : text;
}

std::wstring TrimWhitespace(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin])) {
        ++begin;
    }

    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring LastNonEmptyLine(const std::wstring& text) {
    size_t end = text.size();
    while (end > 0) {
        const size_t newline = text.rfind(L'\n', end - 1);
        const size_t begin = newline == std::wstring::npos ? 0 : newline + 1;
        const std::wstring line = TrimWhitespace(text.substr(begin, end - begin));
        if (!line.empty()) {
            return line;
        }
        if (newline == std::wstring::npos) {
            break;
        }
        end = newline;
    }
    return {};
}

std::wstring ProcessErrorSummary(const ProcessRunResult& result, const std::wstring& fallback) {
    std::wstring summary = LastNonEmptyLine(result.stderrText);
    if (!summary.empty()) {
        return summary;
    }
    summary = LastNonEmptyLine(result.stdoutText);
    if (!summary.empty()) {
        return summary;
    }
    return fallback;
}

VoiceOverTranslationResult Failed(std::wstring errorText) {
    VoiceOverTranslationResult result;
    result.errorText = std::move(errorText);
    return result;
}

VoiceOverTranslationResult Canceled(std::vector<std::filesystem::path> cleanupPaths) {
    std::error_code ec;
    for (const std::filesystem::path& path : cleanupPaths) {
        std::filesystem::remove(path, ec);
        ec.clear();
    }

    VoiceOverTranslationResult result;
    result.canceled = true;
    result.errorText = L"Отменено";
    return result;
}

bool MoveFileReplacing(const std::filesystem::path& source, const std::filesystem::path& destination) {
    if (!IsRegularFile(source)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    std::filesystem::remove(destination, ec);
    ec.clear();
    std::filesystem::rename(source, destination, ec);
    if (!ec && IsRegularFile(destination)) {
        return true;
    }

    ec.clear();
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec || !IsRegularFile(destination)) {
        return false;
    }
    ec.clear();
    std::filesystem::remove(source, ec);
    return true;
}

void EmitVoiceOverProgress(
    const VoiceOverTranslationCallbacks& callbacks,
    double percent,
    const std::wstring& status
) {
    if (callbacks.onProgress) {
        callbacks.onProgress(std::clamp(percent, 0.0, 100.0), status);
    }
    if (callbacks.onStatus) {
        callbacks.onStatus(status);
    }
}

} // namespace

VoiceOverTranslationPaths BuildVoiceOverPaths(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& tempDirectory,
    const std::wstring& language,
    const std::wstring& mode
) {
    const std::wstring stem = mediaPath.stem().wstring();
    const std::wstring audioSuffix = VoiceOverSuffix(language, L"separate");
    const std::wstring videoSuffix = VoiceOverSuffix(language, mode);

    VoiceOverTranslationPaths paths;
    paths.tempAudioPath = PathWithSuffix(tempDirectory, stem, audioSuffix, L".mp3");
    paths.finalAudioPath = PathWithSuffix(mediaPath.parent_path(), stem, audioSuffix, L".mp3");
    paths.finalVideoPath = PathWithSuffix(mediaPath.parent_path(), stem, videoSuffix, OutputVideoExtensionFor(mediaPath));
    return paths;
}

std::vector<std::wstring> BuildVotCliArguments(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
) {
    const std::wstring language = SafeLanguageForFile(request.language);
    return {
        L"--output",
        outputAudioPath.parent_path().wstring(),
        L"--output-file",
        outputAudioPath.filename().wstring(),
        L"--reslang=" + language,
        request.youtubeUrl
    };
}

VoiceOverProcessInvocation BuildVotCliInvocation(
    const VoiceOverTranslationRequest& request,
    const std::filesystem::path& outputAudioPath
) {
    VoiceOverProcessInvocation invocation;
    const std::wstring extension = LowerCopy(request.votCliPath.extension().wstring());
    if (extension == L".cmd" || extension == L".bat") {
        invocation.executable = CmdExePath();
        invocation.arguments = {
            L"/d",
            L"/c",
            L"call",
            request.votCliPath.wstring()
        };
        std::vector<std::wstring> votArgs = BuildVotCliArguments(request, outputAudioPath);
        invocation.arguments.insert(invocation.arguments.end(), votArgs.begin(), votArgs.end());
        return invocation;
    }

    invocation.executable = request.votCliPath;
    invocation.arguments = BuildVotCliArguments(request, outputAudioPath);
    return invocation;
}

std::vector<std::wstring> BuildVoiceOverMuxArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    const std::wstring& language
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-i",
        translationAudioPath.wstring(),
        L"-map",
        L"0:v",
        L"-map",
        L"0:a?",
        L"-map",
        L"1:a",
        L"-c:v",
        L"copy",
        L"-c:a",
        L"aac",
        L"-metadata:s:a:1",
        L"language=" + LanguageMetadataCode(language),
        L"-metadata:s:a:1",
        L"title=VOT " + LanguageTitle(language),
        outputVideoPath.wstring()
    };
}

std::vector<std::wstring> BuildVoiceOverMixArguments(
    const std::filesystem::path& mediaPath,
    const std::filesystem::path& translationAudioPath,
    const std::filesystem::path& outputVideoPath,
    int originalVolumePercent
) {
    return {
        L"-y",
        L"-i",
        mediaPath.wstring(),
        L"-i",
        translationAudioPath.wstring(),
        L"-filter_complex",
        L"[0:a]volume=" + VolumeValue(originalVolumePercent) + L"[orig];[orig][1:a]amix=inputs=2:duration=first[a]",
        L"-map",
        L"0:v",
        L"-map",
        L"[a]",
        L"-c:v",
        L"copy",
        L"-c:a",
        L"aac",
        outputVideoPath.wstring()
    };
}

VoiceOverTranslationResult VoiceOverTranslationClient::Translate(
    const VoiceOverTranslationRequest& request,
    const VoiceOverTranslationCallbacks& callbacks,
    HANDLE cancelEvent
) {
    if (!IsRegularFile(request.mediaPath)) {
        return Failed(L"Файл для перевода не найден");
    }
    if (!IsRegularFile(request.ffmpegExePath)) {
        return Failed(L"FFmpeg не найден");
    }
    if (!IsRegularFile(request.votCliPath)) {
        return Failed(L"vot-cli не найден");
    }
    if (request.youtubeUrl.empty()) {
        return Failed(L"Ссылка на видео недоступна");
    }

    std::error_code ec;
    std::filesystem::create_directories(request.tempDirectory, ec);
    if (ec) {
        return Failed(L"Не удалось создать папку для временной озвучки");
    }

    const VoiceOverTranslationPaths paths = BuildVoiceOverPaths(
        request.mediaPath,
        request.tempDirectory,
        request.language,
        request.mode
    );
    std::filesystem::remove(paths.tempAudioPath, ec);
    ec.clear();

    EmitVoiceOverProgress(callbacks, 5.0, L"Получение перевода");

    ProcessRunOptions vot;
    const VoiceOverProcessInvocation votInvocation = BuildVotCliInvocation(request, paths.tempAudioPath);
    vot.executable = votInvocation.executable;
    vot.arguments = votInvocation.arguments;
    vot.timeoutMs = INFINITE;
    vot.cancelEvent = cancelEvent;
    const auto handleVotLine = [&callbacks](const std::wstring&) {
        EmitVoiceOverProgress(callbacks, 20.0, L"Получение перевода");
    };
    vot.onStdoutLine = handleVotLine;
    vot.onStderrLine = handleVotLine;

    const ProcessRunResult translated = ProcessRunner::Run(vot);
    if (translated.canceled) {
        return Canceled({paths.tempAudioPath});
    }
    if (translated.exitCode != 0) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(L"vot-cli завершился с ошибкой: " + ProcessErrorSummary(translated, L"неизвестная ошибка"));
    }
    if (!MoveFileReplacing(paths.tempAudioPath, paths.finalAudioPath)) {
        std::filesystem::remove(paths.tempAudioPath, ec);
        return Failed(L"vot-cli не создал mp3 с переводом");
    }

    EmitVoiceOverProgress(callbacks, 70.0, L"Сборка видео");

    std::filesystem::remove(paths.finalVideoPath, ec);
    ec.clear();

    ProcessRunOptions ffmpeg;
    ffmpeg.executable = request.ffmpegExePath;
    ffmpeg.arguments = NormalizedMode(request.mode) == L"mixed"
        ? BuildVoiceOverMixArguments(request.mediaPath, paths.finalAudioPath, paths.finalVideoPath, request.originalVolumePercent)
        : BuildVoiceOverMuxArguments(request.mediaPath, paths.finalAudioPath, paths.finalVideoPath, request.language);
    ffmpeg.timeoutMs = INFINITE;
    ffmpeg.cancelEvent = cancelEvent;

    const ProcessRunResult muxed = ProcessRunner::Run(ffmpeg);
    if (muxed.canceled) {
        return Canceled({paths.finalVideoPath});
    }
    if (muxed.exitCode != 0) {
        std::filesystem::remove(paths.finalVideoPath, ec);
        return Failed(L"FFmpeg не собрал видео с переводом: " + ProcessErrorSummary(muxed, L"неизвестная ошибка"));
    }
    if (!IsRegularFile(paths.finalVideoPath)) {
        return Failed(L"FFmpeg не создал видео с переводом");
    }

    EmitVoiceOverProgress(callbacks, 99.0, L"Сохранение перевода");

    VoiceOverTranslationResult result;
    result.success = true;
    result.audioPath = paths.finalAudioPath;
    result.videoPath = paths.finalVideoPath;
    return result;
}

#include "Logger.h"

#include "BackendText.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace {

std::wstring TimestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
    gmtime_s(&utc, &time);

    std::wostringstream out;
    out << std::put_time(&utc, L"%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

Logger::Logger(std::filesystem::path logPath)
    : m_logPath(std::move(logPath)) {
    std::error_code ec;
    std::filesystem::create_directories(m_logPath.parent_path(), ec);
    std::ofstream out(m_logPath, std::ios::binary | std::ios::trunc);
}

Logger::Logger(const AppPaths& paths)
    : Logger(paths.logPath()) {
}

void Logger::Info(const std::wstring& message) {
    Append(L"INFO", message);
}

void Logger::Error(const std::wstring& message) {
    Append(L"ERROR", message);
}

void Logger::Append(const std::wstring& level, const std::wstring& message) {
    std::lock_guard lock(m_mutex);

    std::error_code ec;
    std::filesystem::create_directories(m_logPath.parent_path(), ec);

    std::ofstream out(m_logPath, std::ios::binary | std::ios::app);
    if (!out) {
        return;
    }

    out << WideToUtf8(L"[" + TimestampUtc() + L"] [" + level + L"] " + message + L"\n");
}

std::wstring Logger::ReadAll() const {
    std::lock_guard lock(m_mutex);

    std::ifstream in(m_logPath, std::ios::binary);
    if (!in) {
        return {};
    }

    std::string text{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    };
    if (text.empty()) {
        return {};
    }
    return Utf8ToWide(text);
}


#include "BackendText.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

const wchar_t* RussianPlural(
    std::uint64_t value,
    const wchar_t* singular,
    const wchar_t* few,
    const wchar_t* many
) {
    const std::uint64_t lastTwo = value % 100;
    if (lastTwo >= 11 && lastTwo <= 14) {
        return many;
    }

    switch (value % 10) {
    case 1:
        return singular;
    case 2:
    case 3:
    case 4:
        return few;
    default:
        return many;
    }
}

void AddElapsedPart(
    std::vector<std::wstring>& parts,
    std::uint64_t value,
    const wchar_t* singular,
    const wchar_t* few,
    const wchar_t* many
) {
    if (value == 0) {
        return;
    }
    parts.push_back(std::to_wstring(value) + L" " + RussianPlural(value, singular, few, many));
}

} // namespace

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (size <= 0) {
        throw std::runtime_error("failed to convert wide string to UTF-8");
    }

    std::string out(static_cast<size_t>(size), '\0');
    const int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        out.data(),
        size,
        nullptr,
        nullptr
    );
    if (written != size) {
        throw std::runtime_error("failed to write UTF-8 string");
    }
    return out;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (size <= 0) {
        throw std::runtime_error("failed to convert UTF-8 string to wide");
    }

    std::wstring out(static_cast<size_t>(size), L'\0');
    const int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.c_str(),
        static_cast<int>(value.size()),
        out.data(),
        size
    );
    if (written != size) {
        throw std::runtime_error("failed to write wide string");
    }
    return out;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.wstring());
}

std::filesystem::path PathFromUtf8(const std::string& value) {
    return std::filesystem::path(Utf8ToWide(value));
}

std::wstring FormatBytes(std::uint64_t bytes) {
    if (bytes == 0) {
        return {};
    }

    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    double value = static_cast<double>(bytes);
    const wchar_t* unit = L"B";
    if (value >= kGiB) {
        value /= kGiB;
        unit = L"GB";
    } else if (value >= kMiB) {
        value /= kMiB;
        unit = L"MB";
    } else if (value >= kKiB) {
        value /= kKiB;
        unit = L"KB";
    }

    std::wostringstream out;
    out.setf(std::ios::fixed);
    out.precision((value >= 10.0 || bytes < 1024) ? 0 : 1);
    out << value << L" " << unit;
    return out.str();
}

std::wstring FormatProgressBytes(std::uint64_t downloaded, std::uint64_t total) {
    if (total > 0) {
        const std::wstring downloadedText = downloaded > 0 ? FormatBytes(downloaded) : L"0 B";
        return downloadedText + L" / " + FormatBytes(total);
    }
    return FormatBytes(downloaded);
}

std::wstring FormatDuration(std::uint64_t seconds) {
    if (seconds == 0) {
        return {};
    }

    constexpr std::uint64_t kMinute = 60;
    constexpr std::uint64_t kHour = 60 * kMinute;
    constexpr std::uint64_t kDay = 24 * kHour;

    const std::uint64_t days = seconds / kDay;
    seconds %= kDay;
    const std::uint64_t hours = seconds / kHour;
    seconds %= kHour;
    const std::uint64_t minutes = seconds / kMinute;
    seconds %= kMinute;

    std::vector<std::wstring> parts;
    if (days > 0) {
        parts.push_back(std::to_wstring(days) + L" д");
        if (hours > 0) {
            parts.push_back(std::to_wstring(hours) + L" ч");
        } else if (minutes > 0) {
            parts.push_back(std::to_wstring(minutes) + L" мин");
        }
    } else if (hours > 0) {
        parts.push_back(std::to_wstring(hours) + L" ч");
        if (minutes > 0) {
            parts.push_back(std::to_wstring(minutes) + L" мин");
        }
    } else if (minutes > 0) {
        parts.push_back(std::to_wstring(minutes) + L" мин");
        if (seconds > 0) {
            parts.push_back(std::to_wstring(seconds) + L" с");
        }
    } else {
        parts.push_back(std::to_wstring(seconds) + L" с");
    }

    std::wstring result;
    for (const std::wstring& part : parts) {
        if (!result.empty()) {
            result += L" ";
        }
        result += part;
    }
    return result;
}

std::wstring FormatElapsedDuration(std::uint64_t seconds) {
    constexpr std::uint64_t kMinute = 60;
    constexpr std::uint64_t kHour = 60 * kMinute;
    constexpr std::uint64_t kDay = 24 * kHour;

    const std::uint64_t days = seconds / kDay;
    seconds %= kDay;
    const std::uint64_t hours = seconds / kHour;
    seconds %= kHour;
    const std::uint64_t minutes = seconds / kMinute;
    seconds %= kMinute;

    std::vector<std::wstring> parts;
    AddElapsedPart(parts, days, L"день", L"дня", L"дней");
    AddElapsedPart(parts, hours, L"час", L"часа", L"часов");
    AddElapsedPart(parts, minutes, L"минута", L"минуты", L"минут");
    AddElapsedPart(parts, seconds, L"секунда", L"секунды", L"секунд");
    if (parts.empty()) {
        return L"0 секунд";
    }

    std::wstring result;
    for (const std::wstring& part : parts) {
        if (!result.empty()) {
            result += L" ";
        }
        result += part;
    }
    return result;
}

int CalculateProgressPercent(std::uint64_t downloaded, std::uint64_t total) {
    if (total == 0) {
        return 0;
    }
    const double percent = (static_cast<double>(downloaded) * 100.0) / static_cast<double>(total);
    return std::clamp(static_cast<int>(percent), 0, 100);
}

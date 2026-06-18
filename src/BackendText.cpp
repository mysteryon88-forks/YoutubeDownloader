#include "BackendText.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <stdexcept>

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


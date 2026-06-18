#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string>

using HttpProgressCallback = std::function<void(std::uint64_t downloaded, std::uint64_t total)>;

class WinHttpClient {
public:
    static std::string GetString(const std::wstring& url, HANDLE cancelEvent = nullptr);
    static void DownloadFile(
        const std::wstring& url,
        const std::filesystem::path& target,
        const HttpProgressCallback& onProgress = {},
        HANDLE cancelEvent = nullptr
    );
};


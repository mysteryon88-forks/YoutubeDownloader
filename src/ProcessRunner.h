#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct ProcessRunOptions {
    std::filesystem::path executable;
    std::vector<std::wstring> arguments;
    std::filesystem::path workingDirectory;
    DWORD timeoutMs = INFINITE;
    HANDLE cancelEvent = nullptr;
    std::function<void(const std::wstring&)> onStdoutLine;
    std::function<void(const std::wstring&)> onStderrLine;
};

struct ProcessRunResult {
    int exitCode = -1;
    bool timedOut = false;
    bool canceled = false;
    std::wstring stdoutText;
    std::wstring stderrText;
};

class ProcessRunner {
public:
    static ProcessRunResult Run(const ProcessRunOptions& options);
};


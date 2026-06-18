#include "ProcessRunner.h"

#include "BackendText.h"

#include <array>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : m_handle(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    HANDLE get() const { return m_handle; }
    HANDLE release() {
        HANDLE handle = m_handle;
        m_handle = nullptr;
        return handle;
    }
    void reset(HANDLE handle = nullptr) {
        if (m_handle && m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
        }
        m_handle = handle;
    }

private:
    HANDLE m_handle = nullptr;
};

std::wstring QuoteArgument(const std::wstring& arg) {
    if (arg.empty()) {
        return L"\"\"";
    }

    const bool needsQuotes = arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) {
        return arg;
    }

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(ch);
            backslashes = 0;
            continue;
        }
        out.append(backslashes, L'\\');
        backslashes = 0;
        out.push_back(ch);
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring BuildCommandLine(const ProcessRunOptions& options) {
    std::wstring command = QuoteArgument(options.executable.wstring());
    for (const std::wstring& arg : options.arguments) {
        command.push_back(L' ');
        command += QuoteArgument(arg);
    }
    return command;
}

void EmitLines(const std::wstring& text, size_t& offset, const std::function<void(const std::wstring&)>& callback) {
    if (!callback) {
        return;
    }
    size_t start = offset;
    while (true) {
        const size_t newline = text.find(L'\n', start);
        if (newline == std::wstring::npos) {
            break;
        }
        std::wstring line = text.substr(offset, newline - offset);
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        callback(line);
        start = newline + 1;
        offset = start;
    }
}

std::thread StartReader(HANDLE readHandle, std::wstring& target, const std::function<void(const std::wstring&)>& callback) {
    return std::thread([readHandle, &target, callback]() {
        std::string bytes;
        std::array<char, 4096> buffer = {};
        DWORD read = 0;
        while (ReadFile(readHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
            bytes.append(buffer.data(), buffer.data() + read);
            try {
                target = Utf8ToWide(bytes);
            } catch (...) {
                target.assign(bytes.begin(), bytes.end());
            }
            size_t offset = 0;
            EmitLines(target, offset, callback);
        }
        try {
            target = Utf8ToWide(bytes);
        } catch (...) {
            target.assign(bytes.begin(), bytes.end());
        }
        size_t offset = 0;
        EmitLines(target + L"\n", offset, callback);
    });
}

bool CreatePipePair(UniqueHandle& readPipe, UniqueHandle& writePipe) {
    SECURITY_ATTRIBUTES attributes = {};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if (!CreatePipe(&readHandle, &writeHandle, &attributes, 0)) {
        return false;
    }
    readPipe.reset(readHandle);
    writePipe.reset(writeHandle);
    SetHandleInformation(readPipe.get(), HANDLE_FLAG_INHERIT, 0);
    return true;
}

} // namespace

ProcessRunResult ProcessRunner::Run(const ProcessRunOptions& options) {
    if (options.executable.empty()) {
        throw std::runtime_error("process executable is empty");
    }

    UniqueHandle stdoutRead;
    UniqueHandle stdoutWrite;
    UniqueHandle stderrRead;
    UniqueHandle stderrWrite;
    if (!CreatePipePair(stdoutRead, stdoutWrite) || !CreatePipePair(stderrRead, stderrWrite)) {
        throw std::runtime_error("failed to create process pipes");
    }

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdoutWrite.get();
    startup.hStdError = stderrWrite.get();

    PROCESS_INFORMATION process = {};
    std::wstring commandLine = BuildCommandLine(options);
    std::wstring workingDirectory = options.workingDirectory.empty() ? L"" : options.workingDirectory.wstring();

    const BOOL created = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startup,
        &process
    );

    stdoutWrite.reset();
    stderrWrite.reset();

    if (!created) {
        throw std::runtime_error("failed to create process");
    }

    UniqueHandle processHandle(process.hProcess);
    UniqueHandle threadHandle(process.hThread);

    ProcessRunResult result;
    std::thread stdoutThread = StartReader(stdoutRead.get(), result.stdoutText, options.onStdoutLine);
    std::thread stderrThread = StartReader(stderrRead.get(), result.stderrText, options.onStderrLine);

    const auto start = std::chrono::steady_clock::now();
    while (true) {
        const DWORD wait = WaitForSingleObject(processHandle.get(), 50);
        if (wait == WAIT_OBJECT_0) {
            break;
        }

        if (options.cancelEvent && WaitForSingleObject(options.cancelEvent, 0) == WAIT_OBJECT_0) {
            result.canceled = true;
            TerminateProcess(processHandle.get(), 1);
            break;
        }

        if (options.timeoutMs != INFINITE) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (elapsed >= options.timeoutMs) {
                result.timedOut = true;
                TerminateProcess(processHandle.get(), 1);
                break;
            }
        }
    }

    WaitForSingleObject(processHandle.get(), INFINITE);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(processHandle.get(), &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }

    stdoutRead.reset();
    stderrRead.reset();
    if (stdoutThread.joinable()) {
        stdoutThread.join();
    }
    if (stderrThread.joinable()) {
        stderrThread.join();
    }

    return result;
}


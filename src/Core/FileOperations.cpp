#include "FileOperations.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <sstream>
#include <stdexcept>
#include <system_error>

namespace {

[[noreturn]] void RejectStagedFile(const std::filesystem::path& staged, const char* message) {
    std::error_code ec;
    std::filesystem::remove(staged, ec);
    throw std::runtime_error(message);
}

} // namespace

void CommitDownloadedFile(
    const std::filesystem::path& staged,
    const std::filesystem::path& target,
    std::uint64_t downloaded,
    std::uint64_t expectedSize
) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(staged, ec)) {
        RejectStagedFile(staged, "staged download is missing");
    }

    const std::uint64_t actualSize = static_cast<std::uint64_t>(std::filesystem::file_size(staged, ec));
    if (ec || actualSize != downloaded || (expectedSize > 0 && downloaded != expectedSize)) {
        RejectStagedFile(staged, "download size validation failed");
    }

    const bool targetExists = std::filesystem::is_regular_file(target, ec);
    ec.clear();
    BOOL replaced = FALSE;
    if (targetExists) {
        replaced = ReplaceFileW(
            target.c_str(),
            staged.c_str(),
            nullptr,
            0,
            nullptr,
            nullptr
        );
    } else {
        replaced = MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    }

    if (!replaced) {
        const DWORD error = GetLastError();
        std::filesystem::remove(staged, ec);
        std::ostringstream message;
        message << "failed to commit downloaded file (Win32 error " << error << ")";
        throw std::runtime_error(message.str());
    }
}

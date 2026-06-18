#pragma once

#include <filesystem>
#include <string>

std::string WideToUtf8(const std::wstring& value);
std::wstring Utf8ToWide(const std::string& value);
std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& value);


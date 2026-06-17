#pragma once

#include <string>

struct ReleaseAssetInfo {
    bool found = false;
    std::wstring version;
    std::wstring pageUrl;
    std::wstring downloadUrl;
};

ReleaseAssetInfo ParseGitHubReleaseAsset(const std::string& releaseJson, const std::string& assetName);

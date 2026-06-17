#include "ToolManagers.h"

#include <nlohmann/json.hpp>

namespace {

std::wstring AsciiToWide(const std::string& value) {
    std::wstring out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        out.push_back(static_cast<wchar_t>(ch));
    }
    return out;
}

std::wstring NormalizeVersion(std::wstring version) {
    if (!version.empty() && (version.front() == L'v' || version.front() == L'V')) {
        version.erase(version.begin());
    }
    return version;
}

} // namespace

ReleaseAssetInfo ParseGitHubReleaseAsset(const std::string& releaseJson, const std::string& assetName) {
    ReleaseAssetInfo info;

    try {
        const nlohmann::json json = nlohmann::json::parse(releaseJson);
        if (!json.is_object()) {
            return info;
        }

        info.version = NormalizeVersion(AsciiToWide(json.value("tag_name", json.value("name", ""))));
        info.pageUrl = AsciiToWide(json.value("html_url", ""));

        const auto assets = json.find("assets");
        if (assets == json.end() || !assets->is_array()) {
            return info;
        }

        for (const nlohmann::json& asset : *assets) {
            if (!asset.is_object()) {
                continue;
            }
            if (asset.value("name", "") != assetName) {
                continue;
            }
            info.downloadUrl = AsciiToWide(asset.value("browser_download_url", ""));
            info.found = !info.downloadUrl.empty();
            return info;
        }
    } catch (...) {
        return ReleaseAssetInfo{};
    }

    return info;
}

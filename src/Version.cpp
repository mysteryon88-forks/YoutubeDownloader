#include "Version.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace {

std::vector<int> ParseVersionParts(const std::wstring& version) {
    std::vector<int> parts;
    int current = -1;

    for (wchar_t ch : version) {
        if (std::iswdigit(ch)) {
            if (current < 0) {
                current = 0;
            }
            current = (current * 10) + static_cast<int>(ch - L'0');
            continue;
        }

        if (current >= 0) {
            parts.push_back(current);
            current = -1;
        }
    }

    if (current >= 0) {
        parts.push_back(current);
    }

    return parts;
}

} // namespace

int CompareVersions(const std::wstring& left, const std::wstring& right) {
    const std::vector<int> leftParts = ParseVersionParts(left);
    const std::vector<int> rightParts = ParseVersionParts(right);
    const size_t partCount = std::max(leftParts.size(), rightParts.size());

    for (size_t index = 0; index < partCount; ++index) {
        const int leftPart = index < leftParts.size() ? leftParts[index] : 0;
        const int rightPart = index < rightParts.size() ? rightParts[index] : 0;
        if (leftPart < rightPart) {
            return -1;
        }
        if (leftPart > rightPart) {
            return 1;
        }
    }

    return 0;
}

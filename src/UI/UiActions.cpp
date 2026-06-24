#include "UiActions.h"

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <system_error>

namespace {

constexpr int kEditMenuOuterPadding = 2;
constexpr int kEditMenuItemHeight = 34;
constexpr int kEditMenuSeparatorHeight = 10;

int EditMenuItemHeight(const EditContextMenuItem& item) {
    return item.separator ? kEditMenuSeparatorHeight : kEditMenuItemHeight;
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool IsTranscriptTextPath(const std::filesystem::path& path) {
    return Lowercase(path.extension().wstring()) == L".txt";
}

bool IsVoiceOverVideoPathForLanguage(const std::filesystem::path& path, const std::wstring& language) {
    const std::wstring extension = Lowercase(path.extension().wstring());
    if (extension != L".mp4" && extension != L".mkv") {
        return false;
    }

    std::wstring safeLanguage;
    safeLanguage.reserve(language.size());
    for (wchar_t ch : language) {
        if ((ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') ||
            ch == L'-' ||
            ch == L'_') {
            safeLanguage.push_back(static_cast<wchar_t>(std::towlower(ch)));
        }
    }
    if (safeLanguage.empty()) {
        safeLanguage = L"ru";
    }

    const std::wstring filename = Lowercase(path.filename().wstring());
    return filename.find(L".vot." + safeLanguage + extension) != std::wstring::npos ||
           filename.find(L".vot-mixed." + safeLanguage + extension) != std::wstring::npos;
}

} // namespace

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady, bool previewLoading) {
    if (!ytDlpReady) {
        return DownloadAttemptAction::ShowYtDlpNotReady;
    }
    if (previewLoading) {
        return DownloadAttemptAction::ShowPreviewLoading;
    }
    return DownloadAttemptAction::Enqueue;
}

std::vector<EditContextMenuItem> BuildEditContextMenuItems(
    bool canUndo,
    bool hasSelection,
    bool canPaste,
    bool hasText
) {
    return {
        {IdEditMenuUndo, L"Отменить", false, canUndo},
        {0, L"", true, false},
        {IdEditMenuCut, L"Вырезать", false, hasSelection},
        {IdEditMenuCopy, L"Копировать", false, hasSelection},
        {IdEditMenuPaste, L"Вставить", false, canPaste},
        {IdEditMenuDelete, L"Удалить", false, hasSelection},
        {0, L"", true, false},
        {IdEditMenuSelectAll, L"Выделить всё", false, hasText}
    };
}

int EditContextMenuHeight(const std::vector<EditContextMenuItem>& items) {
    int height = kEditMenuOuterPadding * 2;
    for (const EditContextMenuItem& item : items) {
        height += EditMenuItemHeight(item);
    }
    return height;
}

UINT HitTestEditContextMenuItem(const std::vector<EditContextMenuItem>& items, int y) {
    int top = kEditMenuOuterPadding;
    for (const EditContextMenuItem& item : items) {
        const int bottom = top + EditMenuItemHeight(item);
        if (y >= top && y < bottom) {
            return (!item.separator && item.enabled) ? item.id : 0;
        }
        top = bottom;
    }
    return 0;
}

WhisperUtilityStatusText BuildWhisperUtilityStatusText(
    bool executableAvailable,
    const std::filesystem::path& executablePath,
    bool modelAvailable,
    const std::filesystem::path& modelPath
) {
    WhisperUtilityStatusText text;
    text.executableText = executableAvailable && !executablePath.empty()
        ? L"Найден: " + executablePath.wstring()
        : L"Не найден";
    text.modelText = modelAvailable && !modelPath.empty()
        ? L"Модель: " + modelPath.wstring()
        : L"Модель не скачана";
    return text;
}

std::filesystem::path FindTranscriptTextPath(const std::vector<std::filesystem::path>& outputFiles) {
    std::error_code ec;
    for (auto it = outputFiles.rbegin(); it != outputFiles.rend(); ++it) {
        if (!IsTranscriptTextPath(*it)) {
            continue;
        }
        if (std::filesystem::is_regular_file(*it, ec)) {
            return *it;
        }
        ec.clear();
    }
    return {};
}

std::filesystem::path FindVoiceOverVideoPath(
    const std::vector<std::filesystem::path>& outputFiles,
    const std::wstring& language
) {
    std::error_code ec;
    for (auto it = outputFiles.rbegin(); it != outputFiles.rend(); ++it) {
        if (!IsVoiceOverVideoPathForLanguage(*it, language)) {
            continue;
        }
        if (std::filesystem::is_regular_file(*it, ec)) {
            return *it;
        }
        ec.clear();
    }
    return {};
}

void PasteReplacingEditText(HWND editControl) {
    if (!IsWindow(editControl)) {
        return;
    }

    SetFocus(editControl);
    SendMessageW(editControl, EM_SETSEL, 0, -1);
    SendMessageW(editControl, WM_PASTE, 0, 0);
}

void CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    const SIZE_T byteCount = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, byteCount);
    if (memory) {
        void* target = GlobalLock(memory);
        if (target) {
            std::memcpy(target, text.c_str(), byteCount);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

void RestoreModalOwner(HWND owner, bool ownerWasEnabled) {
    if (!ownerWasEnabled || !IsWindow(owner)) {
        return;
    }

    EnableWindow(owner, TRUE);
    if (IsIconic(owner)) {
        ShowWindow(owner, SW_RESTORE);
    }
    SetActiveWindow(owner);
}

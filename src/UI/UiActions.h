#pragma once

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

enum class DownloadAttemptAction { Enqueue, ShowYtDlpNotReady, ShowPreviewLoading };

enum EditContextMenuCommand : UINT {
    IdEditMenuUndo = 1,
    IdEditMenuCut = 2,
    IdEditMenuCopy = 3,
    IdEditMenuPaste = 4,
    IdEditMenuDelete = 5,
    IdEditMenuSelectAll = 6
};

struct EditContextMenuItem {
    UINT id = 0;
    std::wstring text;
    bool separator = false;
    bool enabled = true;
};

struct WhisperUtilityStatusText {
    std::wstring executableText;
    std::wstring modelText;
};

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady, bool previewLoading);
std::vector<EditContextMenuItem> BuildEditContextMenuItems(
    bool canUndo,
    bool hasSelection,
    bool canPaste,
    bool hasText
);
int EditContextMenuHeight(const std::vector<EditContextMenuItem>& items);
UINT HitTestEditContextMenuItem(const std::vector<EditContextMenuItem>& items, int y);
WhisperUtilityStatusText BuildWhisperUtilityStatusText(
    bool executableAvailable,
    const std::filesystem::path& executablePath,
    bool modelAvailable,
    const std::filesystem::path& modelPath
);
std::filesystem::path FindTranscriptTextPath(const std::vector<std::filesystem::path>& outputFiles);
void PasteReplacingEditText(HWND editControl);
void CopyTextToClipboard(HWND owner, const std::wstring& text);
void RestoreModalOwner(HWND owner, bool ownerWasEnabled);

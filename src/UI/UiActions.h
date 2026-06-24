#pragma once

#include <windows.h>

#include <string>

enum class DownloadAttemptAction { Enqueue, ShowYtDlpNotReady };

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady);
void PasteReplacingEditText(HWND editControl);
void CopyTextToClipboard(HWND owner, const std::wstring& text);
void RestoreModalOwner(HWND owner, bool ownerWasEnabled);

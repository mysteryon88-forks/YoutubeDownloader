#pragma once

#include <windows.h>

enum class DownloadAttemptAction { Enqueue, ShowYtDlpNotReady };

DownloadAttemptAction ResolveDownloadAttempt(bool ytDlpReady);
void PasteReplacingEditText(HWND editControl);
void RestoreModalOwner(HWND owner, bool ownerWasEnabled);

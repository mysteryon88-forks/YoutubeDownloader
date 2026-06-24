#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"

#include <string>

struct ReleaseAssetInfo;

void ShowInfoDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message);
void ShowErrorDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message);
void ShowLogsDialog(HWND owner, HINSTANCE instance, const std::wstring& logText);
bool ShowSettingsDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
bool ShowFfmpegDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
void ShowAboutDialog(HWND owner, HINSTANCE instance, const AppPaths& paths);
bool OfferAppUpdate(HWND owner, HINSTANCE instance, const AppPaths& paths, const ReleaseAssetInfo& release, bool notifyWhenCurrent);
bool CheckAndOfferAppUpdate(HWND owner, HINSTANCE instance, const AppPaths& paths, bool notifyWhenCurrent);

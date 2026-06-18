#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "AppPaths.h"
#include "Config.h"

#include <string>

void ShowInfoDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message);
void ShowErrorDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message);
void ShowSettingsDialog(HWND owner, HINSTANCE instance);
bool ShowSettingsDialog(HWND owner, HINSTANCE instance, AppConfig& config);
bool ShowSettingsDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);
void ShowAboutDialog(HWND owner, HINSTANCE instance);
void ShowFfmpegDialog(HWND owner, HINSTANCE instance);
bool ShowFfmpegDialog(HWND owner, HINSTANCE instance, const AppPaths& paths, AppConfig& config);

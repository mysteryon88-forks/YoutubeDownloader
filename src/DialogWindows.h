#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <string>

void ShowInfoDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message);
void ShowErrorDialog(HWND owner, HINSTANCE instance, const std::wstring& title, const std::wstring& message);
void ShowSettingsDialog(HWND owner, HINSTANCE instance);
void ShowAboutDialog(HWND owner, HINSTANCE instance);
void ShowFfmpegDialog(HWND owner, HINSTANCE instance);

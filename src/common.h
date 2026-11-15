#pragma once

#include "beterwindow/ui/core.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dxgiformat.h>

#include <cstdio>
#include <unordered_set>
#include <vector>

std::wstring ToLowerW(std::wstring s);
std::wstring FileNameOf(const std::wstring& path);
bool PathsEqual(const std::wstring& a, const std::wstring& b);
std::wstring WFormat(const wchar_t* fmt, ...);

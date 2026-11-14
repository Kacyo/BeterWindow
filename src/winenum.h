#pragma once

#include "common.h"

struct WindowInfo {
    HWND hwnd = nullptr;
    std::wstring title;
    std::wstring exePath;
    std::wstring exeName;
    DWORD pid = 0;
    bool applied = false;
};

void EnumerateAppWindows(std::vector<WindowInfo>& out, HWND self);
std::wstring ExePathForPid(DWORD pid);
std::wstring ExePathForWindow(HWND hwnd);

class IconCache {
public:
    ~IconCache();

    ID2D1Bitmap* Get(ID2D1RenderTarget* rt, const std::wstring& exePath);
    void InvalidateBitmaps();
    void Clear();

private:
    struct Entry {
        HICON icon = nullptr;
        ID2D1Bitmap* bitmap = nullptr;
    };
    std::unordered_map<std::wstring, Entry> map_;
};

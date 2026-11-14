#include "winenum.h"

namespace {

bool IsCloaked(HWND hwnd) {
    int cloaked = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked != 0;
}

bool IsShellWindow(const wchar_t* cls) {
    static const wchar_t* kSkip[] = {
        L"Progman",
        L"WorkerW",
        L"Shell_TrayWnd",
        L"Shell_SecondaryTrayWnd",
        L"Windows.UI.Core.CoreWindow",
        L"MultitaskingViewFrame",
        L"ForegroundStaging",
        L"XamlExplorerHostIslandWindow",
    };
    for (auto* s : kSkip) {
        if (wcscmp(cls, s) == 0) return true;
    }
    return false;
}

struct EnumContext {
    std::vector<WindowInfo>* out = nullptr;
    HWND self = nullptr;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<EnumContext*>(param);
    if (!hwnd || hwnd == ctx->self) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    if (IsCloaked(hwnd)) return TRUE;

    wchar_t cls[256] = {};
    if (!GetClassNameW(hwnd, cls, 256)) return TRUE;
    if (IsShellWindow(cls)) return TRUE;

    int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    if (rc.right - rc.left < 40 || rc.bottom - rc.top < 40) return TRUE;

    std::wstring title;
    title.resize((size_t)len + 1);
    int copied = GetWindowTextW(hwnd, title.data(), len + 1);
    if (copied <= 0) return TRUE;
    title.resize((size_t)copied);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return TRUE;

    WindowInfo info;
    info.hwnd = hwnd;
    info.title = title;
    info.pid = pid;
    info.exePath = ExePathForPid(pid);
    if (info.exePath.empty()) return TRUE;
    info.exeName = FileNameOf(info.exePath);

    ctx->out->push_back(std::move(info));
    return TRUE;
}

}  // namespace

std::wstring ExePathForPid(DWORD pid) {
    std::wstring result;
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return result;

    wchar_t buf[1024];
    DWORD size = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    if (QueryFullProcessImageNameW(proc, 0, buf, &size) && size > 0) {
        result.assign(buf, size);
    }
    CloseHandle(proc);
    return result;
}

std::wstring ExePathForWindow(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid ? ExePathForPid(pid) : std::wstring();
}

void EnumerateAppWindows(std::vector<WindowInfo>& out, HWND self) {
    out.clear();
    EnumContext ctx{&out, self};
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
}

IconCache::~IconCache() {
    Clear();
}

void IconCache::Clear() {
    for (auto& kv : map_) {
        if (kv.second.bitmap) kv.second.bitmap->Release();
        if (kv.second.icon) DestroyIcon(kv.second.icon);
    }
    map_.clear();
}

void IconCache::InvalidateBitmaps() {
    for (auto& kv : map_) {
        if (kv.second.bitmap) {
            kv.second.bitmap->Release();
            kv.second.bitmap = nullptr;
        }
    }
}

namespace {

ID2D1Bitmap* CreateBitmapFromIcon(ID2D1RenderTarget* rt, HICON icon) {
    if (!rt || !icon) return nullptr;

    ICONINFO ii{};
    if (!GetIconInfo(icon, &ii)) return nullptr;

    BITMAP bm{};
    if (!ii.hbmColor || !GetObjectW(ii.hbmColor, sizeof(bm), &bm)) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        return nullptr;
    }

    const int w = bm.bmWidth;
    const int h = bm.bmHeight;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) {
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        return nullptr;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<DWORD> pixels((size_t)w * h, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        DeleteObject(ii.hbmColor);
        DeleteObject(ii.hbmMask);
        return nullptr;
    }

    GetDIBits(dc, ii.hbmColor, 0, (UINT)h, pixels.data(), &bi, DIB_RGB_COLORS);

    bool hasAlpha = false;
    for (DWORD p : pixels) {
        if ((p >> 24) != 0) {
            hasAlpha = true;
            break;
        }
    }

    if (!hasAlpha && ii.hbmMask) {
        BITMAPINFO mi{};
        mi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        mi.bmiHeader.biWidth = w;
        mi.bmiHeader.biHeight = -h;
        mi.bmiHeader.biPlanes = 1;
        mi.bmiHeader.biBitCount = 1;
        mi.bmiHeader.biCompression = BI_RGB;

        int stride = ((w + 31) / 32) * 4;
        std::vector<BYTE> mask((size_t)stride * h, 0);
        if (GetDIBits(dc, ii.hbmMask, 0, (UINT)h, mask.data(), &mi, DIB_RGB_COLORS) > 0) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    bool transparent = (mask[(size_t)y * stride + (x / 8)] & (0x80 >> (x % 8))) != 0;
                    DWORD& p = pixels[(size_t)y * w + x];
                    p = transparent ? 0 : (0xFF000000u | (p & 0x00FFFFFFu));
                }
            }
        }
    }

    for (auto& p : pixels) {
        DWORD a = (p >> 24) & 0xFF;
        if (a == 0) {
            p = 0;
            continue;
        }
        DWORD b = p & 0xFF;
        DWORD g = (p >> 8) & 0xFF;
        DWORD r = (p >> 16) & 0xFF;
        b = (b * a + 127) / 255;
        g = (g * a + 127) / 255;
        r = (r * a + 127) / 255;
        p = (a << 24) | (r << 16) | (g << 8) | b;
    }

    DeleteDC(dc);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);

    ID2D1Bitmap* bitmap = nullptr;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    HRESULT hr = rt->CreateBitmap(D2D1::SizeU((UINT32)w, (UINT32)h), pixels.data(),
                                  (UINT32)(w * 4), props, &bitmap);
    if (FAILED(hr)) return nullptr;
    return bitmap;
}

}  // namespace

ID2D1Bitmap* IconCache::Get(ID2D1RenderTarget* rt, const std::wstring& exePath) {
    if (!rt || exePath.empty()) return nullptr;

    std::wstring key = ToLowerW(exePath);
    auto it = map_.find(key);
    if (it != map_.end()) {
        if (it->second.bitmap) return it->second.bitmap;
        if (it->second.icon) {
            it->second.bitmap = CreateBitmapFromIcon(rt, it->second.icon);
            return it->second.bitmap;
        }
        map_.erase(it);
    }

    SHFILEINFOW sfi{};
    if (!SHGetFileInfoW(exePath.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_LARGEICON)) {
        return nullptr;
    }
    if (!sfi.hIcon) return nullptr;

    Entry e;
    e.icon = sfi.hIcon;
    e.bitmap = CreateBitmapFromIcon(rt, sfi.hIcon);
    auto& slot = map_[key];
    slot = e;
    return slot.bitmap;
}

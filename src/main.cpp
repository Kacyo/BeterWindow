#include "app.h"

#include <cstdlib>

namespace {

App* g_app = nullptr;
HICON g_iconBig = nullptr;
HICON g_iconSmall = nullptr;

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT kMenuShow = 1;
constexpr UINT kMenuExit = 2;

NOTIFYICONDATAW g_nid{};
bool g_trayAdded = false;
bool g_reallyExit = false;
UINT g_taskbarCreated = 0;

float DpiScaleOf(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn =
        (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT dpi = fn ? fn(hwnd) : 96;
    if (dpi == 0) dpi = 96;
    return (float)dpi / 96.f;
}

float SystemDpiScale() {
    typedef UINT(WINAPI * GetDpiForSystemFn)();
    static GetDpiForSystemFn fn =
        (GetDpiForSystemFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem");
    UINT dpi = fn ? fn() : 96;
    if (dpi == 0) dpi = 96;
    return (float)dpi / 96.f;
}

float RoundRectCoverage(float x, float y, float cx, float cy, float hx, float hy, float r) {
    float qx = std::fabs(x - cx) - (hx - r);
    float qy = std::fabs(y - cy) - (hy - r);
    float ox = (std::max)(qx, 0.f);
    float oy = (std::max)(qy, 0.f);
    float d = std::sqrt(ox * ox + oy * oy) + (std::min)((std::max)(qx, qy), 0.f) - r;
    return Clampf(0.5f - d, 0.f, 1.f);
}

HICON BuildIcon(int size) {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header);
    header.bV5Width = size;
    header.bV5Height = -size;
    header.bV5Planes = 1;
    header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000;
    header.bV5GreenMask = 0x0000FF00;
    header.bV5BlueMask = 0x000000FF;
    header.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&header, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        DeleteDC(dc);
        return nullptr;
    }

    const float s = (float)size;
    const float pad = s * 0.055f;
    const float rad = s * 0.24f;
    const D2D1_COLOR_F from = Rgb(0x8E7DFF);
    const D2D1_COLOR_F to = Rgb(0x4F8CFF);

    auto* pixels = (uint32_t*)bits;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float shape = RoundRectCoverage(px, py, s * 0.5f, s * 0.5f, s * 0.5f - pad,
                                            s * 0.5f - pad, rad);
            float t = (px + py) / (2.f * s);
            float r = from.r + (to.r - from.r) * t;
            float g = from.g + (to.g - from.g) * t;
            float b = from.b + (to.b - from.b) * t;

            float bar1 = RoundRectCoverage(px, py, s * 0.5f, s * 0.40f, s * 0.21f, s * 0.048f,
                                           s * 0.048f);
            float bar2 = RoundRectCoverage(px, py, s * 0.455f, s * 0.615f, s * 0.155f, s * 0.048f,
                                           s * 0.048f);
            float white = Clampf(bar1 * 0.95f + bar2 * 0.65f, 0.f, 0.95f);

            r = r + (1.f - r) * white;
            g = g + (1.f - g) * white;
            b = b + (1.f - b) * white;

            uint32_t a = (uint32_t)(shape * 255.f + 0.5f);
            uint32_t R = (uint32_t)(r * 255.f + 0.5f);
            uint32_t G = (uint32_t)(g * 255.f + 0.5f);
            uint32_t B = (uint32_t)(b * 255.f + 0.5f);
            pixels[y * size + x] = (a << 24) | (R << 16) | (G << 8) | B;
        }
    }

    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO info{};
    info.fIcon = TRUE;
    info.hbmMask = mask;
    info.hbmColor = color;
    HICON icon = CreateIconIndirect(&info);

    DeleteObject(color);
    if (mask) DeleteObject(mask);
    DeleteDC(dc);
    return icon;
}

void AddTrayIcon(HWND hwnd) {
    if (g_trayAdded) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = g_iconSmall ? g_iconSmall : LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(nid.szTip, L"BeterWindow · 窗口美化服务运行中", ARRAYSIZE(nid.szTip));

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);

    g_nid = nid;
    g_trayAdded = true;
}

void RemoveTrayIcon() {
    if (!g_trayAdded) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
}

void ShowTrayBalloon(const wchar_t* title, const wchar_t* text) {
    if (!g_trayAdded) return;

    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    nid.uTimeout = 6000;
    lstrcpynW(nid.szInfoTitle, title, ARRAYSIZE(nid.szInfoTitle));
    lstrcpynW(nid.szInfo, text, ARRAYSIZE(nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowMainWindow(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }
    SetForegroundWindow(hwnd);
}

void ShowTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, kMenuShow, L"显示主窗口");
    SetMenuDefaultItem(menu, kMenuShow, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"退出 BeterWindow");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    UINT cmd = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd,
                                    nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd == kMenuShow) {
        ShowMainWindow(hwnd);
    } else if (cmd == kMenuExit) {
        g_reallyExit = true;
        DestroyWindow(hwnd);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* app = (App*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = (CREATESTRUCTW*)lParam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        case WM_NCCALCSIZE:
            if (wParam) return 0;
            // wParam == FALSE：直接把客户区扩展为整个窗口，避免残留系统边框
            GetWindowRect(hwnd, (RECT*)lParam);
            return 0;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const float s = DpiScaleOf(hwnd);
            const int border = (int)(7.f * s);
            const bool maximized = IsZoomed(hwnd) != 0;

            if (!maximized) {
                bool left = pt.x < border;
                bool right = pt.x >= rc.right - border;
                bool top = pt.y < border;
                bool bottom = pt.y >= rc.bottom - border;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            int titleH = (int)(kTitleBarHeight * s);
            int buttonsW = (int)((46.f * 3.f + 8.f) * s);
            if (pt.y < titleH && pt.x < rc.right - buttonsW) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lParam;
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
            const float s = DpiScaleOf(hwnd);
            mmi->ptMinTrackSize.x = (LONG)(900.f * s);
            mmi->ptMinTrackSize.y = (LONG)(600.f * s);
            return 0;
        }
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCPAINT:
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (app) {
                app->Paint();
            } else {
                PAINTSTRUCT ps{};
                BeginPaint(hwnd, &ps);
                EndPaint(hwnd, &ps);
            }
            return 0;
        case WM_SIZE:
            if (app && wParam != SIZE_MINIMIZED) {
                app->Resize(LOWORD(lParam), HIWORD(lParam), DpiScaleOf(hwnd));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED: {
            RECT* suggested = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (app) {
                app->OnDpi(DpiScaleOf(hwnd));
                app->Resize(suggested->right - suggested->left,
                            suggested->bottom - suggested->top, DpiScaleOf(hwnd));
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1 && app) app->OnTimer();
            return 0;
        case WM_MOUSEMOVE: {
            if (!app) return 0;
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            app->MouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }
        case WM_MOUSELEAVE:
            if (app) app->MouseLeave();
            return 0;
        case WM_LBUTTONDOWN:
            if (app) {
                SetFocus(hwnd);
                app->LButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        case WM_LBUTTONUP:
            if (app) app->LButtonUp(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEWHEEL: {
            if (!app) return 0;
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd, &pt);
            app->MouseWheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        case WM_CHAR:
            if (app) app->Char((wchar_t)wParam);
            return 0;
        case WM_KEYDOWN:
            if (app) app->KeyDown((UINT)wParam);
            return 0;
        case WM_SETFOCUS:
            if (app) app->FocusChanged(true);
            return 0;
        case WM_KILLFOCUS:
            if (app) app->FocusChanged(false);
            return 0;
        case WM_ACTIVATE:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            if (!g_reallyExit) {
                // 托盘不可用时保持原有行为，避免窗口隐藏后无法找回
                if (!g_trayAdded) AddTrayIcon(hwnd);
                if (g_trayAdded) {
                    ShowWindow(hwnd, SW_HIDE);
                    ShowTrayBalloon(L"BeterWindow 仍在运行",
                                    L"主窗口已最小化到系统托盘。双击托盘图标可重新打开，"
                                    L"右键托盘图标可退出程序。");
                    return 0;
                }
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            RemoveTrayIcon();
            if (g_app) {
                g_app->Shutdown();
                delete g_app;
                g_app = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            if (msg == kTrayMessage) {
                const UINT event = LOWORD(lParam);
                if (event == NIN_SELECT || event == NIN_KEYSELECT ||
                    event == NIN_BALLOONUSERCLICK || event == WM_LBUTTONUP ||
                    event == WM_LBUTTONDBLCLK) {
                    ShowMainWindow(hwnd);
                } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                    ShowTrayMenu(hwnd);
                }
                return 0;
            }
            if (msg == g_taskbarCreated && g_taskbarCreated != 0) {
                // 资源管理器重启后重新注册托盘图标
                g_trayAdded = false;
                AddTrayIcon(hwnd);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    g_iconBig = BuildIcon(32);
    g_iconSmall = BuildIcon(16);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = g_iconBig;
    wc.hIconSm = g_iconSmall;
    wc.lpszClassName = L"BeterWindowClass";
    if (!RegisterClassExW(&wc)) return 0;

    AppConfig boot;
    LoadConfig(boot);

    const float scale = SystemDpiScale();
    int pxW = (int)(boot.winW * scale);
    int pxH = (int)(boot.winH * scale);

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int availW = work.right - work.left;
    int availH = work.bottom - work.top;
    pxW = (std::min)(pxW, availW - (int)(36 * scale));
    pxH = (std::min)(pxH, availH - (int)(36 * scale));
    int x = work.left + ((work.right - work.left) - pxW) / 2;
    int y = work.top + ((work.bottom - work.top) - pxH) / 2;

    App* app = new App();
    HWND hwnd = CreateWindowExW(0, L"BeterWindowClass", L"BeterWindow",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, pxW, pxH, nullptr,
                                nullptr, instance, app);
    if (!hwnd) {
        delete app;
        return 0;
    }

    g_app = app;
    if (!app->Init(instance, hwnd, DpiScaleOf(hwnd))) {
        DestroyWindow(hwnd);
        return 0;
    }

    SetTimer(hwnd, 1, 16, nullptr);
    AddTrayIcon(hwnd);
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, 1);
    if (g_iconBig) DestroyIcon(g_iconBig);
    if (g_iconSmall) DestroyIcon(g_iconSmall);
    CoUninitialize();
    return 0;
}

#include "effects.h"

#include <vector>

using namespace bwui;

const TintPreset kTints[] = {
    {L"午夜黑", 0x0B0B0F},
    {L"石墨灰", 0x1B1C22},
    {L"雾霾蓝", 0x1C2A3D},
    {L"靛紫", 0x271F3D},
    {L"松石绿", 0x14302C},
    {L"奶油白", 0xEDE7DC},
};
const int kTintCount = (int)(sizeof(kTints) / sizeof(kTints[0]));

namespace {

struct ACCENTPOLICY {
    int nAccentState;
    int nFlags;
    int nColor;
    int nAnimationId;
};

struct WINCOMPATTRDATA {
    int nAttribute;
    PVOID pvData;
    SIZE_T ulSize;
};

typedef BOOL(WINAPI* SetWindowCompositionAttributeFn)(HWND, WINCOMPATTRDATA*);
typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);

constexpr int WCA_ACCENT_POLICY = 19;
constexpr int ACCENT_DISABLED = 0;
constexpr int ACCENT_ENABLE_BLURBEHIND = 3;
constexpr int ACCENT_ENABLE_ACRYLICBLURBEHIND = 4;
constexpr int ACCENT_FLAG_DRAW_ALL_BORDERS = 2;

SetWindowCompositionAttributeFn g_setComposition = nullptr;
GetDpiForWindowFn g_getDpiForWindow = nullptr;
bool g_windows11 = false;
unsigned int g_build = 0;

void EnsureEnvironment() {
    static bool done = false;
    if (done) return;
    done = true;

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        g_setComposition =
            (SetWindowCompositionAttributeFn)GetProcAddress(user32, "SetWindowCompositionAttribute");
        g_getDpiForWindow = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        typedef LONG(WINAPI * RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        auto fn = (RtlGetVersionFn)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) {
            RTL_OSVERSIONINFOW vi{};
            vi.dwOSVersionInfoSize = sizeof(vi);
            if (fn(&vi) == 0) {
                g_build = vi.dwBuildNumber;
                g_windows11 = (vi.dwMajorVersion > 10) ||
                              (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 22000);
            }
        }
    }
}

DWORD AbgrFromTint(const EffectSettings& s) {
    unsigned rgb = kTints[Clampi(s.tintIndex, 0, kTintCount - 1)].rgb;
    float t = Clampf(s.tintAlpha / 100.f, 0.f, 1.f);
    DWORD alpha = (DWORD)(0x1C + t * (0xF0 - 0x1C));
    DWORD r = (rgb >> 16) & 0xFF;
    DWORD g = (rgb >> 8) & 0xFF;
    DWORD b = rgb & 0xFF;
    return (alpha << 24) | (b << 16) | (g << 8) | r;
}

void ApplyAccent(HWND hwnd, int state, int flags, DWORD color) {
    if (!g_setComposition) return;
    ACCENTPOLICY policy{};
    policy.nAccentState = state;
    policy.nFlags = flags;
    policy.nColor = (int)color;
    policy.nAnimationId = 0;
    WINCOMPATTRDATA data{};
    data.nAttribute = WCA_ACCENT_POLICY;
    data.pvData = &policy;
    data.ulSize = sizeof(policy);
    g_setComposition(hwnd, &data);
}

void ApplyGlass(HWND hwnd, const EffectSettings& s) {
    if (s.glass == GlassMode::Mica) {
        if (g_windows11) {
            int backdrop = DWMSBT_MAINWINDOW;  // Mica
            HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                                               sizeof(backdrop));
            if (SUCCEEDED(hr)) {
                ApplyAccent(hwnd, ACCENT_DISABLED, 0, 0);
                return;
            }
        }
        // 不支持 Mica 时退回亚克力
        ApplyAccent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND, ACCENT_FLAG_DRAW_ALL_BORDERS,
                    AbgrFromTint(s));
        return;
    }

    if (g_windows11) {
        int none = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &none, sizeof(none));
    }

    switch (s.glass) {
        case GlassMode::Blur:
            ApplyAccent(hwnd, ACCENT_ENABLE_BLURBEHIND, ACCENT_FLAG_DRAW_ALL_BORDERS, 0);
            break;
        case GlassMode::Acrylic:
            ApplyAccent(hwnd, ACCENT_ENABLE_ACRYLICBLURBEHIND, ACCENT_FLAG_DRAW_ALL_BORDERS,
                        AbgrFromTint(s));
            break;
        default:
            ApplyAccent(hwnd, ACCENT_DISABLED, 0, 0);
            break;
    }
}

void ApplyLayered(HWND hwnd, const EffectSettings& s, bool hadLayered) {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    bool wantLayered = s.transparency;
    bool isLayered = (ex & WS_EX_LAYERED) != 0;

    if (wantLayered) {
        if (!isLayered) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_FRAMECHANGED);
        }
        BYTE alpha = (BYTE)Clampi(s.opacity * 255 / 100, 12, 255);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    } else if (isLayered && !hadLayered) {
        // 只移除由本工具添加的层叠样式，避免破坏窗口自身设置
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    } else if (isLayered) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }
}

bool GetCornerGeometry(HWND hwnd, CornerMode mode, int& width, int& height, int& radius) {
    if (mode == CornerMode::SystemDefault || !IsWindow(hwnd)) return false;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return false;
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return false;

    radius = 0;
    if (mode == CornerMode::Small || mode == CornerMode::Large) {
        // Windows 11 原生小圆角约 4 DIP，默认圆角约 8 DIP
        int dip = (mode == CornerMode::Small) ? 4 : 8;
        UINT dpi = g_getDpiForWindow ? g_getDpiForWindow(hwnd) : 96;
        if (dpi == 0) dpi = 96;
        radius = (std::max)(1, (int)MulDiv(dip, dpi, 96));
        radius = (std::min)(radius, (std::min)(width, height) / 2);
    }
    return true;
}

}  // namespace

bool IsWin11OrLater() {
    EnsureEnvironment();
    return g_windows11;
}

unsigned int WindowsBuildNumber() {
    EnsureEnvironment();
    return g_build;
}

unsigned long long SettingsStamp(const EffectSettings& s) {
    unsigned long long v = 1469598103934665603ull;
    auto mix = [&v](unsigned long long x) {
        v ^= x;
        v *= 1099511628211ull;
    };
    mix(s.transparency ? 1 : 0);
    mix((unsigned long long)s.opacity);
    mix((unsigned long long)s.corner);
    mix((unsigned long long)s.glass);
    mix((unsigned long long)s.tintIndex);
    mix((unsigned long long)s.tintAlpha);
    return v;
}

void EffectManager::ApplyCorner(HWND hwnd, CornerMode mode, State& st) {
    st.corner = mode;

    auto clearRegion = [&]() {
        if (st.regionApplied) SetWindowRgn(hwnd, nullptr, TRUE);
        st.regionApplied = false;
        st.regionWidth = st.regionHeight = st.regionRadius = 0;
    };
    auto restoreNcRendering = [&]() {
        if (!st.ncRenderingChanged) return;
        int policy = st.origNcPolicy;
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        st.ncRenderingChanged = false;
    };

    if (g_windows11) {
        // 带标题栏窗口的可见框架由 DWM 绘制，必须使用原生属性才能可靠改变圆角
        clearRegion();
        restoreNcRendering();
        int pref = st.origCorner;
        if (mode == CornerMode::Off) pref = DWMWCP_DONOTROUND;
        if (mode == CornerMode::Small) pref = DWMWCP_ROUNDSMALL;
        if (mode == CornerMode::Large) pref = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        return;
    }

    if (mode == CornerMode::SystemDefault || mode == CornerMode::Off) {
        clearRegion();
        restoreNcRendering();
        return;
    }

    // Windows 10：区域裁剪需要同时关闭 DWM 的非客户区渲染，否则标题栏仍是直角
    int policy = DWMNCRP_DISABLED;
    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy)))) {
        st.ncRenderingChanged = true;
    }

    int width = 0;
    int height = 0;
    int radius = 0;
    if (!GetCornerGeometry(hwnd, mode, width, height, radius)) return;

    HRGN rgn = CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
    if (!rgn) return;
    if (SetWindowRgn(hwnd, rgn, TRUE) == 0) {
        DeleteObject(rgn);
        return;
    }

    // 成功后区域由系统持有
    st.regionApplied = true;
    st.regionWidth = width;
    st.regionHeight = height;
    st.regionRadius = radius;
}

void EffectManager::Apply(HWND hwnd, const EffectSettings& s) {
    if (!hwnd || !IsWindow(hwnd)) return;
    EnsureEnvironment();

    State& st = states_[hwnd];
    if (st.stamp == 0) {
        st.origExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        st.hadLayered = (st.origExStyle & WS_EX_LAYERED) != 0;

        int value = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &value,
                                            sizeof(value)))) {
            st.origCorner = value;
        }
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &value,
                                            sizeof(value)))) {
            st.origNcPolicy = value;
        }
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &value,
                                            sizeof(value)))) {
            st.origBackdrop = value;
        }
    }

    ApplyLayered(hwnd, s, st.hadLayered);

    ApplyCorner(hwnd, s.corner, st);

    ApplyGlass(hwnd, s);

    st.stamp = SettingsStamp(s);
}

void EffectManager::Restore(HWND hwnd) {
    auto it = states_.find(hwnd);
    if (it == states_.end()) return;
    if (IsWindow(hwnd)) {
        State& st = it->second;

        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (!st.hadLayered && (ex & WS_EX_LAYERED)) {
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, st.origExStyle);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_FRAMECHANGED);
        } else if (st.hadLayered) {
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        }

        int pref = st.origCorner;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
        if (st.regionApplied) SetWindowRgn(hwnd, nullptr, TRUE);
        if (st.ncRenderingChanged) {
            int policy = st.origNcPolicy;
            DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        }

        int backdrop = st.origBackdrop;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

        ApplyAccent(hwnd, ACCENT_DISABLED, 0, 0);
    }
    states_.erase(it);
}

void EffectManager::RestoreAll() {
    std::vector<HWND> keys;
    keys.reserve(states_.size());
    for (auto& kv : states_) keys.push_back(kv.first);
    for (HWND h : keys) Restore(h);
}

bool EffectManager::IsApplied(HWND hwnd) const {
    return states_.find(hwnd) != states_.end();
}

unsigned long long EffectManager::StampOf(HWND hwnd) const {
    auto it = states_.find(hwnd);
    return it == states_.end() ? 0 : it->second.stamp;
}

size_t EffectManager::AppliedCount() const {
    return states_.size();
}

void EffectManager::ForgetDeadWindows() {
    for (auto it = states_.begin(); it != states_.end();) {
        if (!IsWindow(it->first)) {
            it = states_.erase(it);
        } else {
            ++it;
        }
    }
}

void EffectManager::RefreshCorners() {
    for (auto& kv : states_) {
        HWND hwnd = kv.first;
        State& st = kv.second;
        if (g_windows11 || st.corner == CornerMode::SystemDefault ||
            st.corner == CornerMode::Off || !IsWindow(hwnd)) {
            continue;
        }

        int width = 0;
        int height = 0;
        int radius = 0;
        if (!GetCornerGeometry(hwnd, st.corner, width, height, radius)) continue;
        if (!st.regionApplied || st.regionWidth != width || st.regionHeight != height ||
            st.regionRadius != radius) {
            ApplyCorner(hwnd, st.corner, st);
        }
    }
}

#pragma once

#include "common.h"

// 磨砂玻璃模式
enum class GlassMode : int {
    None = 0,
    Blur = 1,
    Acrylic = 2,
    Mica = 3,
};

// Windows 11 使用 DWM 原生圆角；Windows 10 使用窗口区域并接管框架渲染
// 旧配置中的 0/1/2 分别对应直角、小圆角、大圆角，保持兼容
// -1 表示不修改目标窗口的系统默认圆角行为
enum class CornerMode : int {
    SystemDefault = -1,
    Off = 0,
    Small = 1,
    Large = 2,
};

struct EffectSettings {
    bool transparency = false;
    int opacity = 92;  // 20 - 100
    CornerMode corner = CornerMode::SystemDefault;
    GlassMode glass = GlassMode::Acrylic;
    int tintIndex = 0;
    int tintAlpha = 55;  // 0 - 100
};

struct TintPreset {
    const wchar_t* name;
    unsigned rgb;
};

extern const TintPreset kTints[];
extern const int kTintCount;

bool IsWin11OrLater();
unsigned int WindowsBuildNumber();

// 设置信息指纹，用于判断是否需要重新应用
unsigned long long SettingsStamp(const EffectSettings& s);

class EffectManager {
public:
    void Apply(HWND hwnd, const EffectSettings& s);
    void Restore(HWND hwnd);
    void RestoreAll();
    bool IsApplied(HWND hwnd) const;
    unsigned long long StampOf(HWND hwnd) const;
    size_t AppliedCount() const;
    void ForgetDeadWindows();
    void RefreshCorners();

private:
    struct State {
        unsigned long long stamp = 0;
        LONG_PTR origExStyle = 0;
        bool hadLayered = false;
        bool regionApplied = false;
        CornerMode corner = CornerMode::SystemDefault;
        int regionWidth = 0;
        int regionHeight = 0;
        int regionRadius = 0;
        int origCorner = 0;
        int origNcPolicy = DWMNCRP_USEWINDOWSTYLE;
        bool ncRenderingChanged = false;
        int origBackdrop = 0;
    };
    void ApplyCorner(HWND hwnd, CornerMode mode, State& st);
    std::unordered_map<HWND, State> states_;
};

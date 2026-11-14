#pragma once

#include "common.h"
#include "config.h"
#include "effects.h"
#include "painter.h"
#include "winenum.h"

constexpr float kTitleBarHeight = 52.f;
constexpr float kSidebarWidth = 216.f;
constexpr float kStatusBarHeight = 30.f;

enum class Page : int {
    Windows = 0,
    Effects = 1,
    Rules = 2,
    About = 3,
    Count = 4,
};

enum class Kind : int {
    None = 0,
    NavWindow,
    NavEffects,
    NavRules,
    NavAbout,
    TitleMin,
    TitleMax,
    TitleClose,
    SearchField,
    Refresh,
    WindowRow,
    ApplySelected,
    RestoreSelected,
    ApplyAll,
    RestoreAll,
    ToggleTransparency,
    ToggleAutoApply,
    SliderOpacity,
    SliderTint,
    SegCorner,
    SegGlass,
    SegScope,
    TintSwatch,
    AddExclude,
    AddInclude,
    RemoveExclude,
    RemoveInclude,
};

struct Hit {
    uint64_t id = 0;
    D2D1_RECT_F rect{};
    Kind kind = Kind::None;
    int arg = 0;
    bool enabled = true;
};

enum class BtnStyle {
    Primary,
    Secondary,
    Subtle,
    Danger,
};

class App {
public:
    bool Init(HINSTANCE instance, HWND hwnd, float dpiScale);
    void Shutdown();

    void Paint();
    void Resize(int pixelWidth, int pixelHeight, float dpiScale);
    void OnDpi(float dpiScale);
    void OnTimer();

    void MouseMove(int px, int py);
    void MouseLeave();
    void LButtonDown(int px, int py);
    void LButtonUp(int px, int py);
    void MouseWheel(int px, int py, int delta);
    void Char(wchar_t ch);
    void KeyDown(UINT vk);
    void FocusChanged(bool focused);

    void CloseRequested() { closeRequested_ = true; }
    bool closeRequested() const { return closeRequested_; }

private:
    // 数据
    void RefreshWindows();
    const WindowInfo* SelectedInfo() const;
    bool MatchesRules(const WindowInfo& info) const;
    bool Excluded(const std::wstring& exePath) const;
    bool Included(const std::wstring& exePath) const;
    void RunAutoApply(bool force);
    void ApplyToSelected();
    void RestoreSelected();
    void AddPathTo(std::vector<std::wstring>& list, const std::wstring& path);
    void RemoveAt(std::vector<std::wstring>& list, int index);
    void Save();
    void Invalidate();

    // 绘制
    void DrawChrome(float w, float h);
    void DrawStatusBar(float w, float h);
    void DrawPageHeader(const D2D1_RECT_F& area, const wchar_t* title,
                        const std::wstring& subtitle);
    void DrawWindowPage(const D2D1_RECT_F& area);
    void DrawWindowList(const D2D1_RECT_F& list);
    void DrawEffectsPage(const D2D1_RECT_F& area);
    void DrawRulesPage(const D2D1_RECT_F& area);
    void DrawAboutPage(const D2D1_RECT_F& area);

    // 控件
    void Card(const D2D1_RECT_F& rect);
    void DrawButton(uint64_t id, Kind kind, int arg, const D2D1_RECT_F& rect,
                    const wchar_t* label, BtnStyle style, bool enabled = true,
                    const wchar_t* icon = nullptr);
    void DrawIconButton(uint64_t id, Kind kind, const D2D1_RECT_F& rect, const wchar_t* glyph);
    void DrawToggle(uint64_t id, Kind kind, const D2D1_RECT_F& rect, bool on,
                    bool enabled = true);
    void DrawSegmented(uint64_t id, Kind kind, const D2D1_RECT_F& rect,
                       const wchar_t* const* labels, int count, int selected,
                       bool enabled = true);
    void DrawSlider(uint64_t id, Kind kind, const D2D1_RECT_F& rect, int value, int minValue,
                    int maxValue, bool enabled);
    void AddHit(uint64_t id, Kind kind, int arg, const D2D1_RECT_F& rect, bool enabled = true);
    bool Hot(uint64_t id) const { return hot_ == id; }
    const Hit* HitAt(float x, float y) const;
    float Anim(uint64_t id, bool on);
    void UpdateSliderDrag(float x);

    // 状态
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    Painter painter_;
    IconCache icons_;
    EffectManager effects_;
    AppConfig cfg_;
    std::vector<WindowInfo> windows_;
    std::vector<size_t> visibleIdx_;
    std::vector<HWND> visibleHwnds_;
    std::unordered_set<HWND> manual_;
    HWND selected_ = nullptr;

    Page page_ = Page::Windows;
    float dpiScale_ = 1.f;
    float scroll_[4] = {0, 0, 0, 0};
    float scrollMax_[4] = {0, 0, 0, 0};
    float listScroll_ = 0.f;
    float listScrollMax_ = 0.f;
    D2D1_RECT_F listRect_{};
    D2D1_RECT_F opacitySlider_{};
    D2D1_RECT_F tintSlider_{};

    std::wstring query_;
    bool searchFocused_ = false;
    bool caretOn_ = true;

    uint64_t hot_ = 0;
    uint64_t active_ = 0;
    Kind activeKind_ = Kind::None;

    std::vector<Hit> hits_;
    std::vector<Hit> lastHits_;

    std::unordered_map<uint64_t, float> anim_;
    bool animating_ = false;

    std::wstring status_ = L"就绪";
    int tick_ = 0;
    bool closeRequested_ = false;
    bool initialized_ = false;
};

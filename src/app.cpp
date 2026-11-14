#include "app.h"

#include <d2d1helper.h>

namespace {

// ---- 调色板 ----
const D2D1_COLOR_F cWindow = Rgb(0x0F1013);
const D2D1_COLOR_F cSidebar = Rgb(0x14151A);
const D2D1_COLOR_F cCard = Rgb(0x17181D);
const D2D1_COLOR_F cCardDeep = Rgb(0x131419);
const D2D1_COLOR_F cHover = Rgb(0x1D1E25);
const D2D1_COLOR_F cBorder = Rgb(0x24252C);
const D2D1_COLOR_F cBorderHi = Rgb(0x30323B);
const D2D1_COLOR_F cText = Rgb(0xECEDF2);
const D2D1_COLOR_F cSub = Rgb(0x9A9BA5);
const D2D1_COLOR_F cMuted = Rgb(0x62646E);
const D2D1_COLOR_F cAccent = Rgb(0x7C6CFF);
const D2D1_COLOR_F cAccent2 = Rgb(0x4F8CFF);
const D2D1_COLOR_F cOk = Rgb(0x4ADE80);
const D2D1_COLOR_F cDanger = Rgb(0xF87171);

const wchar_t* const kCornerLabels[] = {L"系统默认", L"无效果", L"小圆角", L"大圆角"};
const wchar_t* const kGlassLabels[] = {L"关闭", L"模糊", L"亚克力", L"Mica"};
const wchar_t* const kScopeLabels[] = {L"所有窗口", L"仅指定程序"};

uint64_t H(const char* s) {
    uint64_t h = 1469598103934665603ull;
    while (*s) {
        h ^= (unsigned char)(*s++);
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t RowId(HWND hwnd) {
    return H("row") ^ (uint64_t)(uintptr_t)hwnd;
}

}  // namespace

// ============================================================
// 生命周期
// ============================================================

bool App::Init(HINSTANCE instance, HWND hwnd, float dpiScale) {
    instance_ = instance;
    hwnd_ = hwnd;
    dpiScale_ = dpiScale;

    LoadConfig(cfg_);
    if (!painter_.Init(hwnd_)) return false;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!painter_.Resize((UINT)(std::max)(1L, rc.right - rc.left),
                         (UINT)(std::max)(1L, rc.bottom - rc.top), dpiScale_)) {
        return false;
    }

    RefreshWindows();
    RunAutoApply(cfg_.autoApply);
    status_ = L"就绪 · 选择窗口后点击「应用效果」，或在「生效规则」中配置批量生效";
    initialized_ = true;
    return true;
}

void App::Shutdown() {
    if (!initialized_) return;
    Save();
    effects_.RestoreAll();
    painter_.Release();
    icons_.Clear();
    initialized_ = false;
}

void App::Resize(int pixelWidth, int pixelHeight, float dpiScale) {
    dpiScale_ = dpiScale;
    painter_.Resize((UINT)(std::max)(1, pixelWidth), (UINT)(std::max)(1, pixelHeight), dpiScale);

    // 记住窗口尺寸（DIP），下次启动沿用
    if (pixelWidth > 200 && pixelHeight > 200 && dpiScale > 0.f) {
        cfg_.winW = (int)std::lround(pixelWidth / dpiScale);
        cfg_.winH = (int)std::lround(pixelHeight / dpiScale);
    }
}

void App::OnDpi(float dpiScale) {
    dpiScale_ = dpiScale;
    painter_.SetDpi(dpiScale);
    Invalidate();
}

void App::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::Paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);

    RECT client{};
    GetClientRect(hwnd_, &client);
    painter_.Resize((UINT)(std::max)(1L, client.right - client.left),
                    (UINT)(std::max)(1L, client.bottom - client.top), dpiScale_);

    bool ok = painter_.Begin();
    if (ok) {
        hits_.clear();
        D2D1_SIZE_F size = painter_.rt()->GetSize();
        DrawChrome(size.width, size.height);

        D2D1_RECT_F area =
            RectF(kSidebarWidth, kTitleBarHeight, size.width, size.height - kStatusBarHeight);
        switch (page_) {
            case Page::Windows:
                DrawWindowPage(area);
                break;
            case Page::Effects:
                DrawEffectsPage(area);
                break;
            case Page::Rules:
                DrawRulesPage(area);
                break;
            case Page::About:
                DrawAboutPage(area);
                break;
            default:
                break;
        }

        DrawStatusBar(size.width, size.height);

        if (!painter_.End()) {
            // 设备丢失，重建资源
            painter_.Resize(1, 1, dpiScale_);
            icons_.InvalidateBitmaps();
            ok = false;
        }
        lastHits_.swap(hits_);
        if (!ok) Invalidate();
    }

    EndPaint(hwnd_, &ps);
}

// ============================================================
// 数据与规则
// ============================================================

void App::RefreshWindows() {
    windows_.clear();
    EnumerateAppWindows(windows_, hwnd_);
    if (selected_ && !IsWindow(selected_)) selected_ = nullptr;

    for (auto it = manual_.begin(); it != manual_.end();) {
        if (!IsWindow(*it)) {
            it = manual_.erase(it);
        } else {
            ++it;
        }
    }
    if (selected_) {
        bool found = false;
        for (auto& w : windows_) {
            if (w.hwnd == selected_) {
                found = true;
                break;
            }
        }
        if (!found) selected_ = nullptr;
    }
}

const WindowInfo* App::SelectedInfo() const {
    if (!selected_) return nullptr;
    for (auto& w : windows_) {
        if (w.hwnd == selected_) return &w;
    }
    return nullptr;
}

bool App::Excluded(const std::wstring& exePath) const {
    for (auto& p : cfg_.exclude) {
        if (PathsEqual(p, exePath)) return true;
    }
    return false;
}

bool App::Included(const std::wstring& exePath) const {
    for (auto& p : cfg_.include) {
        if (PathsEqual(p, exePath)) return true;
    }
    return false;
}

bool App::MatchesRules(const WindowInfo& info) const {
    if (Excluded(info.exePath)) return false;
    if (cfg_.scopeMode == 0) return true;
    return Included(info.exePath);
}

void App::RunAutoApply(bool force) {
    if (!force && !cfg_.autoApply) return;

    unsigned long long stamp = SettingsStamp(cfg_.effects);
    for (auto& wi : windows_) {
        bool match = MatchesRules(wi) || manual_.count(wi.hwnd) > 0;
        if (match) {
            if (force || effects_.StampOf(wi.hwnd) != stamp) {
                effects_.Apply(wi.hwnd, cfg_.effects);
            }
        } else if (effects_.IsApplied(wi.hwnd)) {
            effects_.Restore(wi.hwnd);
        }
    }
    effects_.ForgetDeadWindows();
}

void App::ApplyToSelected() {
    const WindowInfo* info = SelectedInfo();
    if (!info) {
        status_ = L"请先在窗口列表中选择一个窗口";
        Invalidate();
        return;
    }
    effects_.Apply(info->hwnd, cfg_.effects);
    manual_.insert(info->hwnd);
    status_ = WFormat(L"已对「%s」应用外观效果", info->title.c_str());
    Save();
    Invalidate();
}

void App::RestoreSelected() {
    const WindowInfo* info = SelectedInfo();
    if (!info) return;
    effects_.Restore(info->hwnd);
    manual_.erase(info->hwnd);
    status_ = WFormat(L"已恢复「%s」的原始外观", info->title.c_str());
    Invalidate();
}

void App::AddPathTo(std::vector<std::wstring>& list, const std::wstring& path) {
    if (path.empty()) return;
    for (auto& p : list) {
        if (PathsEqual(p, path)) return;
    }
    list.push_back(path);
}

void App::RemoveAt(std::vector<std::wstring>& list, int index) {
    if (index < 0 || index >= (int)list.size()) return;
    list.erase(list.begin() + index);
}

void App::Save() {
    SaveConfig(cfg_);
}

// ============================================================
// 输入
// ============================================================

const Hit* App::HitAt(float x, float y) const {
    for (auto it = lastHits_.rbegin(); it != lastHits_.rend(); ++it) {
        if (it->enabled && PointIn(it->rect, x, y)) return &(*it);
    }
    return nullptr;
}

float App::Anim(uint64_t id, bool on) {
    float target = on ? 1.f : 0.f;
    auto it = anim_.find(id);
    if (it == anim_.end()) {
        anim_[id] = target;
        return target;
    }
    float& v = it->second;
    if (std::fabs(v - target) > 0.002f) {
        v += (target - v) * 0.32f;
        if (std::fabs(v - target) <= 0.002f) v = target;
        animating_ = true;
    }
    return v;
}

void App::UpdateSliderDrag(float x) {
    auto update = [&](const D2D1_RECT_F& r, int minV, int maxV, int& target) {
        if (r.right - r.left <= 1.f) return;
        float t = Clampf((x - r.left) / (r.right - r.left), 0.f, 1.f);
        int v = Clampi((int)std::lround(minV + t * (maxV - minV)), minV, maxV);
        if (v != target) {
            target = v;
            Save();
            Invalidate();
        }
    };
    if (activeKind_ == Kind::SliderOpacity) {
        update(opacitySlider_, 20, 100, cfg_.effects.opacity);
    } else if (activeKind_ == Kind::SliderTint) {
        update(tintSlider_, 0, 100, cfg_.effects.tintAlpha);
    }
}

void App::MouseMove(int px, int py) {
    float x = px / dpiScale_;
    float y = py / dpiScale_;

    if (active_ != 0) {
        UpdateSliderDrag(x);
        return;
    }

    const Hit* hit = HitAt(x, y);
    uint64_t newHot = (hit && hit->enabled) ? hit->id : 0;
    if (newHot != hot_) {
        hot_ = newHot;
        Invalidate();
    }
}

void App::MouseLeave() {
    if (hot_ != 0) {
        hot_ = 0;
        Invalidate();
    }
}

void App::LButtonDown(int px, int py) {
    float x = px / dpiScale_;
    float y = py / dpiScale_;
    const Hit* hit = HitAt(x, y);
    if (!hit || !hit->enabled) {
        if (searchFocused_) {
            searchFocused_ = false;
            Invalidate();
        }
        return;
    }
    active_ = hit->id;
    activeKind_ = hit->kind;
    if (hit->kind == Kind::SearchField) {
        searchFocused_ = true;
        Invalidate();
        return;
    }
    if (hit->kind == Kind::SliderOpacity || hit->kind == Kind::SliderTint) {
        UpdateSliderDrag(x);
    }
    Invalidate();
}

void App::LButtonUp(int px, int py) {
    float x = px / dpiScale_;
    float y = py / dpiScale_;
    uint64_t pressed = active_;
    Kind pressedKind = activeKind_;
    active_ = 0;
    activeKind_ = Kind::None;

    if (pressed == 0) return;

    const Hit* hit = HitAt(x, y);
    if (!hit || hit->id != pressed || !hit->enabled) {
        Invalidate();
        return;
    }

    // 滑杆在拖动过程中已实时更新，松开时不再重复处理
    if (pressedKind == Kind::SliderOpacity || pressedKind == Kind::SliderTint) {
        Save();
        return;
    }

    const int arg = hit->arg;
    switch (hit->kind) {
        case Kind::NavWindow:
            page_ = Page::Windows;
            break;
        case Kind::NavEffects:
            page_ = Page::Effects;
            break;
        case Kind::NavRules:
            page_ = Page::Rules;
            break;
        case Kind::NavAbout:
            page_ = Page::About;
            break;
        case Kind::TitleMin:
            SendMessageW(hwnd_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            break;
        case Kind::TitleMax:
            SendMessageW(hwnd_, WM_SYSCOMMAND, IsZoomed(hwnd_) ? SC_RESTORE : SC_MAXIMIZE, 0);
            break;
        case Kind::TitleClose:
            CloseRequested();
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case Kind::SearchField:
            searchFocused_ = true;
            break;
        case Kind::Refresh:
            RefreshWindows();
            status_ = WFormat(L"窗口列表已刷新 · %d 个可见窗口", (int)windows_.size());
            break;
        case Kind::WindowRow:
            if (arg >= 0 && arg < (int)visibleHwnds_.size()) {
                selected_ = visibleHwnds_[arg];
                status_ = L"已选择窗口，可单独应用外观效果";
            }
            break;
        case Kind::ApplySelected:
            ApplyToSelected();
            break;
        case Kind::RestoreSelected:
            RestoreSelected();
            break;
        case Kind::ApplyAll: {
            RefreshWindows();
            RunAutoApply(true);
            status_ = WFormat(L"已按规则应用 · 当前修改 %d 个窗口",
                              (int)effects_.AppliedCount());
            break;
        }
        case Kind::RestoreAll:
            effects_.RestoreAll();
            manual_.clear();
            status_ = L"已恢复所有被修改的窗口";
            break;
        case Kind::ToggleTransparency:
            cfg_.effects.transparency = !cfg_.effects.transparency;
            Save();
            break;
        case Kind::ToggleAutoApply:
            cfg_.autoApply = !cfg_.autoApply;
            Save();
            break;
        case Kind::SegCorner:
            cfg_.effects.corner = (CornerMode)Clampi(arg - 1, -1, 2);
            Save();
            break;
        case Kind::SegGlass:
            cfg_.effects.glass = (GlassMode)Clampi(arg, 0, 3);
            Save();
            break;
        case Kind::SegScope:
            cfg_.scopeMode = Clampi(arg, 0, 1);
            Save();
            break;
        case Kind::TintSwatch:
            cfg_.effects.tintIndex = Clampi(arg, 0, kTintCount - 1);
            Save();
            break;
        case Kind::AddExclude: {
            const WindowInfo* info = SelectedInfo();
            if (info) {
                AddPathTo(cfg_.exclude, info->exePath);
                effects_.Restore(info->hwnd);
                manual_.erase(info->hwnd);
                status_ = WFormat(L"已排除「%s」", info->exeName.c_str());
                Save();
            }
            break;
        }
        case Kind::AddInclude: {
            const WindowInfo* info = SelectedInfo();
            if (info) {
                AddPathTo(cfg_.include, info->exePath);
                status_ = WFormat(L"已加入指定列表「%s」", info->exeName.c_str());
                Save();
                RunAutoApply(cfg_.autoApply);
            }
            break;
        }
        case Kind::RemoveExclude:
            RemoveAt(cfg_.exclude, arg);
            Save();
            break;
        case Kind::RemoveInclude:
            RemoveAt(cfg_.include, arg);
            Save();
            break;
        default:
            break;
    }
    Invalidate();
}

void App::MouseWheel(int px, int py, int delta) {
    float x = px / dpiScale_;
    float y = py / dpiScale_;
    float step = delta > 0 ? -1.f : 1.f;

    if (page_ == Page::Windows && PointIn(listRect_, x, y)) {
        listScroll_ = Clampf(listScroll_ + step * 62.f, 0.f, listScrollMax_);
        Invalidate();
        return;
    }

    int p = (int)page_;
    float maxScroll = scrollMax_[p];
    if (maxScroll <= 0.f) return;
    scroll_[p] = Clampf(scroll_[p] + step * 72.f, 0.f, maxScroll);
    Invalidate();
}

void App::Char(wchar_t ch) {
    if (!searchFocused_) return;
    if (ch == L'\r' || ch == L'\n' || ch == L'\t') return;
    if (ch < 32) return;
    if (ch == 127) return;
    query_.push_back(ch);
    caretOn_ = true;
    Invalidate();
}

void App::KeyDown(UINT vk) {
    if (!searchFocused_) return;
    if (vk == VK_BACK) {
        if (!query_.empty()) {
            query_.pop_back();
            Invalidate();
        }
    } else if (vk == VK_ESCAPE) {
        searchFocused_ = false;
        query_.clear();
        Invalidate();
    } else if (vk == VK_RETURN) {
        searchFocused_ = false;
        Invalidate();
    }
}

void App::FocusChanged(bool focused) {
    if (!focused && searchFocused_) {
        searchFocused_ = false;
        Invalidate();
    }
    if (focused) {
        caretOn_ = true;
        Invalidate();
    }
}

void App::OnTimer() {
    ++tick_;
    bool need = false;

    if (tick_ % 30 == 0) {
        caretOn_ = !caretOn_;
        if (searchFocused_) need = true;
    }

    if (tick_ % 60 == 0) {
        RefreshWindows();
        if (cfg_.autoApply) RunAutoApply(false);
        effects_.RefreshCorners();
        need = true;
    }

    if (animating_) {
        animating_ = false;
        need = true;
    }

    if (need) Invalidate();
}

// ============================================================
// 控件
// ============================================================

void App::AddHit(uint64_t id, Kind kind, int arg, const D2D1_RECT_F& rect, bool enabled) {
    Hit h;
    h.id = id;
    h.kind = kind;
    h.arg = arg;
    h.rect = rect;
    h.enabled = enabled;
    hits_.push_back(h);
}

void App::Card(const D2D1_RECT_F& rect) {
    painter_.FillRound(rect, 12.f, cCard);
    painter_.StrokeRound(rect, 12.f, cBorder);
}

void App::DrawButton(uint64_t id, Kind kind, int arg, const D2D1_RECT_F& rect,
                     const wchar_t* label, BtnStyle style, bool enabled, const wchar_t* icon) {
    bool hot = Hot(id) && enabled;
    D2D1_COLOR_F textColor = cText;

    if (style == BtnStyle::Primary) {
        D2D1_COLOR_F from = enabled ? (hot ? Rgb(0x9083FF) : Rgb(0x8A7BFF)) : Rgb(0x2A2A33);
        D2D1_COLOR_F to = enabled ? (hot ? Rgb(0x6E9DFF) : Rgb(0x5F93FF)) : Rgb(0x26262E);
        painter_.FillRoundGrad(rect, 9.f, from, to, false);
        textColor = enabled ? Rgb(0xFFFFFF) : cMuted;
    } else if (style == BtnStyle::Danger) {
        painter_.FillRound(rect, 9.f, hot ? Rgb(0x3A1E22) : Rgb(0x1E1B20));
        painter_.StrokeRound(rect, 9.f, hot ? Rgb(0x7A3A42) : cBorder);
        textColor = enabled ? (hot ? Rgb(0xFF9AA1) : Rgb(0xE9A0A6)) : cMuted;
    } else if (style == BtnStyle::Subtle) {
        painter_.FillRound(rect, 9.f, hot ? cHover : D2D1::ColorF(0, 0, 0, 0));
        painter_.StrokeRound(rect, 9.f, hot ? cBorderHi : cBorder);
        textColor = enabled ? (hot ? cText : cSub) : cMuted;
    } else {
        painter_.FillRound(rect, 9.f, hot ? Rgb(0x23242C) : Rgb(0x1B1C22));
        painter_.StrokeRound(rect, 9.f, hot ? cBorderHi : cBorder);
        textColor = enabled ? (hot ? cText : cSub) : cMuted;
    }

    if (icon) {
        float tw = painter_.MeasureWidth(label, FontRole::Strong);
        float total = 18.f + 7.f + tw;
        float sx = rect.left + ((rect.right - rect.left) - total) * 0.5f;
        painter_.Text(RectF(sx, rect.top, sx + 18.f, rect.bottom), icon, FontRole::Icon,
                      textColor, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      false);
        painter_.Text(RectF(sx + 25.f, rect.top, rect.right, rect.bottom), label,
                      FontRole::Strong, textColor, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    } else {
        painter_.Text(rect, label, FontRole::Strong, textColor,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }

    AddHit(id, kind, arg, rect, enabled);
}

void App::DrawIconButton(uint64_t id, Kind kind, const D2D1_RECT_F& rect,
                         const wchar_t* glyph) {
    bool hot = Hot(id);
    painter_.FillRound(rect, 9.f, hot ? cHover : Rgb(0x1A1B21));
    painter_.StrokeRound(rect, 9.f, hot ? cBorderHi : cBorder);
    painter_.Text(rect, glyph, FontRole::Icon, hot ? cText : cSub,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    AddHit(id, kind, 0, rect);
}

void App::DrawToggle(uint64_t id, Kind kind, const D2D1_RECT_F& rect, bool on, bool enabled) {
    float t = Anim(id, on && enabled);
    float cy = (rect.top + rect.bottom) * 0.5f;
    float radius = (rect.bottom - rect.top) * 0.5f;
    float knobR = radius - 3.5f;

    D2D1_COLOR_F track = MixColor(Rgb(0x2E3039), cAccent, t);
    if (!enabled) track = Rgb(0x23242B);
    painter_.FillRound(rect, radius, track);

    float x0 = rect.left + 3.5f + knobR;
    float x1 = rect.right - 3.5f - knobR;
    float cx = x0 + (x1 - x0) * t;
    painter_.FillCircle(cx, cy, knobR, enabled ? Rgb(0xFFFFFF) : Rgb(0x6A6B75));

    AddHit(id, kind, 0, rect, enabled);
}

void App::DrawSegmented(uint64_t id, Kind kind, const D2D1_RECT_F& rect,
                        const wchar_t* const* labels, int count, int selected, bool enabled) {
    painter_.FillRound(rect, 10.f, cCardDeep);
    painter_.StrokeRound(rect, 10.f, cBorder);

    float cellW = (rect.right - rect.left) / (float)count;
    for (int i = 0; i < count; ++i) {
        uint64_t cid = id ^ ((uint64_t)(i + 1) * 0x9E3779B97F4A7C15ull);
        D2D1_RECT_F cell = RectF(rect.left + cellW * i, rect.top, rect.left + cellW * (i + 1),
                                 rect.bottom);
        D2D1_RECT_F inner = ShrinkRect(cell, 3.f, 3.f);
        bool hot = Hot(cid) && enabled;

        if (i == selected) {
            painter_.FillRound(inner, 7.f, WithAlpha(cAccent, enabled ? 0.20f : 0.10f));
            painter_.StrokeRound(inner, 7.f, WithAlpha(cAccent, enabled ? 0.55f : 0.22f));
        } else if (hot) {
            painter_.FillRound(inner, 7.f, Rgb(0x1C1D24));
        }

        D2D1_COLOR_F color = (i == selected) ? (enabled ? cText : cMuted)
                                             : (hot ? cText : cSub);
        painter_.Text(cell, labels[i], FontRole::Body, color, DWRITE_TEXT_ALIGNMENT_CENTER,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

        AddHit(cid, kind, i, cell, enabled);
    }
}

void App::DrawSlider(uint64_t id, Kind kind, const D2D1_RECT_F& rect, int value, int minValue,
                     int maxValue, bool enabled) {
    float cy = (rect.top + rect.bottom) * 0.5f;
    float t = (maxValue > minValue) ? (float)(value - minValue) / (float)(maxValue - minValue) : 0.f;
    t = Clampf(t, 0.f, 1.f);
    float x = rect.left + (rect.right - rect.left) * t;

    painter_.FillRound(RectF(rect.left, cy - 2.f, rect.right, cy + 2.f), 2.f, Rgb(0x2B2C35));
    painter_.FillRound(RectF(rect.left, cy - 2.f, x, cy + 2.f), 2.f,
                       enabled ? cAccent : Rgb(0x3C3D47));

    bool hot = Hot(id) && enabled;
    float knobR = (hot || active_ == id) ? 7.5f : 7.f;
    painter_.FillCircle(x, cy, knobR, enabled ? Rgb(0xFFFFFF) : Rgb(0x6A6B75));
    painter_.FillCircle(x, cy, 3.2f, enabled ? cAccent : Rgb(0x3C3D47));

    AddHit(id, kind, 0, ShrinkRect(rect, 0.f, -7.f), enabled);
}

// ============================================================
// 框架绘制
// ============================================================

void App::DrawChrome(float w, float h) {
    painter_.Clear(cWindow);
    painter_.FillGrad(RectF(0, 0, w, h), Rgb(0x121318), Rgb(0x0C0D10), true);
    painter_.Glow(w * 0.66f, -90.f, 640.f, WithAlpha(cAccent, 0.10f));

    // 侧边栏
    painter_.Fill(RectF(0, kTitleBarHeight, kSidebarWidth, h - kStatusBarHeight), cSidebar);
    painter_.Line(kSidebarWidth, kTitleBarHeight, kSidebarWidth, h - kStatusBarHeight, cBorder,
                  1.f);

    // 标题栏
    painter_.Line(0, kTitleBarHeight, w, kTitleBarHeight, cBorder, 1.f);

    // 品牌标识
    D2D1_RECT_F logo = RectF(18.f, 13.f, 44.f, 39.f);
    painter_.FillRoundGrad(logo, 8.f, Rgb(0x8E7DFF), Rgb(0x4F8CFF), true);
    painter_.FillRound(RectF(logo.left + 7.f, logo.top + 9.f, logo.right - 7.f, logo.top + 12.5f),
                       1.8f, Rgb(0xFFFFFF, 0.95f));
    painter_.FillRound(RectF(logo.left + 7.f, logo.top + 16.f, logo.right - 11.f, logo.top + 19.5f),
                       1.8f, Rgb(0xFFFFFF, 0.55f));
    painter_.Text(RectF(56.f, 0.f, 320.f, kTitleBarHeight), L"BeterWindow", FontRole::Brand,
                  cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    // 窗口按钮
    const float bw = 46.f;
    const float bx = w - 8.f - bw * 3.f;
    struct WinBtn {
        Kind kind;
        const wchar_t* glyph;
        uint64_t id;
    };
    WinBtn buttons[3] = {
        {Kind::TitleMin, L"\uE921", H("title.min")},
        {Kind::TitleMax, IsZoomed(hwnd_) ? L"\uE923" : L"\uE922", H("title.max")},
        {Kind::TitleClose, L"\uE8BB", H("title.close")},
    };
    for (int i = 0; i < 3; ++i) {
        D2D1_RECT_F br = RectF(bx + bw * i, 10.f, bx + bw * (i + 1), 42.f);
        bool hot = Hot(buttons[i].id);
        bool isClose = buttons[i].kind == Kind::TitleClose;
        D2D1_RECT_F inner = ShrinkRect(br, 4.f, 0.f);
        if (hot) {
            painter_.FillRound(inner, 7.f, isClose ? Rgb(0xE81123) : Rgb(0x22232B));
        }
        painter_.Text(br, buttons[i].glyph, FontRole::Icon,
                      hot && isClose ? Rgb(0xFFFFFF) : (hot ? cText : cSub),
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        AddHit(buttons[i].id, buttons[i].kind, 0, br);
    }

    // 导航
    struct NavItem {
        const wchar_t* label;
        const wchar_t* glyph;
        Page page;
        Kind kind;
        uint64_t id;
    };
    NavItem nav[] = {
        {L"窗口", L"\uE7C4", Page::Windows, Kind::NavWindow, H("nav.window")},
        {L"外观效果", L"\uE790", Page::Effects, Kind::NavEffects, H("nav.effects")},
        {L"生效规则", L"\uE71C", Page::Rules, Kind::NavRules, H("nav.rules")},
        {L"关于", L"\uE946", Page::About, Kind::NavAbout, H("nav.about")},
    };

    float ny = kTitleBarHeight + 14.f;
    for (auto& item : nav) {
        D2D1_RECT_F r = RectF(12.f, ny, kSidebarWidth - 12.f, ny + 38.f);
        bool selected = page_ == item.page;
        bool hot = Hot(item.id);
        if (selected) {
            painter_.FillRound(r, 9.f, Rgb(0x22232B));
            painter_.FillRound(RectF(r.left + 1.f, r.top + 11.f, r.left + 4.f, r.bottom - 11.f),
                               1.5f, cAccent);
        } else if (hot) {
            painter_.FillRound(r, 9.f, Rgb(0x1A1B21));
        }

        D2D1_COLOR_F iconColor = selected ? cAccent : (hot ? cText : cSub);
        D2D1_COLOR_F textColor = selected ? cText : (hot ? cText : cSub);
        painter_.Text(RectF(r.left + 14.f, r.top, r.left + 36.f, r.bottom), item.glyph,
                      FontRole::Icon, iconColor, DWRITE_TEXT_ALIGNMENT_CENTER,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        painter_.Text(RectF(r.left + 40.f, r.top, r.right - 8.f, r.bottom), item.label,
                      selected ? FontRole::Strong : FontRole::Body, textColor,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        AddHit(item.id, item.kind, 0, r);
        ny += 42.f;
    }

    painter_.Text(RectF(24.f, h - kStatusBarHeight - 34.f, kSidebarWidth - 16.f,
                        h - kStatusBarHeight - 14.f),
                  L"v1.0.0", FontRole::Tiny, cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void App::DrawStatusBar(float w, float h) {
    float top = h - kStatusBarHeight;
    painter_.Fill(RectF(0, top, w, h), Rgb(0x0A0B0E));
    painter_.Line(0, top, w, top, cBorder, 1.f);

    painter_.FillCircle(18.f, top + kStatusBarHeight * 0.5f, 3.f,
                        effects_.AppliedCount() > 0 ? cOk : cMuted);
    painter_.Text(RectF(28.f, top, w * 0.62f, h), status_, FontRole::Small, cSub,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);

    std::wstring right = WFormat(L"%s · Build %u · 已修改 %d 个窗口",
                                 IsWin11OrLater() ? L"Windows 11" : L"Windows 10",
                                 WindowsBuildNumber(), (int)effects_.AppliedCount());
    painter_.Text(RectF(w - 460.f, top, w - 16.f, h), right, FontRole::Small, cMuted,
                  DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
}

void App::DrawPageHeader(const D2D1_RECT_F& area, const wchar_t* title,
                         const std::wstring& subtitle) {
    painter_.Text(RectF(area.left + 30.f, area.top + 18.f, area.right - 30.f, area.top + 50.f),
                  title, FontRole::H1, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    painter_.Text(RectF(area.left + 30.f, area.top + 50.f, area.right - 30.f, area.top + 70.f),
                  subtitle, FontRole::Small, cSub, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
}

// ============================================================
// 窗口页
// ============================================================

void App::DrawWindowPage(const D2D1_RECT_F& area) {
    const float left = area.left + 30.f;
    const float right = area.right - 30.f;

    DrawPageHeader(area, L"窗口", WFormat(L"共 %d 个可见窗口 · 选中后可单独应用效果",
                                          (int)windows_.size()));

    D2D1_RECT_F refresh = RectF(right - 34.f, area.top + 18.f, right, area.top + 52.f);
    DrawIconButton(H("btn.refresh"), Kind::Refresh, refresh, L"\uE72C");

    // 搜索框
    D2D1_RECT_F search = RectF(left, area.top + 84.f, right, area.top + 120.f);
    bool focused = searchFocused_;
    painter_.FillRound(search, 10.f, focused ? Rgb(0x191A21) : Rgb(0x14151A));
    painter_.StrokeRound(search, 10.f, focused ? WithAlpha(cAccent, 0.65f) : cBorder);
    painter_.Text(RectF(search.left + 13.f, search.top, search.left + 35.f, search.bottom),
                  L"\uE721", FontRole::Icon, focused ? cSub : cMuted,
                  DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);

    float textLeft = search.left + 41.f;
    if (query_.empty()) {
        painter_.Text(RectF(textLeft, search.top, search.right - 14.f, search.bottom),
                      L"搜索窗口标题或进程名", FontRole::Body, cMuted,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        if (focused && caretOn_) {
            float cy = (search.top + search.bottom) * 0.5f;
            painter_.Fill(RectF(textLeft, cy - 8.f, textLeft + 1.4f, cy + 8.f), cAccent);
        }
    } else {
        painter_.Text(RectF(textLeft, search.top, search.right - 14.f, search.bottom), query_,
                      FontRole::Body, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        if (focused && caretOn_) {
            float tw = painter_.MeasureWidth(query_, FontRole::Body);
            float cx = textLeft + (std::min)(tw, search.right - 20.f - textLeft);
            float cy = (search.top + search.bottom) * 0.5f;
            painter_.Fill(RectF(cx + 1.f, cy - 8.f, cx + 2.4f, cy + 8.f), cAccent);
        }
    }
    AddHit(H("search"), Kind::SearchField, 0, search);

    // 列表与操作栏
    D2D1_RECT_F list = RectF(left, area.top + 132.f, right, area.bottom - 70.f);
    DrawWindowList(list);

    D2D1_RECT_F bar = RectF(left, area.bottom - 58.f, right, area.bottom - 14.f);
    const WindowInfo* info = SelectedInfo();
    if (info) {
        painter_.Text(RectF(bar.left, bar.top, bar.right - 280.f, bar.top + 24.f), info->title,
                      FontRole::Strong, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        painter_.Text(RectF(bar.left, bar.top + 22.f, bar.right - 280.f, bar.bottom),
                      info->exePath, FontRole::Tiny, cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    } else {
        painter_.Text(bar, L"尚未选择窗口", FontRole::Body, cMuted,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    }

    D2D1_RECT_F applyBtn = RectF(bar.right - 122.f, bar.top + 2.f, bar.right, bar.top + 40.f);
    D2D1_RECT_F restoreBtn =
        RectF(applyBtn.left - 110.f, bar.top + 2.f, applyBtn.left - 10.f, bar.top + 40.f);

    DrawButton(H("btn.applySel"), Kind::ApplySelected, 0, applyBtn, L"应用效果",
               BtnStyle::Primary, info != nullptr);
    DrawButton(H("btn.restoreSel"), Kind::RestoreSelected, 0, restoreBtn, L"恢复原状",
               BtnStyle::Secondary, info != nullptr && effects_.IsApplied(info->hwnd));
}

void App::DrawWindowList(const D2D1_RECT_F& list) {
    listRect_ = list;

    visibleIdx_.clear();
    visibleHwnds_.clear();
    std::wstring q = ToLowerW(query_);
    for (size_t i = 0; i < windows_.size(); ++i) {
        const auto& wi = windows_[i];
        if (!q.empty()) {
            if (ToLowerW(wi.title).find(q) == std::wstring::npos &&
                ToLowerW(wi.exeName).find(q) == std::wstring::npos) {
                continue;
            }
        }
        visibleIdx_.push_back(i);
        visibleHwnds_.push_back(wi.hwnd);
    }

    painter_.FillRound(list, 12.f, cCardDeep);
    painter_.StrokeRound(list, 12.f, cBorder);

    const float rowH = 58.f;
    float contentH = (float)visibleIdx_.size() * rowH + 8.f;
    float viewH = list.bottom - list.top;
    listScrollMax_ = (std::max)(0.f, contentH - viewH);
    listScroll_ = Clampf(listScroll_, 0.f, listScrollMax_);

    painter_.PushClip(ShrinkRect(list, 1.f, 1.f));

    for (size_t i = 0; i < visibleIdx_.size(); ++i) {
        float y = list.top + 4.f + (float)i * rowH - listScroll_;
        if (y + rowH < list.top - 2.f || y > list.bottom + 2.f) continue;

        const WindowInfo& wi = windows_[visibleIdx_[i]];
        D2D1_RECT_F row = RectF(list.left + 6.f, y, list.right - 6.f, y + rowH - 4.f);
        uint64_t rid = RowId(wi.hwnd);
        bool selected = wi.hwnd == selected_;
        bool hot = Hot(rid);

        if (selected) {
            painter_.FillRound(row, 9.f, WithAlpha(cAccent, 0.15f));
            painter_.StrokeRound(row, 9.f, WithAlpha(cAccent, 0.55f));
        } else if (hot) {
            painter_.FillRound(row, 9.f, Rgb(0x1B1C23));
        }

        D2D1_RECT_F iconRect = RectF(row.left + 13.f, y + 13.f, row.left + 43.f, y + 43.f);
        ID2D1Bitmap* bitmap = icons_.Get(painter_.rt(), wi.exePath);
        if (bitmap) {
            painter_.Image(bitmap, iconRect);
        } else {
            painter_.FillRound(iconRect, 7.f, Rgb(0x262832));
            std::wstring letter =
                wi.exeName.empty() ? L"?" : wi.exeName.substr(0, 1);
            painter_.Text(iconRect, letter, FontRole::Strong, cSub,
                          DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }

        bool applied = effects_.IsApplied(wi.hwnd);
        float textRight = row.right - (applied ? 84.f : 16.f);
        painter_.Text(RectF(row.left + 52.f, y + 7.f, textRight, y + 30.f), wi.title,
                      FontRole::Body, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        painter_.Text(RectF(row.left + 52.f, y + 30.f, textRight, y + 50.f),
                      WFormat(L"%s · PID %u", wi.exeName.c_str(), (unsigned)wi.pid),
                      FontRole::Small, cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);

        if (applied) {
            D2D1_RECT_F badge = RectF(row.right - 70.f, y + 16.f, row.right - 16.f, y + 36.f);
            painter_.FillRound(badge, 10.f, WithAlpha(cOk, 0.14f));
            painter_.StrokeRound(badge, 10.f, WithAlpha(cOk, 0.35f));
            painter_.Text(badge, L"已应用", FontRole::Tiny, cOk,
                          DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        }

        AddHit(rid, Kind::WindowRow, (int)i, row);
    }

    if (visibleIdx_.empty()) {
        const wchar_t* msg = windows_.empty() ? L"没有可操作的可见窗口" : L"没有匹配的窗口";
        painter_.Text(RectF(list.left, list.top, list.right, list.bottom - 20.f), msg,
                      FontRole::Body, cMuted, DWRITE_TEXT_ALIGNMENT_CENTER,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    }

    painter_.PopClip();

    if (listScrollMax_ > 0.f) {
        float trackTop = list.top + 14.f;
        float trackBottom = list.bottom - 14.f;
        float trackH = trackBottom - trackTop;
        float thumbH = (std::max)(30.f, trackH * viewH / (contentH + 0.001f));
        float ratio = listScroll_ / listScrollMax_;
        float y = trackTop + (trackH - thumbH) * ratio;
        painter_.FillRound(RectF(list.right - 9.f, y, list.right - 5.f, y + thumbH), 2.f,
                           Rgb(0xFFFFFF, 0.16f));
    }
}

// ============================================================
// 效果页
// ============================================================

void App::DrawEffectsPage(const D2D1_RECT_F& area) {
    DrawPageHeader(area, L"外观效果", L"调整参数后，应用到选中窗口或按规则批量生效");

    D2D1_RECT_F body = RectF(area.left, area.top + 76.f, area.right, area.bottom - 66.f);
    int p = (int)Page::Effects;
    float scroll = Clampf(scroll_[p], 0.f, scrollMax_[p]);

    const float x = area.left + 30.f;
    const float right = area.right - 30.f;
    float y = body.top - scroll + 2.f;

    painter_.PushClip(body);

    // --- 透明度 ---
    D2D1_RECT_F card1 = RectF(x, y, right, y + 134.f);
    Card(card1);
    painter_.Text(RectF(x + 18.f, y + 12.f, right - 82.f, y + 36.f), L"窗口透明度",
                  FontRole::H2, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    painter_.Text(RectF(x + 18.f, y + 36.f, right - 82.f, y + 56.f),
                  L"整体降低不透明度；与磨砂同时开启时磨砂会被覆盖", FontRole::Small, cMuted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    DrawToggle(H("tg.transparency"), Kind::ToggleTransparency,
               RectF(right - 60.f, y + 16.f, right - 18.f, y + 40.f), cfg_.effects.transparency);

    float valueRight = right - 18.f;
    painter_.Text(RectF(x + 18.f, y + 66.f, x + 90.f, y + 92.f), L"不透明度", FontRole::Small,
                  cfg_.effects.transparency ? cSub : cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    opacitySlider_ = RectF(x + 96.f, y + 68.f, valueRight - 56.f, y + 90.f);
    DrawSlider(H("sl.opacity"), Kind::SliderOpacity, opacitySlider_, cfg_.effects.opacity, 20, 100,
               cfg_.effects.transparency);
    painter_.Text(RectF(valueRight - 48.f, y + 68.f, valueRight, y + 90.f),
                  WFormat(L"%d%%", cfg_.effects.opacity), FontRole::Mono,
                  cfg_.effects.transparency ? cText : cMuted, DWRITE_TEXT_ALIGNMENT_TRAILING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    y += 148.f;

    // --- 圆角 ---
    D2D1_RECT_F card2 = RectF(x, y, right, y + 92.f);
    Card(card2);
    painter_.Text(RectF(x + 18.f, y + 14.f, right - 18.f, y + 38.f), L"窗口圆角", FontRole::H2,
                  cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  false);
    painter_.Text(RectF(x + 18.f, y + 38.f, right - 374.f, y + 60.f),
                  L"系统默认不修改；无效果为直角；圆角支持 Windows 10 / 11",
                  FontRole::Small, cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    DrawSegmented(H("seg.corner"), Kind::SegCorner,
                  RectF(right - 360.f, y + 29.f, right - 18.f, y + 63.f), kCornerLabels, 4,
                  (int)cfg_.effects.corner + 1);
    y += 106.f;

    // --- 磨砂 ---
    int glassSel = (int)cfg_.effects.glass;
    float card3H = (glassSel == (int)GlassMode::Acrylic) ? 186.f : 122.f;
    D2D1_RECT_F card3 = RectF(x, y, right, y + card3H);
    Card(card3);
    painter_.Text(RectF(x + 18.f, y + 14.f, right - 18.f, y + 38.f), L"磨砂玻璃", FontRole::H2,
                  cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  false);
    painter_.Text(RectF(x + 18.f, y + 38.f, right - 18.f, y + 58.f),
                  L"亚克力会实时模糊窗口背后的内容，Mica 需要 Windows 11", FontRole::Small,
                  cMuted, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  true);
    DrawSegmented(H("seg.glass"), Kind::SegGlass, RectF(x + 18.f, y + 64.f, right - 18.f, y + 100.f),
                  kGlassLabels, 4, glassSel);

    if (glassSel == (int)GlassMode::Acrylic) {
        painter_.Text(RectF(x + 18.f, y + 106.f, x + 60.f, y + 128.f), L"色调", FontRole::Small,
                      cSub, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      false);
        float cx = x + 74.f;
        float cy = y + 117.f;
        for (int i = 0; i < kTintCount; ++i) {
            uint64_t sid = H("tint") ^ ((uint64_t)(i + 1) * 7919ull);
            bool sel = i == cfg_.effects.tintIndex;
            painter_.FillCircle(cx, cy, 10.f, Rgb(kTints[i].rgb));
            painter_.StrokeCircle(cx, cy, 10.f, Rgb(0xFFFFFF, 0.20f), 1.f);
            if (sel) painter_.StrokeCircle(cx, cy, 13.f, cAccent, 2.f);
            AddHit(sid, Kind::TintSwatch, i, RectF(cx - 14.f, cy - 14.f, cx + 14.f, cy + 14.f));
            cx += 31.f;
        }

        painter_.Text(RectF(x + 18.f, y + 140.f, x + 60.f, y + 166.f), L"浓度", FontRole::Small,
                      cSub, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      false);
        tintSlider_ = RectF(x + 60.f, y + 146.f, right - 18.f - 56.f, y + 170.f);
        DrawSlider(H("sl.tint"), Kind::SliderTint, tintSlider_, cfg_.effects.tintAlpha, 0, 100,
                   true);
        painter_.Text(RectF(right - 18.f - 48.f, y + 146.f, right - 18.f, y + 170.f),
                      WFormat(L"%d%%", cfg_.effects.tintAlpha), FontRole::Mono, cText,
                      DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    } else {
        painter_.Text(RectF(x + 18.f, y + 106.f, right - 18.f, y + 126.f),
                      L"选择「亚克力」后可调整色调与浓度", FontRole::Tiny, cMuted,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    }
    y += card3H + 14.f;

    // 底部动作（固定显示）
    painter_.PopClip();
    scrollMax_[p] = (std::max)(0.f, y + 8.f - body.bottom);

    bool hasSelection = SelectedInfo() != nullptr;
    float fy = area.bottom - 54.f;
    DrawButton(H("btn.applySel2"), Kind::ApplySelected, 0,
               RectF(x, fy, x + 186.f, fy + 42.f), L"应用到选中窗口", BtnStyle::Primary,
               hasSelection, L"\uE768");
    DrawButton(H("btn.applyAll2"), Kind::ApplyAll, 0,
               RectF(x + 198.f, fy, x + 398.f, fy + 42.f), L"应用到全部匹配窗口",
               BtnStyle::Secondary, true);
}

// ============================================================
// 规则页
// ============================================================

void App::DrawRulesPage(const D2D1_RECT_F& area) {
    DrawPageHeader(area, L"生效规则", L"全局模式覆盖所有窗口，也可以用排除与指定列表精确控制");

    D2D1_RECT_F body = RectF(area.left, area.top + 76.f, area.right, area.bottom - 66.f);
    int p = (int)Page::Rules;
    float scroll = Clampf(scroll_[p], 0.f, scrollMax_[p]);

    const float x = area.left + 30.f;
    const float right = area.right - 30.f;
    float y = body.top - scroll + 2.f;

    painter_.PushClip(body);

    // 应用模式
    D2D1_RECT_F cardA = RectF(x, y, right, y + 178.f);
    Card(cardA);
    painter_.Text(RectF(x + 18.f, y + 14.f, right - 18.f, y + 38.f), L"应用模式", FontRole::H2,
                  cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  false);
    painter_.Text(RectF(x + 18.f, y + 38.f, right - 18.f, y + 58.f),
                  L"选择自动生效的窗口范围", FontRole::Small, cMuted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    DrawSegmented(H("seg.scope"), Kind::SegScope, RectF(x + 18.f, y + 64.f, x + 318.f, y + 100.f),
                  kScopeLabels, 2, cfg_.scopeMode);
    painter_.Line(x + 18.f, y + 116.f, right - 18.f, y + 116.f, cBorder, 1.f);

    painter_.Text(RectF(x + 18.f, y + 124.f, right - 82.f, y + 146.f), L"自动应用", FontRole::Strong,
                  cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  false);
    painter_.Text(RectF(x + 18.f, y + 144.f, right - 82.f, y + 164.f),
                  L"规则变化或新窗口出现时自动生效", FontRole::Small, cMuted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    DrawToggle(H("tg.auto"), Kind::ToggleAutoApply,
               RectF(right - 60.f, y + 134.f, right - 18.f, y + 158.f), cfg_.autoApply);
    y += 192.f;

    // 排除列表
    size_t exCount = cfg_.exclude.size();
    float exH = 70.f + (std::max)(1.0f, (float)exCount) * 46.f + (exCount == 0 ? 18.f : 4.f);
    D2D1_RECT_F cardB = RectF(x, y, right, y + exH);
    Card(cardB);
    painter_.Text(RectF(x + 18.f, y + 16.f, x + 140.f, y + 40.f), L"排除列表", FontRole::H2, cText,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    painter_.Text(RectF(x + 18.f + painter_.MeasureWidth(L"排除列表", FontRole::H2) + 8.f, y + 16.f,
                        x + 220.f, y + 40.f),
                  WFormat(L"%d 项", (int)exCount), FontRole::Tiny, cMuted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    painter_.Text(RectF(x + 18.f, y + 40.f, right - 160.f, y + 58.f),
                  L"这些程序永远不会被修改", FontRole::Tiny, cMuted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    DrawButton(H("btn.addExclude"), Kind::AddExclude, 0,
               RectF(right - 148.f, y + 18.f, right - 18.f, y + 50.f), L"添加选中窗口",
               BtnStyle::Secondary, SelectedInfo() != nullptr, L"\uE710");

    float ey = y + 62.f;
    if (exCount == 0) {
        painter_.Text(RectF(x + 18.f, ey, right - 18.f, ey + 24.f),
                      L"暂无排除项 · 在「窗口」页选中程序后添加", FontRole::Small, cMuted,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        ey += 30.f;
    } else {
        for (size_t i = 0; i < cfg_.exclude.size(); ++i) {
            const std::wstring& path = cfg_.exclude[i];
            D2D1_RECT_F row = RectF(x + 10.f, ey, right - 10.f, ey + 44.f);
            if (Hot(H("exrow") ^ ((uint64_t)(i + 1) * 2654435761ull))) {
                painter_.FillRound(row, 8.f, Rgb(0x1C1D24));
            }
            D2D1_RECT_F iconRect = RectF(row.left + 10.f, ey + 11.f, row.left + 32.f, ey + 33.f);
            ID2D1Bitmap* bmp = icons_.Get(painter_.rt(), path);
            if (bmp) {
                painter_.Image(bmp, iconRect);
            } else {
                painter_.FillRound(iconRect, 5.f, Rgb(0x262832));
            }
            painter_.Text(RectF(row.left + 40.f, ey + 4.f, row.right - 52.f, ey + 24.f),
                          FileNameOf(path), FontRole::Body, cText,
                          DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
            painter_.Text(RectF(row.left + 40.f, ey + 22.f, row.right - 52.f, ey + 40.f), path,
                          FontRole::Tiny, cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                          DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);

            uint64_t rid = H("exdel") ^ ((uint64_t)(i + 1) * 40503ull);
            D2D1_RECT_F del = RectF(row.right - 38.f, ey + 8.f, row.right - 10.f, ey + 36.f);
            bool hot = Hot(rid);
            if (hot) painter_.FillRound(del, 7.f, WithAlpha(cDanger, 0.16f));
            painter_.Text(del, L"\uE711", FontRole::Icon, hot ? cDanger : cMuted,
                          DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
            AddHit(rid, Kind::RemoveExclude, (int)i, del);

            ey += 46.f;
        }
    }
    y += exH + 14.f;

    // 指定列表
    if (cfg_.scopeMode == 1) {
        size_t inCount = cfg_.include.size();
        float inH = 70.f + (std::max)(1.0f, (float)inCount) * 46.f + (inCount == 0 ? 18.f : 4.f);
        D2D1_RECT_F cardC = RectF(x, y, right, y + inH);
        Card(cardC);
        painter_.Text(RectF(x + 18.f, y + 16.f, x + 140.f, y + 40.f), L"指定列表", FontRole::H2,
                      cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                      false);
        painter_.Text(RectF(x + 18.f + painter_.MeasureWidth(L"指定列表", FontRole::H2) + 8.f,
                            y + 16.f, x + 220.f, y + 40.f),
                      WFormat(L"%d 项", (int)inCount), FontRole::Tiny, cMuted,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        painter_.Text(RectF(x + 18.f, y + 40.f, right - 160.f, y + 58.f),
                      L"只有这些程序会应用效果", FontRole::Tiny, cMuted,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        DrawButton(H("btn.addInclude"), Kind::AddInclude, 0,
                   RectF(right - 148.f, y + 18.f, right - 18.f, y + 50.f), L"添加选中窗口",
                   BtnStyle::Secondary, SelectedInfo() != nullptr, L"\uE710");

        float iy = y + 62.f;
        if (inCount == 0) {
            painter_.Text(RectF(x + 18.f, iy, right - 18.f, iy + 24.f),
                          L"暂无指定项 · 添加后仅这些程序自动生效", FontRole::Small, cMuted,
                          DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                          true);
            iy += 30.f;
        } else {
            for (size_t i = 0; i < cfg_.include.size(); ++i) {
                const std::wstring& path = cfg_.include[i];
                D2D1_RECT_F row = RectF(x + 10.f, iy, right - 10.f, iy + 44.f);
                if (Hot(H("inrow") ^ ((uint64_t)(i + 1) * 2654435761ull))) {
                    painter_.FillRound(row, 8.f, Rgb(0x1C1D24));
                }
                D2D1_RECT_F iconRect = RectF(row.left + 10.f, iy + 11.f, row.left + 32.f, iy + 33.f);
                ID2D1Bitmap* bmp = icons_.Get(painter_.rt(), path);
                if (bmp) {
                    painter_.Image(bmp, iconRect);
                } else {
                    painter_.FillRound(iconRect, 5.f, Rgb(0x262832));
                }
                painter_.Text(RectF(row.left + 40.f, iy + 4.f, row.right - 52.f, iy + 24.f),
                              FileNameOf(path), FontRole::Body, cText,
                              DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                              true);
                painter_.Text(RectF(row.left + 40.f, iy + 22.f, row.right - 52.f, iy + 40.f), path,
                              FontRole::Tiny, cMuted, DWRITE_TEXT_ALIGNMENT_LEADING,
                              DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);

                uint64_t rid = H("indel") ^ ((uint64_t)(i + 1) * 40503ull);
                D2D1_RECT_F del = RectF(row.right - 38.f, iy + 8.f, row.right - 10.f, iy + 36.f);
                bool hot = Hot(rid);
                if (hot) painter_.FillRound(del, 7.f, WithAlpha(cDanger, 0.16f));
                painter_.Text(del, L"\uE711", FontRole::Icon, hot ? cDanger : cMuted,
                              DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                              false);
                AddHit(rid, Kind::RemoveInclude, (int)i, del);

                iy += 46.f;
            }
        }
        y += inH + 14.f;
    }

    painter_.PopClip();
    scrollMax_[p] = (std::max)(0.f, y + 8.f - body.bottom);

    float fy = area.bottom - 54.f;
    DrawButton(H("btn.applyAll"), Kind::ApplyAll, 0, RectF(x, fy, x + 186.f, fy + 42.f),
               L"按规则应用全部", BtnStyle::Primary, true, L"\uE768");
    DrawButton(H("btn.restoreAll"), Kind::RestoreAll, 0,
               RectF(x + 198.f, fy, x + 352.f, fy + 42.f), L"恢复全部窗口", BtnStyle::Secondary,
               true);
}

// ============================================================
// 关于页
// ============================================================

void App::DrawAboutPage(const D2D1_RECT_F& area) {
    DrawPageHeader(area, L"关于", L"BeterWindow 是一个原生 C++ 实现的窗口外观工具");

    D2D1_RECT_F body = RectF(area.left, area.top + 76.f, area.right, area.bottom);
    int p = (int)Page::About;
    float scroll = Clampf(scroll_[p], 0.f, scrollMax_[p]);

    const float x = area.left + 30.f;
    const float right = area.right - 30.f;
    float y = body.top - scroll + 2.f;

    painter_.PushClip(body);

    D2D1_RECT_F card1 = RectF(x, y, right, y + 128.f);
    Card(card1);
    D2D1_RECT_F logo = RectF(x + 22.f, y + 22.f, x + 66.f, y + 66.f);
    painter_.FillRoundGrad(logo, 12.f, Rgb(0x8E7DFF), Rgb(0x4F8CFF), true);
    painter_.FillRound(RectF(logo.left + 11.f, logo.top + 14.f, logo.right - 11.f, logo.top + 20.f),
                       2.4f, Rgb(0xFFFFFF, 0.95f));
    painter_.FillRound(RectF(logo.left + 11.f, logo.top + 25.f, logo.right - 17.f, logo.top + 31.f),
                       2.4f, Rgb(0xFFFFFF, 0.55f));

    painter_.Text(RectF(x + 80.f, y + 18.f, right - 20.f, y + 44.f), L"BeterWindow",
                  FontRole::H2, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    painter_.Text(RectF(x + 80.f, y + 42.f, right - 20.f, y + 62.f),
                  L"版本 1.0.0 · C++ / Direct2D 原生界面", FontRole::Small, cSub,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    painter_.Text(RectF(x + 22.f, y + 78.f, right - 22.f, y + 98.f),
                  L"为任意桌面窗口添加透明、圆角与磨砂玻璃效果，", FontRole::Body,
                  cSub, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    painter_.Text(RectF(x + 22.f, y + 98.f, right - 22.f, y + 118.f),
                  L"支持排除规则与单窗口独立设置。", FontRole::Body, cSub,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    y += 142.f;

    // 功能列表
    struct Feature {
        const wchar_t* glyph;
        const wchar_t* name;
        const wchar_t* value;
    };
    Feature features[] = {
        {L"\uE8A9", L"窗口透明", L"20% - 100% 整体不透明度"},
        {L"\uE7C4", L"窗口圆角", L"系统默认 · 直角 · 4 / 8 DIP 圆角"},
        {L"\uE790", L"磨砂玻璃", L"模糊 · 亚克力 · Mica"},
    };
    D2D1_RECT_F card2 = RectF(x, y, right, y + 56.f + 3.f * 36.f);
    Card(card2);
    painter_.Text(RectF(x + 18.f, y + 12.f, right - 18.f, y + 38.f), L"支持的窗口效果",
                  FontRole::H2, cText, DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
    float fy = y + 44.f;
    for (auto& f : features) {
        painter_.Text(RectF(x + 20.f, fy, x + 48.f, fy + 34.f), f.glyph, FontRole::Icon, cAccent,
                      DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, false);
        painter_.Text(RectF(x + 52.f, fy, x + 220.f, fy + 34.f), f.name, FontRole::Body, cText,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        painter_.Text(RectF(x + 220.f, fy, right - 20.f, fy + 34.f), f.value, FontRole::Small,
                      cMuted, DWRITE_TEXT_ALIGNMENT_TRAILING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        fy += 36.f;
    }
    y += 56.f + 3.f * 36.f + 14.f;

    // 系统信息
    bool isWin11 = IsWin11OrLater();
    struct SysRow {
        const wchar_t* name;
        std::wstring value;
    };
    SysRow sys[] = {
        {L"系统版本", WFormat(L"Build %u", WindowsBuildNumber())},
        {L"圆角支持", L"Windows 10 / 11（DWM / 窗口区域）"},
        {L"磨砂支持", isWin11 ? L"模糊 · 亚克力 · Mica" : L"模糊 · 亚克力"},
    };
    D2D1_RECT_F card3 = RectF(x, y, right, y + 52.f + 3.f * 34.f);
    Card(card3);
    painter_.Text(RectF(x + 18.f, y + 12.f, right - 18.f, y + 38.f), L"系统信息", FontRole::H2,
                  cText, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  false);
    float sy = y + 44.f;
    for (auto& r : sys) {
        painter_.Text(RectF(x + 20.f, sy, x + 200.f, sy + 32.f), r.name, FontRole::Small, cSub,
                      DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        painter_.Text(RectF(x + 200.f, sy, right - 20.f, sy + 32.f), r.value, FontRole::Small,
                      cMuted, DWRITE_TEXT_ALIGNMENT_TRAILING,
                      DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
        sy += 34.f;
    }
    y += 52.f + 3.f * 34.f + 16.f;

    painter_.Text(RectF(x + 4.f, y, right, y + 22.f),
                  L"退出程序时会自动恢复所有被修改的窗口。", FontRole::Tiny, cMuted,
                  DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER, true);
    y += 30.f;

    painter_.PopClip();
    scrollMax_[p] = (std::max)(0.f, y + 8.f - body.bottom);
}

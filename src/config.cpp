#include "config.h"

namespace {

std::wstring JoinList(const std::vector<std::wstring>& items) {
    std::wstring out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].empty()) continue;
        if (!out.empty()) out += L'|';
        out += items[i];
    }
    return out;
}

void SplitList(const std::wstring& raw, std::vector<std::wstring>& out) {
    out.clear();
    size_t start = 0;
    while (start <= raw.size()) {
        size_t pos = raw.find(L'|', start);
        std::wstring item =
            (pos == std::wstring::npos) ? raw.substr(start) : raw.substr(start, pos - start);
        if (!item.empty()) out.push_back(item);
        if (pos == std::wstring::npos) break;
        start = pos + 1;
    }
}

std::wstring ReadString(const wchar_t* section, const wchar_t* key) {
    std::vector<wchar_t> buf(32768, 0);
    GetPrivateProfileStringW(section, key, L"", buf.data(), (DWORD)buf.size(),
                             ConfigFilePath().c_str());
    return std::wstring(buf.data());
}

void WriteString(const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(section, key, value.c_str(), ConfigFilePath().c_str());
}

void WriteInt(const wchar_t* section, const wchar_t* key, int value) {
    WriteString(section, key, std::to_wstring(value));
}

int ReadInt(const wchar_t* section, const wchar_t* key, int fallback) {
    return (int)GetPrivateProfileIntW(section, key, fallback, ConfigFilePath().c_str());
}

}  // namespace

std::wstring ConfigFilePath() {
    wchar_t appData[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(appData, n) : std::wstring(L".");
    dir += L"\\BeterWindow";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

void LoadConfig(AppConfig& cfg) {
    std::wstring path = ConfigFilePath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    cfg.effects.transparency = ReadInt(L"effects", L"transparency", 0) != 0;
    cfg.effects.opacity = Clampi(ReadInt(L"effects", L"opacity", 92), 20, 100);
    cfg.effects.corner = (CornerMode)Clampi(ReadInt(L"effects", L"corner", -1), -1, 2);
    cfg.effects.glass = (GlassMode)Clampi(ReadInt(L"effects", L"glass", 2), 0, 3);
    cfg.effects.tintIndex = Clampi(ReadInt(L"effects", L"tint", 0), 0, kTintCount - 1);
    cfg.effects.tintAlpha = Clampi(ReadInt(L"effects", L"tintAlpha", 55), 0, 100);

    cfg.scopeMode = Clampi(ReadInt(L"rules", L"scope", 1), 0, 1);
    cfg.autoApply = ReadInt(L"rules", L"autoApply", 1) != 0;
    SplitList(ReadString(L"rules", L"exclude"), cfg.exclude);
    SplitList(ReadString(L"rules", L"include"), cfg.include);

    cfg.winW = Clampi(ReadInt(L"window", L"width", 1120), 980, 4000);
    cfg.winH = Clampi(ReadInt(L"window", L"height", 760), 660, 3000);
}

void SaveConfig(const AppConfig& cfg) {
    WriteInt(L"effects", L"transparency", cfg.effects.transparency ? 1 : 0);
    WriteInt(L"effects", L"opacity", cfg.effects.opacity);
    WriteInt(L"effects", L"corner", (int)cfg.effects.corner);
    WriteInt(L"effects", L"glass", (int)cfg.effects.glass);
    WriteInt(L"effects", L"tint", cfg.effects.tintIndex);
    WriteInt(L"effects", L"tintAlpha", cfg.effects.tintAlpha);

    WriteInt(L"rules", L"scope", cfg.scopeMode);
    WriteInt(L"rules", L"autoApply", cfg.autoApply ? 1 : 0);
    WriteString(L"rules", L"exclude", JoinList(cfg.exclude));
    WriteString(L"rules", L"include", JoinList(cfg.include));

    WriteInt(L"window", L"width", cfg.winW);
    WriteInt(L"window", L"height", cfg.winH);
}

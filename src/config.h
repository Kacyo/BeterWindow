#pragma once

#include "common.h"
#include "effects.h"

struct AppConfig {
    EffectSettings effects;
    int scopeMode = 1;  // 0 = 所有窗口, 1 = 仅指定程序
    bool autoApply = true;
    std::vector<std::wstring> exclude;
    std::vector<std::wstring> include;
    int winW = 1120;
    int winH = 760;
};

std::wstring ConfigFilePath();
void LoadConfig(AppConfig& cfg);
void SaveConfig(const AppConfig& cfg);

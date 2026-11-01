#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct MetricConfig {
    bool enabled = true;
    bool showTemp = true;
    std::wstring title = L"CPU";
    COLORREF titleColor = RGB(56, 189, 248);  // #38BDF8
    COLORREF valueColor = RGB(248, 250, 252); // #F8FAFC
};

struct AppConfig {
    int configVersion = 4;
    int uiScalePct = 100;          // 60% ~ 200% multiplier on top of auto resolution scale
    int notchWidth = 260;          // Base logical width for Dual-Row (Single-Row auto-expands width)
    int notchHeight = 36;          // Base logical height
    int expandedWidth = 420;       // Dynamic island logical expanded width
    int expandedHeight = 188;      // Dynamic island logical expanded height
    int cornerRadius = 16;         // Bottom corner radius
    int layoutMode = 0;            // 0 = Dual Row (2x2), 1 = Single Row (1x4)
    int refreshIntervalMs = 500;   // 250 ~ 2000 ms
    int fontSize = 11;             // 8 ~ 18 pt
    int bgOpacity = 240;           // 100 ~ 255
    COLORREF bgColor = RGB(12, 14, 20);          // #0C0E14
    COLORREF borderColor = RGB(45, 55, 72);      // #2D3748
    COLORREF btnColor = RGB(30, 32, 38);         // #1E2026 (Default monochrome dark button)
    COLORREF btnHoverColor = RGB(244, 244, 246); // #F4F4F6 (Default monochrome white hover/active)
    COLORREF accentColor = RGB(244, 244, 246);   // #F4F4F6

    int tempWarnC = 75;            // Orange warning threshold
    int tempCritC = 85;            // Red critical threshold
    COLORREF warnColor = RGB(251, 146, 60);   // #FB923C
    COLORREF critColor = RGB(248, 113, 113);  // #F87171

    bool clickThroughLocked = false;
    int fpsIdleMode = 0;           // 0 = Show "--", 1 = Show Monitor Hz
    bool autoStartAdmin = false;

    // 0: CPU, 1: GPU, 2: RAM, 3: FPS
    MetricConfig metrics[4];
    int itemOrder[4] = {0, 1, 2, 3};

    AppConfig();
    void ResetDefaults();
    bool LoadFromFile(const std::wstring& path);
    bool SaveToFile(const std::wstring& path) const;

    int GetActiveMetricCount() const;
    float GetCollapsedLogicalWidth() const;
    float GetCollapsedLogicalHeight() const;
    static float GetAutoResolutionScale();
    float GetEffectiveScale() const;
    static std::wstring GetDefaultConfigPath();
};

std::wstring ColorToHex(COLORREF c);
COLORREF HexToColor(const std::wstring& hex, COLORREF fallback);

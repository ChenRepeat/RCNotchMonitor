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
    int configVersion = 5;
    int uiScalePct = 100;          // 60% ~ 220% multiplier on top of auto resolution scale
    int notchWidth = 260;          // Base logical width for Dual-Row (Single-Row auto-expands width)
    int notchHeight = 36;          // Base logical height
    int expandedWidth = 420;       // Dynamic island logical expanded width (without media panel)
    int expandedHeight = 176;      // Dynamic island logical expanded height (2.0 line-height header + symmetric padding)
    int cornerRadius = 24;         // Corner radius (expanded concentric: R_outer - pad = R_card)
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

    // Display Monitor & Position Alignment
    int targetMonitor = 0;                  // 0-based monitor index
    std::wstring targetMonitorDevice = L""; // Device name e.g. \\.\DISPLAY1
    int positionOffsetPct = 50;             // Top horizontal alignment: 0% = Left, 50% = Center (default), 100% = Right

    // Media Monitoring & Docked Block Configuration
    bool mediaEnabled = true;      // Enable GSMTC + CoreAudio Media Block
    int mediaDockSide = 0;         // 0 = Docked Left (靠左), 1 = Docked Right (靠右)
    COLORREF mediaTitleColor = RGB(248, 250, 252); // #F8FAFC (Track title marquee color)
    COLORREF mediaWaveColor = RGB(74, 222, 128);   // #4ADE80 (Audio peak visualizer color)

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

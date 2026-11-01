#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <string>
#include "config.h"
#include "telemetry.h"
#include "fa_icons.h"

class NotchWindow {
public:
    NotchWindow();
    ~NotchWindow();

    bool Create(HINSTANCE hInstance, AppConfig* config, TelemetryEngine* telemetry);
    void Destroy();
    HWND GetHwnd() const { return m_hwnd; }

    void ApplyConfig();
    void SetClickThrough(bool enable);
    void ForceRedraw();
    void SetHoverButtonForPreview(int btnId) { m_hoverButtonId = btnId; }

    // Renders either collapsed or expanded state and saves a 32-bit BMP for verification
    bool SavePreviewBmp(const std::wstring& bmpPath, bool expandedState);

    std::function<void()> onOpenSettings;
    std::function<void()> onConfigChanged;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool InitD2D();
    void CleanupD2D();
    void EnsureCanvas(int reqW, int reqH);
    void FreeCanvas();
    void OnTimerTick();
    void RenderAndPresent();
    void RenderToTarget(ID2D1RenderTarget* rt, float canvasLogW, float drawW, float drawH, float expandRatio, float scale);

    void DrawFaIcon(ID2D1RenderTarget* rt, FaIconType icon, float cx, float cy, float size, ID2D1Brush* brush);
    void DrawNotchShape(ID2D1RenderTarget* rt, float w, float h, float radius, D2D1_COLOR_F fillCol, D2D1_COLOR_F borderCol);
    void DrawCollapsedContent(ID2D1RenderTarget* rt, float w, float h, float alpha);
    void DrawExpandedContent(ID2D1RenderTarget* rt, float w, float h, float alpha);

    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    AppConfig* m_config = nullptr;
    TelemetryEngine* m_telemetry = nullptr;

    ID2D1Factory* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    ID2D1DCRenderTarget* m_dcRenderTarget = nullptr;

    // Persistent offscreen DIB canvas to avoid per-frame allocations & DWM surface thrashing
    HDC m_memDC = NULL;
    HBITMAP m_dibBmp = NULL;
    HBITMAP m_oldBmp = NULL;
    void* m_dibBits = nullptr;
    int m_canvasW = 0;
    int m_canvasH = 0;

    // High-resolution timing for smooth frame-rate-independent animation
    LARGE_INTEGER m_qpcFreq = {};
    LARGE_INTEGER m_lastTick = {};

    // Animation & Interaction state
    bool m_isHovered = false;
    bool m_trackingMouse = false;
    int m_hoverButtonId = 0;     // 0: none, 1: Settings, 2: Layout toggle, 3: Lock click-through
    float m_animProgress = 0.0f; // 0.0 = collapsed, 1.0 = expanded
    float m_layoutAnim = 0.0f;   // 0.0 = Dual-Row (2x2), 1.0 = Single-Row (1x4)
    float m_currW = 260.0f;
    float m_currH = 36.0f;

    // Interactive button rects in Expanded mode (in canvas logical coordinates)
    D2D1_RECT_F m_btnSettingsRect = {};
    D2D1_RECT_F m_btnLayoutRect = {};
    D2D1_RECT_F m_btnLockRect = {};
};

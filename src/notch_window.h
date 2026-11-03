#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <string>
#include "config.h"
#include "telemetry.h"
#include "fa_icons.h"
#include "monitor_utils.h"

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
    void UpdateAlbumArtBitmap(ID2D1RenderTarget* rt, const TelemetrySnapshot& snap);
    void OnTimerTick();
    void RenderAndPresent();
    void RenderToTarget(ID2D1RenderTarget* rt, float canvasLogW, float drawW, float drawH, float expandRatio, float scale);

    void DrawFaIcon(ID2D1RenderTarget* rt, FaIconType icon, float cx, float cy, float size, ID2D1Brush* brush);
    void DrawNotchShape(ID2D1RenderTarget* rt, float w, float h, float radius, D2D1_COLOR_F fillCol, D2D1_COLOR_F borderCol);
    void DrawRoundedAlbumArt(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float radius, float alpha);
    void DrawAudioVisualizer(ID2D1RenderTarget* rt, float x, float cy, float w, float maxH, D2D1_COLOR_F col);
    void DrawCollapsedContent(ID2D1RenderTarget* rt, float w, float h, float alpha);
    void DrawExpandedContent(ID2D1RenderTarget* rt, float w, float h, float alpha);

    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    AppConfig* m_config = nullptr;
    TelemetryEngine* m_telemetry = nullptr;

    ID2D1Factory* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    ID2D1DCRenderTarget* m_dcRenderTarget = nullptr;
    ID2D1Bitmap* m_albumBitmap = nullptr;
    uint32_t m_albumBitmapVer = 0;
    int m_albumBitmapW = 48;
    int m_albumBitmapH = 48;

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
    int m_unhoverStreak = 0;
    // Hover IDs: 0=none, 1=Settings, 3=Lock, 10=MediaSwitch, 11=Prev, 12=Rew(-10s), 13=PlayPause, 14=Ffwd(+10s), 15=Next, 16=SeekBar
    int m_hoverButtonId = 0;
    float m_animProgress = 0.0f;  // 0.0 = collapsed, 1.0 = expanded
    float m_layoutAnim = 0.0f;    // 0.0 = Dual-Row (2x2), 1.0 = Single-Row (1x4)
    float m_mediaAnim = 0.0f;     // 0.0 = Media Hidden, 1.0 = Media Active
    float m_mediaDockAnim = 0.0f; // 0.0 = Docked Left, 1.0 = Docked Right
    float m_marqueeOffset = 0.0f; // Continuous horizontal scroll offset (px)
    float m_marqueePhase = 0.0f;
    float m_audioBars[6] = { 0.18f, 0.28f, 0.22f, 0.16f, 0.24f, 0.18f };
    float m_currW = 260.0f;
    float m_currH = 36.0f;

    // Interactive Dragging on Screen Top Edge
    bool m_isDragging = false;
    int m_dragStartX = 0;
    int m_dragStartY = 0;
    int m_dragStartOffsetPct = 50;

    // Interactive button rects in Expanded mode (in Notch local logical coordinates)
    D2D1_RECT_F m_btnSettingsRect = {};
    D2D1_RECT_F m_btnLockRect = {};
    D2D1_RECT_F m_btnMediaSwitchRect = {};
    D2D1_RECT_F m_btnMediaPrevRect = {};
    D2D1_RECT_F m_btnMediaRewRect = {};
    D2D1_RECT_F m_btnMediaPlayRect = {};
    D2D1_RECT_F m_btnMediaFfwdRect = {};
    D2D1_RECT_F m_btnMediaNextRect = {};
    D2D1_RECT_F m_rectMediaSeekBar = {};
    D2D1_RECT_F m_rectGpuCard = {};
};

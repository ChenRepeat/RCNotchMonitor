#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <functional>
#include <string>
#include <vector>
#include "config.h"
#include "monitor_utils.h"

struct UiHitZone {
    int id;
    D2D1_RECT_F rect;
};

class SettingsWindow {
public:
    SettingsWindow();
    ~SettingsWindow();

    bool Create(HINSTANCE hInstance, AppConfig* config);
    void Show();
    void Hide();
    void RefreshIfVisible();
    HWND GetHwnd() const { return m_hwnd; }

    bool SaveWindowBmp(const std::wstring& bmpPath);

    std::function<void()> onConfigUpdated;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    bool InitD2D();
    void CleanupD2D();
    void EnsureOffscreen(int w, int h);
    void FreeOffscreen();
    void ComputeTargetBlockRects(float layoutAnim, D2D1_RECT_F outRects[5]) const;
    void SnapBlocksToTargets();
    void RenderToTarget(ID2D1RenderTarget* rt, int w, int h);
    void HandleZoneClick(int zoneId, float mx, float my, const D2D1_RECT_F& rc);
    void CommitTempEdit();
    void CommitScaleEdit();
    void NotifyChanged(bool saveToDisk = true);
    bool PickColor(COLORREF& ioColor);
    void ApplyAutoStartTask(bool enable);

    HWND m_hwnd = NULL;
    HINSTANCE m_hInstance = NULL;
    AppConfig* m_config = nullptr;

    ID2D1Factory* m_d2dFactory = nullptr;
    IDWriteFactory* m_dwriteFactory = nullptr;
    ID2D1DCRenderTarget* m_dcRenderTarget = nullptr;
    ID2D1StrokeStyle* m_dashedStroke = nullptr;

    // Persistent double-buffer DIB for tear-free WM_PAINT
    HDC m_memDC = NULL;
    HBITMAP m_dibBmp = NULL;
    HBITMAP m_oldBmp = NULL;
    int m_bufW = 0;
    int m_bufH = 0;

    // High-resolution QPC timer for smooth 120FPS animation
    LARGE_INTEGER m_qpcFreq = {};
    LARGE_INTEGER m_lastTick = {};

    std::vector<UiHitZone> m_hitZones;
    int m_hoveredZone = -1;
    int m_draggingSliderZone = -1;
    int m_selectedBlock = 0;      // 0..3 = CPU/GPU/RAM/FPS, 4 = MEDIA
    int m_focusedTitleRow = -1;   // 0..3 when typing custom metric title
    int m_focusedTempIdx = -1;    // 0 = Warn temp, 1 = Crit temp
    std::wstring m_tempEditStr;

    // Direct scale percentage input
    bool m_editingScale = false;
    std::wstring m_scaleEditStr;

    // Animation state for 4-dot layout icon & 2D visual block drag-and-drop canvas
    float m_layoutAnim = 0.0f;    // 0.0 = Dual-Row (2x2), 1.0 = Single-Row (1x4)
    D2D1_RECT_F m_blockVisual[5] = {};
    bool m_blockAnimReady = false;
    int m_draggingBlock = -1;     // 0..3 hardware metric or 4 = MEDIA currently being dragged
    float m_dragGrabOffsetX = 0.0f;
    float m_dragGrabOffsetY = 0.0f;
};

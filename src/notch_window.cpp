#include "notch_window.h"
#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

#define TIMER_ANIM_ID 101
#define TIMER_DATA_ID 102

static D2D1_COLOR_F ColorRefToD2D(COLORREF c, float alpha = 1.0f) {
    return D2D1::ColorF(
        GetRValue(c) / 255.0f,
        GetGValue(c) / 255.0f,
        GetBValue(c) / 255.0f,
        alpha
    );
}

static inline float Smoothstep01(float x) {
    float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

NotchWindow::NotchWindow() {
    QueryPerformanceFrequency(&m_qpcFreq);
    QueryPerformanceCounter(&m_lastTick);
}

NotchWindow::~NotchWindow() {
    Destroy();
}

bool NotchWindow::InitD2D() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwriteFactory));
    if (FAILED(hr)) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT
    );

    hr = m_d2dFactory->CreateDCRenderTarget(&props, &m_dcRenderTarget);
    if (FAILED(hr)) return false;
    m_dcRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    m_dcRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

void NotchWindow::FreeCanvas() {
    if (m_memDC) {
        if (m_oldBmp) {
            SelectObject(m_memDC, m_oldBmp);
            m_oldBmp = NULL;
        }
        if (m_dibBmp) {
            DeleteObject(m_dibBmp);
            m_dibBmp = NULL;
        }
        DeleteDC(m_memDC);
        m_memDC = NULL;
    }
    m_dibBits = nullptr;
    m_canvasW = 0;
    m_canvasH = 0;
}

void NotchWindow::EnsureCanvas(int reqW, int reqH) {
    reqW = std::max(64, ((reqW + 3) / 4) * 4);
    reqH = std::max(32, ((reqH + 3) / 4) * 4);
    if (m_memDC && m_dibBmp && m_canvasW == reqW && m_canvasH == reqH) {
        return;
    }
    FreeCanvas();

    HDC hdcScreen = GetDC(NULL);
    m_memDC = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    if (!m_memDC) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = reqW;
    bmi.bmiHeader.biHeight = -reqH; // top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    m_dibBmp = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &m_dibBits, NULL, 0);
    if (m_dibBmp) {
        m_oldBmp = (HBITMAP)SelectObject(m_memDC, m_dibBmp);
        m_canvasW = reqW;
        m_canvasH = reqH;
    }
}

void NotchWindow::CleanupD2D() {
    FreeCanvas();
    if (m_albumBitmap) { m_albumBitmap->Release(); m_albumBitmap = nullptr; }
    if (m_dcRenderTarget) { m_dcRenderTarget->Release(); m_dcRenderTarget = nullptr; }
    if (m_dwriteFactory) { m_dwriteFactory->Release(); m_dwriteFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
}

static void GetDualAndSingleLogicalSize(const AppConfig* cfg, float& outDualW, float& outDualH, float& outSingleW, float& outSingleH) {
    float baseW = (float)((cfg->notchWidth / 4) * 4);
    int active = cfg->GetActiveMetricCount();
    float sW = baseW;
    if (active >= 4) sW = cfg->notchWidth * 1.76f;
    else if (active == 3) sW = cfg->notchWidth * 1.36f;
    outDualW = baseW;
    outSingleW = (float)(((int)std::round(sW / 4.0f)) * 4);

    int h = (cfg->notchHeight / 4) * 4;
    outDualH = (float)std::max(28, h);
    outSingleH = (float)std::max(28, ((h - 8) / 4) * 4);
}

bool NotchWindow::Create(HINSTANCE hInstance, AppConfig* config, TelemetryEngine* telemetry) {
    m_hInstance = hInstance;
    m_config = config;
    m_telemetry = telemetry;
    if (m_telemetry && m_config) {
        m_telemetry->SetGpuTarget(m_config->gpuTarget);
    }

    if (!InitD2D()) return false;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = NotchWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"RCNotchMonitor_OverlayClass";
    RegisterClassExW(&wc);

    m_layoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
    m_mediaDockAnim = (m_config->mediaDockSide == 1) ? 1.0f : 0.0f;
    bool hasMed = false;
    if (m_telemetry && m_config->mediaEnabled) {
        hasMed = m_telemetry->GetSnapshot().hasMediaSession;
    }
    m_mediaAnim = hasMed ? 1.0f : 0.0f;

    float wDual = 260.0f, hDual = 36.0f, wSingle = 396.0f, hSingle = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDual, hDual, wSingle, hSingle);
    float easeLayout = Smoothstep01(m_layoutAnim);
    float medExtra = (204.0f + 20.0f * easeLayout) * Smoothstep01(m_mediaAnim);
    m_currW = wDual + (wSingle - wDual) * easeLayout + medExtra;
    m_currH = hDual + (hSingle - hDual) * easeLayout;
    QueryPerformanceCounter(&m_lastTick);

    float scale = m_config->GetEffectiveScale();
    MonitorEntry mon = GetTargetMonitor(m_config->targetMonitor, m_config->targetMonitorDevice);
    int monW = mon.rcMonitor.right - mon.rcMonitor.left;
    int monLeft = mon.rcMonitor.left;
    int monTop = mon.rcMonitor.top;

    int logW = (((int)std::ceil(m_currW) + 16 + 3) / 4) * 4;
    int logH = (((int)std::ceil(m_currH) + 3) / 4) * 4;
    int winW = (((int)std::round(logW * scale) + 2) / 4) * 4;
    int winH = (((int)std::round(logH * scale) + 2) / 4) * 4;

    int maxOffset = std::max(0, monW - winW);
    float pct = std::clamp((float)m_config->positionOffsetPct / 100.0f, 0.0f, 1.0f);
    int posX = monLeft + (int)std::round(pct * (float)maxOffset);
    int posY = monTop;

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
    if (m_config->clickThroughLocked) {
        exStyle |= WS_EX_TRANSPARENT;
    }

    m_hwnd = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"RCNotchMonitor",
        WS_POPUP,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, this
    );

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetTimer(m_hwnd, TIMER_ANIM_ID, 8, NULL); // ~120fps high-precision delta-time animation
    SetTimer(m_hwnd, TIMER_DATA_ID, m_config->refreshIntervalMs, NULL);

    RenderAndPresent();
    return true;
}

void NotchWindow::Destroy() {
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_ANIM_ID);
        KillTimer(m_hwnd, TIMER_DATA_ID);
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
    CleanupD2D();
}

void NotchWindow::ApplyConfig() {
    if (!m_hwnd || !m_config) return;
    SetClickThrough(m_config->clickThroughLocked);
    QueryPerformanceCounter(&m_lastTick);
    KillTimer(m_hwnd, TIMER_DATA_ID);
    SetTimer(m_hwnd, TIMER_DATA_ID, m_config->refreshIntervalMs, NULL);
    if (m_telemetry) {
        m_telemetry->SetIntervalMs(m_config->refreshIntervalMs);
        m_telemetry->SetGpuTarget(m_config->gpuTarget);
    }
    RenderAndPresent();
}

void NotchWindow::SetClickThrough(bool enable) {
    if (!m_hwnd) return;
    m_config->clickThroughLocked = enable;
    LONG_PTR exStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if (enable) {
        exStyle |= WS_EX_TRANSPARENT;
        m_isHovered = false;
    } else {
        exStyle &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, exStyle);
}

void NotchWindow::ForceRedraw() {
    RenderAndPresent();
}

LRESULT CALLBACK NotchWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    NotchWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<NotchWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<NotchWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool PointInRectF(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

LRESULT NotchWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == TIMER_ANIM_ID) {
            OnTimerTick();
        } else if (wParam == TIMER_DATA_ID) {
            SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            RenderAndPresent();
        }
        return 0;

    case WM_MOUSEMOVE: {
        if (m_config->clickThroughLocked) return 0;
        if (GetCapture() == m_hwnd) {
            POINT ptScreen;
            GetCursorPos(&ptScreen);
            int dx = ptScreen.x - m_dragStartX;
            if (!m_isDragging && (std::abs(dx) > 4)) {
                m_isDragging = true;
            }
            if (m_isDragging) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                MonitorEntry mon = GetTargetMonitor(m_config->targetMonitor, m_config->targetMonitorDevice);
                int monW = mon.rcMonitor.right - mon.rcMonitor.left;
                int maxOffset = std::max(1, monW - m_canvasW);
                float deltaPct = ((float)dx / (float)maxOffset) * 100.0f;
                int newPct = std::clamp((int)std::round((float)m_dragStartOffsetPct + deltaPct), 0, 100);
                if (newPct != m_config->positionOffsetPct) {
                    m_config->positionOffsetPct = newPct;
                    RenderAndPresent();
                    if (onConfigChanged) onConfigChanged();
                }
                return 0;
            }
        }

        float scale = m_config->GetEffectiveScale();
        float canvasLogW = (m_canvasW > 0) ? ((float)m_canvasW / scale) : (m_currW + 16.0f);
        float drawW = m_currW + 16.0f;
        float offsetX = (canvasLogW - drawW) * 0.5f;

        float mx = (float)GET_X_LPARAM(lParam) / scale - offsetX;
        float my = (float)GET_Y_LPARAM(lParam) / scale;

        // Strict hit-test: Is the cursor actually within the visible notch boundary?
        float expH = (float)m_config->expandedHeight;
        float activeH = m_isHovered ? std::max(m_currH, expH) : m_currH;
        bool insideNotch = (mx >= -4.0f && mx <= drawW + 4.0f && my >= -4.0f && my <= activeH + 6.0f);

        if (!insideNotch) {
            // Cursor is in transparent canvas area outside the notch; do not trigger hover
            return 0;
        }

        if (!m_trackingMouse) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            m_trackingMouse = true;
        }
        if (!m_isHovered) {
            QueryPerformanceCounter(&m_lastTick);
            m_isHovered = true;
        }
        m_unhoverStreak = 0;

        int oldBtn = m_hoverButtonId;
        m_hoverButtonId = 0;
        if (m_animProgress > 0.65f) {
            if (PointInRectF(mx, my, m_btnSettingsRect)) m_hoverButtonId = 1;
            else if (PointInRectF(mx, my, m_btnLockRect)) m_hoverButtonId = 3;
            else if (m_mediaAnim > 0.5f) {
                if (PointInRectF(mx, my, m_btnMediaSwitchRect)) m_hoverButtonId = 10;
                else if (PointInRectF(mx, my, m_btnMediaPrevRect)) m_hoverButtonId = 11;
                else if (PointInRectF(mx, my, m_btnMediaRewRect)) m_hoverButtonId = 12;
                else if (PointInRectF(mx, my, m_btnMediaPlayRect)) m_hoverButtonId = 13;
                else if (PointInRectF(mx, my, m_btnMediaFfwdRect)) m_hoverButtonId = 14;
                else if (PointInRectF(mx, my, m_btnMediaNextRect)) m_hoverButtonId = 15;
                else if (PointInRectF(mx, my, m_rectMediaSeekBar)) m_hoverButtonId = 16;
            }
        }
        SetCursor(LoadCursor(NULL, m_hoverButtonId > 0 ? IDC_HAND : IDC_ARROW));
        if (oldBtn != m_hoverButtonId) {
            RenderAndPresent();
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (!m_config || m_config->clickThroughLocked) return 0;
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta > 0) m_config->uiScalePct = std::clamp(m_config->uiScalePct + 4, 60, 220);
        else if (delta < 0) m_config->uiScalePct = std::clamp(m_config->uiScalePct - 4, 60, 220);
        m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
        if (onConfigChanged) onConfigChanged();
        RenderAndPresent();
        return 0;
    }

    case WM_MOUSELEAVE: {
        m_trackingMouse = false;
        if (m_isHovered && !m_isDragging) {
            POINT pt;
            if (GetCursorPos(&pt)) {
                RECT rcWin = {};
                GetWindowRect(m_hwnd, &rcWin);
                float scale = m_config->GetEffectiveScale();
                float expH = (float)m_config->expandedHeight;
                float drawW = m_currW + 16.0f;
                float activePhysW = drawW * scale;
                float activePhysH = std::max(m_currH, expH) * scale;
                float winCenterX = (rcWin.left + rcWin.right) * 0.5f;
                float left = winCenterX - activePhysW * 0.5f - 6.0f;
                float right = winCenterX + activePhysW * 0.5f + 6.0f;
                float top = (float)rcWin.top - 4.0f;
                float bottom = (float)rcWin.top + activePhysH + 6.0f;
                if (pt.x < (LONG)left || pt.x > (LONG)right || pt.y < (LONG)top || pt.y > (LONG)bottom) {
                    QueryPerformanceCounter(&m_lastTick);
                    m_isHovered = false;
                    m_hoverButtonId = 0;
                    m_unhoverStreak = 0;
                } else {
                    // Cursor is still physically inside active area, re-arm tracking
                    TRACKMOUSEEVENT tme = {};
                    tme.cbSize = sizeof(tme);
                    tme.dwFlags = TME_LEAVE;
                    tme.hwndTrack = m_hwnd;
                    TrackMouseEvent(&tme);
                    m_trackingMouse = true;
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        float scale = m_config->GetEffectiveScale();
        float canvasLogW = (m_canvasW > 0) ? ((float)m_canvasW / scale) : (m_currW + 16.0f);
        float drawW = m_currW + 16.0f;
        float offsetX = (canvasLogW - drawW) * 0.5f;

        float mx = (float)GET_X_LPARAM(lParam) / scale - offsetX;
        float my = (float)GET_Y_LPARAM(lParam) / scale;
        if (m_animProgress > 0.65f) {
            if (PointInRectF(mx, my, m_btnSettingsRect)) {
                if (onOpenSettings) onOpenSettings();
                return 0;
            } else if (PointInRectF(mx, my, m_btnLockRect)) {
                SetClickThrough(true);
                m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
                if (onConfigChanged) onConfigChanged();
                RenderAndPresent();
                return 0;
            } else if (PointInRectF(mx, my, m_rectGpuCard) && m_telemetry) {
                m_config->gpuTarget = (m_config->gpuTarget + 1) % 3;
                m_telemetry->SetGpuTarget(m_config->gpuTarget);
                m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
                if (onConfigChanged) onConfigChanged();
                RenderAndPresent();
                return 0;
            } else if (m_mediaAnim > 0.5f && m_telemetry) {
                if (PointInRectF(mx, my, m_btnMediaSwitchRect)) {
                    m_telemetry->MediaCycleSession();
                    return 0;
                } else if (PointInRectF(mx, my, m_btnMediaPrevRect)) {
                    m_telemetry->MediaPrevTrack();
                    return 0;
                } else if (PointInRectF(mx, my, m_btnMediaRewRect)) {
                    m_telemetry->MediaSeekRelative(-10);
                    return 0;
                } else if (PointInRectF(mx, my, m_btnMediaPlayRect)) {
                    m_telemetry->MediaPlayPause();
                    return 0;
                } else if (PointInRectF(mx, my, m_btnMediaFfwdRect)) {
                    m_telemetry->MediaSeekRelative(+10);
                    return 0;
                } else if (PointInRectF(mx, my, m_btnMediaNextRect)) {
                    m_telemetry->MediaNextTrack();
                    return 0;
                } else if (PointInRectF(mx, my, m_rectMediaSeekBar)) {
                    float barW = std::max(1.0f, m_rectMediaSeekBar.right - m_rectMediaSeekBar.left);
                    float ratio = std::clamp((mx - m_rectMediaSeekBar.left) / barW, 0.0f, 1.0f);
                    m_telemetry->MediaSeekToRatio(ratio);
                    return 0;
                }
            }
        }
        // Prepare for top-edge dragging
        if (!m_config->clickThroughLocked) {
            POINT ptScreen;
            GetCursorPos(&ptScreen);
            m_isDragging = false;
            m_dragStartX = ptScreen.x;
            m_dragStartY = ptScreen.y;
            m_dragStartOffsetPct = m_config->positionOffsetPct;
            SetCapture(m_hwnd);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (GetCapture() == m_hwnd) {
            ReleaseCapture();
            if (m_isDragging) {
                m_isDragging = false;
                m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
                if (onConfigChanged) onConfigChanged();
                return 0;
            }
        }
        return 0;
    }

    case WM_RBUTTONUP:
        if (onOpenSettings) onOpenSettings();
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void NotchWindow::OnTimerTick() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = (m_qpcFreq.QuadPart > 0)
        ? (float)(now.QuadPart - m_lastTick.QuadPart) / (float)m_qpcFreq.QuadPart
        : 0.008f;
    m_lastTick = now;
    dt = std::clamp(dt, 0.001f, 0.050f);

    float wDualRef = 260.0f, hDualRef = 36.0f, wSingleRef = 396.0f, hSingleRef = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDualRef, hDualRef, wSingleRef, hSingleRef);
    float easeLayoutRef = Smoothstep01(m_layoutAnim);
    float easeMediaRef  = Smoothstep01(m_mediaAnim);
    float medCollapsedWRef = (204.0f + 20.0f * easeLayoutRef) * easeMediaRef;
    float colWRef = wDualRef + (wSingleRef - wDualRef) * easeLayoutRef + medCollapsedWRef;
    float expWRef = (easeMediaRef > 0.02f || m_config->layoutMode == 1) ? colWRef : (float)m_config->expandedWidth;
    float expHRef = (float)m_config->expandedHeight;

    if (m_isHovered && !m_config->clickThroughLocked && !m_isDragging) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            RECT rcWin = {};
            GetWindowRect(m_hwnd, &rcWin);
            float scale = m_config->GetEffectiveScale();
            float activeLogW = std::max({ m_currW, colWRef, expWRef }) + 16.0f;
            float activeLogH = std::max(m_currH, expHRef);
            float actualPhysW = activeLogW * scale;
            float actualPhysH = activeLogH * scale;
            float winCenterX = (rcWin.left + rcWin.right) * 0.5f;
            float left = winCenterX - actualPhysW * 0.5f - 8.0f;
            float right = winCenterX + actualPhysW * 0.5f + 8.0f;
            float top = (float)rcWin.top - 4.0f;
            float bottom = (float)rcWin.top + actualPhysH + 8.0f;
            if (pt.x < (LONG)left || pt.x > (LONG)right || pt.y < (LONG)top || pt.y > (LONG)bottom) {
                m_unhoverStreak++;
                if (m_unhoverStreak >= 4) { // ~60-80ms stable grace period before collapsing
                    m_isHovered = false;
                    m_hoverButtonId = 0;
                    m_unhoverStreak = 0;
                }
            } else {
                m_unhoverStreak = 0;
            }
        }
    } else {
        m_unhoverStreak = 0;
    }

    TelemetrySnapshot snap;
    if (m_telemetry) {
        snap = m_telemetry->GetSnapshot();
    }

    float targetProgress = (m_isHovered && !m_config->clickThroughLocked) ? 1.0f : 0.0f;
    float targetLayoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
    float targetMediaAnim = (m_config->mediaEnabled && snap.hasMediaSession) ? 1.0f : 0.0f;
    float targetDockAnim = (m_config->mediaDockSide == 1) ? 1.0f : 0.0f;

    bool changed = false;

    auto stepLinear = [&](float& val, float target, float durationSec) {
        if (std::fabs(val - target) > 0.002f) {
            float step = dt / durationSec;
            if (val < target) val = std::min(target, val + step);
            else val = std::max(target, val - step);
            changed = true;
        } else if (val != target) {
            val = target;
            changed = true;
        }
    };

    stepLinear(m_animProgress, targetProgress, 0.22f);
    stepLinear(m_layoutAnim, targetLayoutAnim, 0.26f);
    stepLinear(m_mediaAnim, targetMediaAnim, 0.26f);
    stepLinear(m_mediaDockAnim, targetDockAnim, 0.26f);

    // Advance marquee & audio visualizer bars when media is visible
    if (m_mediaAnim > 0.01f) {
        // Preference 3+1: When mouse expands the notch (or hovered), marquee scrolls even if paused (Mode 1).
        // When outside / collapsed, paused state completely freezes marquee at current position (Mode 3).
        bool shouldScrollMarquee = snap.mediaIsPlaying || (m_isHovered || m_animProgress > 0.05f);
        if (shouldScrollMarquee) {
            m_marqueePhase += dt * 36.0f; // 36 logical px/sec smooth marquee scroll
            if (m_marqueePhase > 10000.0f) m_marqueePhase -= 10000.0f;
            m_marqueeOffset = m_marqueePhase;
        }

        float rawPeak = m_telemetry ? m_telemetry->PollAudioPeakFast() : snap.audioPeak;
        if (snap.mediaIsPlaying && rawPeak < 0.08f) {
            // Subtle breathing pulse when playing at low/muted volume
            rawPeak = 0.28f + 0.18f * std::sin(m_marqueePhase * 0.20f);
        }
        const float freqs[6] = { 0.19f, 0.31f, 0.25f, 0.37f, 0.22f, 0.34f };
        const float phases[6] = { 0.0f, 1.4f, 2.7f, 4.1f, 0.8f, 2.1f };
        float barAlpha = 1.0f - std::exp(-20.0f * dt);
        for (int b = 0; b < 6; ++b) {
            float wave = 0.55f + 0.45f * std::sin(m_marqueePhase * freqs[b] + phases[b]);
            float targetBar = snap.mediaIsPlaying
                ? std::clamp(0.14f + rawPeak * wave * 1.15f, 0.14f, 1.0f)
                : 0.14f;
            if (std::fabs(m_audioBars[b] - targetBar) > 0.01f) {
                m_audioBars[b] += (targetBar - m_audioBars[b]) * barAlpha;
                changed = true;
            }
        }
        // Keep redrawing smoothly for marquee scroll whenever marquee is actively scrolling
        if (shouldScrollMarquee) {
            changed = true;
        }
    }

    float wDual = 260.0f, hDual = 36.0f, wSingle = 396.0f, hSingle = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDual, hDual, wSingle, hSingle);

    float easeLayout = Smoothstep01(m_layoutAnim);
    float easeExpand = Smoothstep01(m_animProgress);
    float easeMedia  = Smoothstep01(m_mediaAnim);

    float medCollapsedW = (204.0f + 20.0f * easeLayout) * easeMedia;
    float collapsedW = wDual + (wSingle - wDual) * easeLayout + medCollapsedW;
    float collapsedH = hDual + (hSingle - hDual) * easeLayout;

    // Expanded width matches the original collapsed width (especially with media),
    // ensuring the left and right boundaries remain stationary and completely preventing
    // boundary oscillation and open/close flickering ("避免有部分區塊可以一直觸發開關導致跳閃")
    float expW = (easeMedia > 0.02f || m_config->layoutMode == 1)
        ? collapsedW
        : (float)m_config->expandedWidth;
    float expH = (float)m_config->expandedHeight;

    float desiredW = collapsedW + (expW - collapsedW) * easeExpand;
    float desiredH = collapsedH + (expH - collapsedH) * easeExpand;

    if (std::fabs(m_currW - desiredW) > 0.05f || std::fabs(m_currH - desiredH) > 0.05f) {
        if (std::fabs(m_animProgress - targetProgress) > 0.001f ||
            std::fabs(m_layoutAnim - targetLayoutAnim) > 0.001f ||
            std::fabs(m_mediaAnim - targetMediaAnim) > 0.001f) {
            m_currW = desiredW;
            m_currH = desiredH;
        } else {
            float alpha = 1.0f - std::exp(-22.0f * dt);
            m_currW += (desiredW - m_currW) * alpha;
            m_currH += (desiredH - m_currH) * alpha;
        }
        changed = true;
    } else if (m_currW != desiredW || m_currH != desiredH) {
        m_currW = desiredW;
        m_currH = desiredH;
        changed = true;
    }

    if (changed) {
        RenderAndPresent();
    }
}

void NotchWindow::UpdateAlbumArtBitmap(ID2D1RenderTarget* rt, const TelemetrySnapshot& snap) {
    if (!rt || !snap.hasAlbumArt || snap.albumArtBgra.empty() || snap.albumArtW <= 0 || snap.albumArtH <= 0) return;
    if (m_albumBitmap && m_albumBitmapVer == snap.albumArtVer) return;

    if (m_albumBitmap) {
        m_albumBitmap->Release();
        m_albumBitmap = nullptr;
    }

    D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f
    );
    if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU((UINT32)snap.albumArtW, (UINT32)snap.albumArtH),
                                   snap.albumArtBgra.data(), (UINT32)(snap.albumArtW * 4), &bp, &m_albumBitmap))) {
        m_albumBitmapVer = snap.albumArtVer;
        m_albumBitmapW = snap.albumArtW;
        m_albumBitmapH = snap.albumArtH;
    }
}

void NotchWindow::DrawRoundedAlbumArt(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float radius, float alpha) {
    if (!rt) return;
    float w = rc.right - rc.left;
    float h = rc.bottom - rc.top;
    if (w <= 1.0f || h <= 1.0f) return;

    if (m_albumBitmap) {
        ID2D1BitmapBrush* bmpBrush = nullptr;
        D2D1_BITMAP_BRUSH_PROPERTIES bbp = D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
        if (SUCCEEDED(rt->CreateBitmapBrush(m_albumBitmap, bbp, &bmpBrush)) && bmpBrush) {
            // Keep aspect ratio without deformation: fill height ("把高撐滿") and center horizontally ("致中")
            float bmpW = std::max(1.0f, (float)m_albumBitmapW);
            float bmpH = std::max(1.0f, (float)m_albumBitmapH);
            float scale = std::max(w / bmpW, h / bmpH);
            float drawBmpW = bmpW * scale;
            float drawBmpH = bmpH * scale;
            float transX = rc.left + (w - drawBmpW) * 0.5f;
            float transY = rc.top + (h - drawBmpH) * 0.5f;

            bmpBrush->SetTransform(
                D2D1::Matrix3x2F::Scale(scale, scale) *
                D2D1::Matrix3x2F::Translation(transX, transY)
            );
            bmpBrush->SetOpacity(alpha);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rc, radius, radius), bmpBrush);
            bmpBrush->Release();
            return;
        }
    }

    // Fallback icon box
    ID2D1SolidColorBrush* brBg = nullptr;
    ID2D1SolidColorBrush* brFg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.18f, 0.24f, alpha), &brBg);
    rt->CreateSolidColorBrush(ColorRefToD2D(m_config ? m_config->mediaWaveColor : RGB(74, 222, 128), alpha), &brFg);
    if (brBg) rt->FillRoundedRectangle(D2D1::RoundedRect(rc, radius, radius), brBg);
    if (brFg) DrawFaIcon(rt, FA_ICON_MUSIC, (rc.left + rc.right) * 0.5f, (rc.top + rc.bottom) * 0.5f, w * 0.58f, brFg);
    if (brBg) brBg->Release();
    if (brFg) brFg->Release();
}

void NotchWindow::DrawAudioVisualizer(ID2D1RenderTarget* rt, float x, float cy, float w, float maxH, D2D1_COLOR_F col) {
    ID2D1SolidColorBrush* br = nullptr;
    rt->CreateSolidColorBrush(col, &br);
    if (!br) return;

    const int nBars = 6;
    float barW = 1.8f;
    float gap = (w - nBars * barW) / (float)(nBars - 1);
    for (int i = 0; i < nBars; ++i) {
        float bh = std::clamp(m_audioBars[i], 0.14f, 1.0f) * maxH;
        float bx = x + i * (barW + gap);
        D2D1_RECT_F rcBar = D2D1::RectF(bx, cy - bh * 0.5f, bx + barW, cy + bh * 0.5f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rcBar, 0.9f, 0.9f), br);
    }
    br->Release();
}

void NotchWindow::DrawFaIcon(ID2D1RenderTarget* rt, FaIconType icon, float cx, float cy, float size, ID2D1Brush* brush) {
    D2D1_COLOR_F holeCol = ColorRefToD2D(m_config ? m_config->bgColor : RGB(12, 14, 20), 1.0f);
    ::DrawFaIcon(m_d2dFactory, rt, icon, cx, cy, size, brush, holeCol, m_layoutAnim);
}

void NotchWindow::DrawNotchShape(ID2D1RenderTarget* rt, float w, float h, float radius, D2D1_COLOR_F fillCol, D2D1_COLOR_F borderCol) {
    float ear = 8.0f;
    float left = ear;
    float right = w - ear;
    float bottom = h - 1.5f;
    float r = std::min(radius, std::min((right - left) * 0.4f, bottom * 0.65f));

    ID2D1PathGeometry* path = nullptr;
    if (FAILED(m_d2dFactory->CreatePathGeometry(&path)) || !path) return;

    ID2D1GeometrySink* sink = nullptr;
    if (SUCCEEDED(path->Open(&sink))) {
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(D2D1::Point2F(0.0f, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(ear * 0.75f, 0.0f),
            D2D1::Point2F(left, ear * 0.25f),
            D2D1::Point2F(left, ear)
        ));
        sink->AddLine(D2D1::Point2F(left, bottom - r));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(left, bottom - r * 0.44f),
            D2D1::Point2F(left + r * 0.44f, bottom),
            D2D1::Point2F(left + r, bottom)
        ));
        sink->AddLine(D2D1::Point2F(right - r, bottom));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(right - r * 0.44f, bottom),
            D2D1::Point2F(right, bottom - r * 0.44f),
            D2D1::Point2F(right, bottom - r)
        ));
        sink->AddLine(D2D1::Point2F(right, ear));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(right, ear * 0.25f),
            D2D1::Point2F(w - ear * 0.75f, 0.0f),
            D2D1::Point2F(w, 0.0f)
        ));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        sink->Release();
    }

    ID2D1SolidColorBrush* fillBrush = nullptr;
    ID2D1SolidColorBrush* borderBrush = nullptr;
    rt->CreateSolidColorBrush(fillCol, &fillBrush);
    rt->CreateSolidColorBrush(borderCol, &borderBrush);

    if (fillBrush) rt->FillGeometry(path, fillBrush);
    if (borderBrush) rt->DrawGeometry(path, borderBrush, 1.2f);

    if (fillBrush) fillBrush->Release();
    if (borderBrush) borderBrush->Release();
    path->Release();
}

static float MeasureTextWidth(IDWriteFactory* dwFactory, IDWriteTextFormat* fmt, const std::wstring& text) {
    if (text.empty()) return 0.0f;
    IDWriteTextLayout* layout = nullptr;
    float w = 0.0f;
    if (SUCCEEDED(dwFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, 1000.0f, 50.0f, &layout))) {
        DWRITE_TEXT_METRICS tm = {};
        if (SUCCEEDED(layout->GetMetrics(&tm))) {
            w = tm.widthIncludingTrailingWhitespace;
        }
        layout->Release();
    }
    return w;
}

static void DrawMarqueeText(ID2D1RenderTarget* rt, IDWriteFactory* dwFactory, IDWriteTextFormat* fmt,
                            const std::wstring& text, const D2D1_RECT_F& clipRect, ID2D1Brush* brush, float scrollOffset) {
    if (text.empty() || !rt || !fmt || !brush) return;
    float boxW = clipRect.right - clipRect.left;
    if (boxW <= 4.0f) return;

    float textW = MeasureTextWidth(dwFactory, fmt, text);
    rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (textW <= boxW) {
        rt->DrawText(text.c_str(), (UINT32)text.size(), fmt, clipRect, brush);
    } else {
        float gap = 28.0f;
        float cycle = textW + gap;
        float shift = std::fmod(scrollOffset, cycle);
        float x1 = clipRect.left - shift;
        D2D1_RECT_F rc1 = D2D1::RectF(x1, clipRect.top, x1 + textW + 4.0f, clipRect.bottom);
        rt->DrawText(text.c_str(), (UINT32)text.size(), fmt, rc1, brush);

        if (x1 + textW + gap < clipRect.right) {
            float x2 = x1 + cycle;
            D2D1_RECT_F rc2 = D2D1::RectF(x2, clipRect.top, x2 + textW + 4.0f, clipRect.bottom);
            rt->DrawText(text.c_str(), (UINT32)text.size(), fmt, rc2, brush);
        }
    }

    rt->PopAxisAlignedClip();
}

void NotchWindow::DrawCollapsedContent(ID2D1RenderTarget* rt, float w, float h, float alpha) {
    if (alpha <= 0.02f || !m_config || !m_telemetry) return;

    TelemetrySnapshot snap = m_telemetry->GetSnapshot();
    UpdateAlbumArtBitmap(rt, snap);

    std::vector<int> activeItems;
    for (int i = 0; i < 4; ++i) {
        int idx = m_config->itemOrder[i];
        if (idx >= 0 && idx < 4 && m_config->metrics[idx].enabled) {
            activeItems.push_back(idx);
        }
    }

    int count = (int)activeItems.size();
    float layoutT = (count <= 2) ? 1.0f : Smoothstep01(m_layoutAnim);
    float easeMedia = Smoothstep01(m_mediaAnim);
    float easeDock  = Smoothstep01(m_mediaDockAnim); // 0.0 = Left, 1.0 = Right

    float padX = 20.0f;
    float innerH = h - 4.0f;
    float medAllocatedW = (204.0f + 20.0f * layoutT) * easeMedia;

    // Determine horizontal sub-regions for Hardware Block and Media Block
    float totalInnerW = std::max(40.0f, w - padX * 2.0f);
    float hwRegionW = std::max(40.0f, totalInnerW - medAllocatedW);
    // When easeDock == 0 (Media Left): hwStartX = padX + medAllocatedW; when easeDock == 1 (Media Right): hwStartX = padX
    float hwStartX = padX + medAllocatedW * (1.0f - easeDock);

    // ==================== 1. Draw Collapsed Media Block (Source Art + Marquee + Audio Peak) ====================
    if (easeMedia > 0.02f && !snap.mediaTitle.empty()) {
        float medAlpha = alpha * easeMedia;
        float medBoxW = 156.0f + 14.0f * layoutT;
        float medX_Left  = padX;
        float medX_Right = w - padX - medBoxW;
        float medX = medX_Left + (medX_Right - medX_Left) * easeDock;

        // Generous, balanced section gap between Media and Monitoring regions (centered divider)
        float divX_Left  = (medX_Left + medBoxW + padX + medAllocatedW) * 0.5f;
        float divX_Right = (padX + hwRegionW + medX_Right) * 0.5f;
        float divX = divX_Left + (divX_Right - divX_Left) * easeDock;

        ID2D1SolidColorBrush* brDiv = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f * medAlpha), &brDiv);
        if (brDiv) {
            rt->DrawLine(D2D1::Point2F(divX, 5.0f), D2D1::Point2F(divX, h - 7.0f), brDiv, 1.0f);
            brDiv->Release();
        }

        // Interpolate Album Art size between Dual-Row (24x24 spanning both rows) and Single-Row (18x18)
        float artSize = 22.0f - 4.0f * layoutT;
        float artY = 1.0f + (innerH - artSize) * 0.5f;
        D2D1_RECT_F rcArt = D2D1::RectF(medX, artY, medX + artSize, artY + artSize);
        DrawRoundedAlbumArt(rt, rcArt, 4.0f, medAlpha);

        IDWriteTextFormat* fmtMedTitle = nullptr;
        IDWriteTextFormat* fmtMedSub = nullptr;
        m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.2f - 0.4f * layoutT, L"zh-TW", &fmtMedTitle);
        m_dwriteFactory->CreateTextFormat(L"Segoe UI Semibold", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 8.8f, L"zh-TW", &fmtMedSub);
        if (fmtMedTitle && fmtMedSub) {
            fmtMedTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            fmtMedTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            fmtMedSub->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            fmtMedSub->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            ID2D1SolidColorBrush* brMedTitle = nullptr;
            ID2D1SolidColorBrush* brMedSub = nullptr;
            rt->CreateSolidColorBrush(ColorRefToD2D(m_config->mediaTitleColor, medAlpha), &brMedTitle);
            rt->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.68f, 0.78f, medAlpha * (1.0f - layoutT)), &brMedSub);

            float textLeft = medX + artSize + 6.0f;
            float waveW = 18.0f;

            // Dual-Row: Top line = full-width Marquee, Bottom line = Source Name + Audio Peak Bars
            // Single-Row: Single line = Marquee on left + Audio Peak Bars on right
            float marqueeRight = (medX + medBoxW) - (waveW + 6.0f) * layoutT;
            float marqueeTop = 1.0f;
            float marqueeBot = 1.0f + innerH * (0.54f + 0.46f * layoutT);

            std::wstring fullTrack = snap.mediaTitle;
            if (!snap.mediaArtist.empty() && fullTrack.find(snap.mediaArtist) == std::wstring::npos) {
                fullTrack += L" — " + snap.mediaArtist;
            }
            DrawMarqueeText(rt, m_dwriteFactory, fmtMedTitle, fullTrack,
                            D2D1::RectF(textLeft, marqueeTop, marqueeRight, marqueeBot),
                            brMedTitle, m_marqueeOffset);

            // In Dual-Row mode, draw source label on bottom-left of media block
            if (layoutT < 0.85f && brMedSub) {
                D2D1_RECT_F rcSrc = D2D1::RectF(textLeft, 1.0f + innerH * 0.50f, medX + medBoxW - waveW - 4.0f, 1.0f + innerH);
                rt->DrawText(snap.mediaSourceName.c_str(), (UINT32)snap.mediaSourceName.size(), fmtMedSub, rcSrc, brMedSub);
            }

            // Audio Visualizer Bars (bottom-right in Dual-Row, center-right in Single-Row)
            float waveX = medX + medBoxW - waveW;
            float waveCy_Dual = 1.0f + innerH * 0.75f;
            float waveCy_Single = 1.0f + innerH * 0.50f;
            float waveCy = waveCy_Dual + (waveCy_Single - waveCy_Dual) * layoutT;
            float waveMaxH = 11.0f + 3.0f * layoutT;
            DrawAudioVisualizer(rt, waveX, waveCy, waveW, waveMaxH, ColorRefToD2D(m_config->mediaWaveColor, medAlpha));

            if (brMedTitle) brMedTitle->Release();
            if (brMedSub) brMedSub->Release();
        }
        if (fmtMedTitle) fmtMedTitle->Release();
        if (fmtMedSub) fmtMedSub->Release();
    }

    if (activeItems.empty()) return;

    // ==================== 2. Draw Hardware Telemetry Block (with 2D Single <-> Dual Tweening) ====================
    struct ItemRenderData {
        int mIdx;
        std::wstring title;
        std::wstring val1;
        std::wstring val2;
        bool hasTemp = false;
        COLORREF titleCol;
        COLORREF val1Col;
        COLORREF val2Col;
        float titleW = 0.0f;
        float slot1W = 0.0f;
        float slot2W = 0.0f;
        float totalW = 0.0f;
    };
    std::vector<ItemRenderData> items(count);

    for (int i = 0; i < count; ++i) {
        int mIdx = activeItems[i];
        const MetricConfig& mc = m_config->metrics[mIdx];
        items[i].mIdx = mIdx;
        items[i].title = mc.title;
        items[i].titleCol = mc.titleColor;
        items[i].val1Col = mc.valueColor;
        items[i].val2Col = mc.valueColor;

        wchar_t buf1[32] = {0};
        wchar_t buf2[32] = {0};
        if (mIdx == 0) { // CPU
            swprintf(buf1, 32, L"%d%%", snap.cpuUsagePct);
            if (mc.showTemp) {
                items[i].hasTemp = true;
                if (snap.cpuTempC > 0) {
                    swprintf(buf2, 32, L"%d°C", snap.cpuTempC);
                    if (snap.cpuTempC >= m_config->tempCritC) items[i].val2Col = m_config->critColor;
                    else if (snap.cpuTempC >= m_config->tempWarnC) items[i].val2Col = m_config->warnColor;
                } else {
                    swprintf(buf2, 32, L"--");
                }
            }
        } else if (mIdx == 1) { // GPU
            swprintf(buf1, 32, L"%d%%", snap.gpuUsagePct);
            if (mc.showTemp) {
                items[i].hasTemp = true;
                if (snap.gpuTempC > 0) {
                    swprintf(buf2, 32, L"%d°C", snap.gpuTempC);
                    if (snap.gpuTempC >= m_config->tempCritC) items[i].val2Col = m_config->critColor;
                    else if (snap.gpuTempC >= m_config->tempWarnC) items[i].val2Col = m_config->warnColor;
                } else {
                    swprintf(buf2, 32, L"--");
                }
            }
        } else if (mIdx == 2) { // RAM
            swprintf(buf1, 32, L"%d%%", snap.ramUsagePct);
        } else if (mIdx == 3) { // FPS
            if (snap.currentFps > 0) {
                swprintf(buf1, 32, L"%d", snap.currentFps);
            } else if (m_config->fpsIdleMode == 1) {
                swprintf(buf1, 32, L"%dHz", snap.monitorRefreshHz);
            } else {
                swprintf(buf1, 32, L"--");
            }
        }
        items[i].val1 = buf1;
        items[i].val2 = buf2;
    }

    float titleValGap = 6.0f;
    float tempSlotGap = 6.0f;

    float wDualRef = 260.0f, hDualRef = 36.0f, wSingleRef = 396.0f, hSingleRef = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDualRef, hDualRef, wSingleRef, hSingleRef);
    float innerW_Dual = (wDualRef + 16.0f) - padX * 2.0f;
    float innerW_Single = (wSingleRef + 16.0f) - padX * 2.0f;

    float fontDual = 11.0f;
    float fontSingle = 11.0f * (count >= 3 ? 0.90f : 1.0f);
    float baseFontSize = fontDual + (fontSingle - fontDual) * layoutT;

    IDWriteTextFormat* fmtTitle = nullptr;
    IDWriteTextFormat* fmtValueRight = nullptr;

    auto createFormatsAndMeasure = [&](float fSize) {
        if (fmtTitle) { fmtTitle->Release(); fmtTitle = nullptr; }
        if (fmtValueRight) { fmtValueRight->Release(); fmtValueRight = nullptr; }
        m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fSize, L"zh-TW", &fmtTitle);
        m_dwriteFactory->CreateTextFormat(L"Segoe UI Semibold", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fSize, L"zh-TW", &fmtValueRight);
        if (!fmtTitle || !fmtValueRight) return 0.0f;

        fmtTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmtValueRight->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmtTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtValueRight->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmtValueRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

        float fixed4CharPctW = std::max(
            MeasureTextWidth(m_dwriteFactory, fmtValueRight, L"100%"),
            MeasureTextWidth(m_dwriteFactory, fmtValueRight, L"9999")
        ) + 2.0f;
        float fixed4CharTempW = MeasureTextWidth(m_dwriteFactory, fmtValueRight, L"99°C") + 2.0f;

        float sumW = 0.0f;
        for (int i = 0; i < count; ++i) {
            items[i].titleW = MeasureTextWidth(m_dwriteFactory, fmtTitle, items[i].title);
            items[i].slot1W = fixed4CharPctW;
            items[i].slot2W = items[i].hasTemp ? fixed4CharTempW : 0.0f;
            items[i].totalW = items[i].titleW + titleValGap + items[i].slot1W +
                              (items[i].hasTemp ? (tempSlotGap + items[i].slot2W) : 0.0f);
            sumW += items[i].totalW;
        }
        return sumW;
    };

    float sumItemsW = createFormatsAndMeasure(baseFontSize);
    if (!fmtTitle || !fmtValueRight) return;

    float minItemGap = 12.0f;
    float neededSingleW = sumItemsW + (count > 1 ? (count - 1) * minItemGap : 0.0f);
    float scaleSingle = (neededSingleW > innerW_Single && neededSingleW > 1.0f)
        ? std::max(0.68f, (innerW_Single / neededSingleW) * 0.97f)
        : 1.0f;

    float colMaxTitleW[2] = {0.0f, 0.0f};
    for (int i = 0; i < count; ++i) {
        int c = i % 2;
        if (items[i].titleW > colMaxTitleW[c]) colMaxTitleW[c] = items[i].titleW;
    }
    float maxColTotalW = 0.0f;
    for (int i = 0; i < count; ++i) {
        int c = i % 2;
        float alignedW = colMaxTitleW[c] + titleValGap + items[i].slot1W +
                         (items[i].hasTemp ? (tempSlotGap + items[i].slot2W) : 0.0f);
        if (alignedW > maxColTotalW) maxColTotalW = alignedW;
    }
    float cellW_Dual = innerW_Dual * 0.5f;
    float scaleDual = (maxColTotalW > cellW_Dual - 6.0f && maxColTotalW > 1.0f)
        ? std::max(0.68f, (cellW_Dual - 6.0f) / maxColTotalW)
        : 1.0f;

    float effScaleFactor = scaleDual + (scaleSingle - scaleDual) * layoutT;
    if (effScaleFactor < 0.99f) {
        sumItemsW = createFormatsAndMeasure(std::max(7.5f, baseFontSize * effScaleFactor));
        colMaxTitleW[0] = colMaxTitleW[1] = 0.0f;
        for (int i = 0; i < count; ++i) {
            int c = i % 2;
            if (items[i].titleW > colMaxTitleW[c]) colMaxTitleW[c] = items[i].titleW;
        }
    }

    // 1. Single-Row coordinates relative to hardware region center
    float gapSingle = (count > 1) ? std::max(6.0f, (innerW_Single - sumItemsW) / (float)(count - 1)) : 0.0f;
    float totalRowW = sumItemsW + gapSingle * (count - 1);
    float startX_Single = -totalRowW * 0.5f;
    std::vector<float> dxSingle(count), ySingle(count), hSingle(count), titleSlotSingle(count);
    float runX = startX_Single;
    for (int i = 0; i < count; ++i) {
        dxSingle[i] = runX;
        ySingle[i] = 1.0f;
        hSingle[i] = innerH;
        titleSlotSingle[i] = items[i].titleW;
        runX += items[i].totalW + gapSingle;
    }

    // 2. Dual-Row coordinates relative to hardware region center
    int rowsDual = (count + 1) / 2;
    float cellH_Dual = innerH / (float)rowsDual;
    std::vector<float> dxDual(count), yDual(count), hDual(count), titleSlotDual(count);
    for (int i = 0; i < count; ++i) {
        int r = i / 2;
        int c = i % 2;
        float xInDual = c * cellW_Dual + 4.0f;
        dxDual[i] = xInDual - innerW_Dual * 0.5f;
        yDual[i] = 1.0f + r * cellH_Dual;
        hDual[i] = cellH_Dual;
        titleSlotDual[i] = colMaxTitleW[c];
    }

    float hwCenterX = hwStartX + hwRegionW * 0.5f;
    for (int i = 0; i < count; ++i) {
        float curX = hwCenterX + dxDual[i] + (dxSingle[i] - dxDual[i]) * layoutT;
        float curY = yDual[i] + (ySingle[i] - yDual[i]) * layoutT;
        float curH = hDual[i] + (hSingle[i] - hDual[i]) * layoutT;
        float curTitleW = titleSlotDual[i] + (titleSlotSingle[i] - titleSlotDual[i]) * layoutT;

        ID2D1SolidColorBrush* brTitle = nullptr;
        ID2D1SolidColorBrush* brVal1 = nullptr;
        ID2D1SolidColorBrush* brVal2 = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(items[i].titleCol, alpha), &brTitle);
        rt->CreateSolidColorBrush(ColorRefToD2D(items[i].val1Col, alpha), &brVal1);
        rt->CreateSolidColorBrush(ColorRefToD2D(items[i].val2Col, alpha), &brVal2);

        float xTitleEnd = curX + curTitleW;
        float xSlot1End = xTitleEnd + titleValGap + items[i].slot1W;

        D2D1_RECT_F rcTitle = D2D1::RectF(curX, curY, xTitleEnd + 2.0f, curY + curH);
        D2D1_RECT_F rcSlot1 = D2D1::RectF(xTitleEnd + titleValGap, curY, xSlot1End, curY + curH);

        if (brTitle) rt->DrawText(items[i].title.c_str(), (UINT32)items[i].title.size(), fmtTitle, rcTitle, brTitle);
        if (brVal1)  rt->DrawText(items[i].val1.c_str(), (UINT32)items[i].val1.size(), fmtValueRight, rcSlot1, brVal1);

        if (items[i].hasTemp && brVal2) {
            float xSlot2End = xSlot1End + tempSlotGap + items[i].slot2W;
            D2D1_RECT_F rcSlot2 = D2D1::RectF(xSlot1End + tempSlotGap, curY, xSlot2End, curY + curH);
            rt->DrawText(items[i].val2.c_str(), (UINT32)items[i].val2.size(), fmtValueRight, rcSlot2, brVal2);
        }

        if (brTitle) brTitle->Release();
        if (brVal1)  brVal1->Release();
        if (brVal2)  brVal2->Release();
    }

    fmtTitle->Release();
    fmtValueRight->Release();
}

void NotchWindow::DrawExpandedContent(ID2D1RenderTarget* rt, float w, float h, float alpha) {
    if (alpha <= 0.02f || !m_config || !m_telemetry) return;

    TelemetrySnapshot snap = m_telemetry->GetSnapshot();
    UpdateAlbumArtBitmap(rt, snap);

    IDWriteTextFormat* fmtHeader = nullptr;
    IDWriteTextFormat* fmtCardTitle = nullptr;
    IDWriteTextFormat* fmtCardBig = nullptr;
    IDWriteTextFormat* fmtCardSub = nullptr;
    IDWriteTextFormat* fmtTime = nullptr;

    m_dwriteFactory->CreateTextFormat(L"Segoe UI Semibold", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"zh-TW", &fmtHeader);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-TW", &fmtCardTitle);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-TW", &fmtCardBig);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.6f, L"zh-TW", &fmtCardSub);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI Semibold", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.0f, L"zh-TW", &fmtTime);

    if (!fmtHeader || !fmtCardTitle || !fmtCardBig || !fmtCardSub || !fmtTime) return;

    fmtHeader->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtCardBig->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtCardSub->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtTime->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float padX = 16.0f; // Symmetric horizontal & bottom padding
    float expCornerRadius = std::max((float)m_config->cornerRadius, padX + 8.0f);
    float cardRadius = std::max(4.0f, expCornerRadius - padX); // Concentric: R_card = R_notch - pad

    ID2D1SolidColorBrush* brGreen = nullptr;
    ID2D1SolidColorBrush* brMuted = nullptr;
    ID2D1SolidColorBrush* brCardBg = nullptr;
    ID2D1SolidColorBrush* brBarBg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.29f, 0.87f, 0.50f, alpha), &brGreen);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.71f, 0.80f, alpha), &brMuted);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.055f * alpha), &brCardBg);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f * alpha), &brBarBg);

    auto drawIconBtn = [&](const D2D1_RECT_F& rcBtn, FaIconType icon, int hoverId, bool activeHighlight = false) {
        bool isHov = (m_hoverButtonId == hoverId);
        COLORREF rawBg = (isHov || activeHighlight) ? m_config->btnHoverColor : m_config->btnColor;
        int lum = (GetRValue(rawBg) * 299 + GetGValue(rawBg) * 587 + GetBValue(rawBg) * 114) / 1000;
        D2D1_COLOR_F bgCol = ColorRefToD2D(rawBg, (isHov ? 0.96f : 0.90f) * alpha);
        D2D1_COLOR_F icCol = (lum > 145)
            ? D2D1::ColorF(0.05f, 0.06f, 0.09f, alpha)
            : D2D1::ColorF(0.95f, 0.96f, 0.98f, alpha);
        D2D1_COLOR_F bdrCol = isHov
            ? ColorRefToD2D(m_config->btnHoverColor, alpha)
            : D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f * alpha);

        ID2D1SolidColorBrush* brBtnBg = nullptr;
        ID2D1SolidColorBrush* brBtnIc = nullptr;
        ID2D1SolidColorBrush* brBtnBdr = nullptr;
        rt->CreateSolidColorBrush(bgCol, &brBtnBg);
        rt->CreateSolidColorBrush(icCol, &brBtnIc);
        rt->CreateSolidColorBrush(bdrCol, &brBtnBdr);

        if (brBtnBg) rt->FillRoundedRectangle(D2D1::RoundedRect(rcBtn, 6.0f, 6.0f), brBtnBg);
        if (brBtnBdr) rt->DrawRoundedRectangle(D2D1::RoundedRect(rcBtn, 6.0f, 6.0f), brBtnBdr, 1.0f);
        if (brBtnIc) {
            float icx = (rcBtn.left + rcBtn.right) * 0.5f;
            float icy = (rcBtn.top + rcBtn.bottom) * 0.5f;
            float icSz = std::min(rcBtn.right - rcBtn.left, rcBtn.bottom - rcBtn.top) * 0.54f;
            ::DrawFaIcon(m_d2dFactory, rt, icon, icx, icy, icSz, brBtnIc, bgCol, m_layoutAnim);
        }

        if (brBtnBg) brBtnBg->Release();
        if (brBtnIc) brBtnIc->Release();
        if (brBtnBdr) brBtnBdr->Release();
    };

    // ==================== 1. Top Header Bar (2.0 Line-Height) + Top-Right Buttons ====================
    if (brGreen) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(padX + 5.0f, 18.0f), 4.0f, 4.0f), brGreen);
    }

    std::wstring shortCpu = snap.cpuName;
    size_t pPos = shortCpu.find(L" 8-Core");
    if (pPos == std::wstring::npos) pPos = shortCpu.find(L" Processor");
    if (pPos != std::wstring::npos) shortCpu = shortCpu.substr(0, pPos);
    std::wstring shortGpu = snap.gpuName;
    size_t gPos = shortGpu.find(L"NVIDIA ");
    if (gPos != std::wstring::npos) shortGpu = shortGpu.substr(gPos + 7);

    std::wstring headerStr = shortCpu + L"  •  " + shortGpu;
    D2D1_RECT_F rcHeader = D2D1::RectF(padX + 16.0f, 0.0f, w - padX - 72.0f, 36.0f);
    if (brMuted) {
        rt->PushAxisAlignedClip(rcHeader, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        rt->DrawText(headerStr.c_str(), (UINT32)headerStr.size(), fmtHeader, rcHeader, brMuted);
        rt->PopAxisAlignedClip();
    }

    // Top-right Settings & Click-Through Lock buttons (centered vertically in 36px header bar)
    m_btnSettingsRect = D2D1::RectF(w - padX - 62.0f, 6.0f, w - padX - 34.0f, 30.0f);
    m_btnLockRect     = D2D1::RectF(w - padX - 28.0f, 6.0f, w - padX,         30.0f);
    drawIconBtn(m_btnSettingsRect, FA_ICON_GEAR, 1, false);
    drawIconBtn(m_btnLockRect,     FA_ICON_LOCK, 3, false);

    // ==================== 2. Side-by-Side Docked Layout: Media Control Panel & 2x2 Hardware Grid ====================
    float gridTop = 40.0f; // 2.0 line-height header + 4px breathing gap
    float gap = 8.0f;
    float easeMedia = Smoothstep01(m_mediaAnim);
    float easeDock  = Smoothstep01(m_mediaDockAnim); // 0.0 = Media Left, 1.0 = Media Right

    float medCardW = 200.0f;
    float medAllocW = (medCardW + gap) * easeMedia;
    float totalBodyW = w - padX * 2.0f;
    float hwBodyW = std::max(216.0f, totalBodyW - medAllocW);
    float hwStartX = padX + medAllocW * (1.0f - easeDock);

    // -------------------- 2A. Expanded Media Control Panel (Full-height on Left or Right) --------------------
    if (easeMedia > 0.02f && !snap.mediaTitle.empty()) {
        float medAlpha = alpha * easeMedia;
        float medX_Left  = padX;
        float medX_Right = w - padX - medCardW;
        float mx = medX_Left + (medX_Right - medX_Left) * easeDock;
        float my = gridTop;
        float mh = 120.0f; // Matches 2 * 56 + 8 = 120px height of the 2x2 hardware grid!

        D2D1_RECT_F rcMedCard = D2D1::RectF(mx, my, mx + medCardW, my + mh);
        if (brCardBg) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcMedCard, cardRadius, cardRadius), brCardBg);
        }

        // Top row of Media Card: 40x40 Album Art + Source Pill / Switcher + Title Marquee + Artist
        D2D1_RECT_F rcArt = D2D1::RectF(mx + 8.0f, my + 8.0f, mx + 48.0f, my + 48.0f);
        DrawRoundedAlbumArt(rt, rcArt, 6.0f, medAlpha);

        // Source / Player Switcher Pill on top-right of Media Card
        m_btnMediaSwitchRect = D2D1::RectF(mx + 56.0f, my + 6.0f, mx + medCardW - 28.0f, my + 20.0f);
        bool hovSw = (m_hoverButtonId == 10);
        ID2D1SolidColorBrush* brWave = nullptr;
        ID2D1SolidColorBrush* brTitle = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(m_config->mediaWaveColor, medAlpha), &brWave);
        rt->CreateSolidColorBrush(ColorRefToD2D(m_config->mediaTitleColor, medAlpha), &brTitle);

        std::wstring srcLabel = snap.mediaSourceName;
        if (snap.mediaSessionCount > 1) {
            srcLabel += L" ⇄";
        }
        if (hovSw || snap.mediaSessionCount > 1) {
            rt->DrawText(srcLabel.c_str(), (UINT32)srcLabel.size(), fmtTime, m_btnMediaSwitchRect, hovSw ? brTitle : brWave);
        } else if (brWave) {
            rt->DrawText(srcLabel.c_str(), (UINT32)srcLabel.size(), fmtTime, m_btnMediaSwitchRect, brWave);
        }

        // Audio Peak Visualizer on top-right corner of Media Card (6 bars, 18px width)
        DrawAudioVisualizer(rt, mx + medCardW - 26.0f, my + 13.0f, 18.0f, 10.0f, ColorRefToD2D(m_config->mediaWaveColor, medAlpha));

        // Track Title Marquee
        D2D1_RECT_F rcTrack = D2D1::RectF(mx + 56.0f, my + 20.0f, mx + medCardW - 8.0f, my + 36.0f);
        DrawMarqueeText(rt, m_dwriteFactory, fmtCardTitle, snap.mediaTitle, rcTrack, brTitle, m_marqueeOffset);

        // Artist / Channel Name
        std::wstring artistStr = snap.mediaArtist.empty() ? L"正在播放媒體" : snap.mediaArtist;
        D2D1_RECT_F rcArtist = D2D1::RectF(mx + 56.0f, my + 36.0f, mx + medCardW - 8.0f, my + 50.0f);
        if (brMuted) {
            rt->PushAxisAlignedClip(rcArtist, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            rt->DrawText(artistStr.c_str(), (UINT32)artistStr.size(), fmtCardSub, rcArtist, brMuted);
            rt->PopAxisAlignedClip();
        }

        // Middle Row: Interactive Timeline Seek Bar (01:24 [====o----] 04:03)
        wchar_t posBuf[16], durBuf[16];
        swprintf(posBuf, 16, L"%02d:%02d", snap.mediaPosSec / 60, snap.mediaPosSec % 60);
        swprintf(durBuf, 16, L"%02d:%02d", snap.mediaDurSec / 60, snap.mediaDurSec % 60);

        float seekY = my + 60.0f;
        if (brMuted) {
            rt->DrawText(posBuf, (UINT32)wcslen(posBuf), fmtTime, D2D1::RectF(mx + 8.0f, seekY - 7.0f, mx + 36.0f, seekY + 7.0f), brMuted);
            fmtTime->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            rt->DrawText(durBuf, (UINT32)wcslen(durBuf), fmtTime, D2D1::RectF(mx + medCardW - 36.0f, seekY - 7.0f, mx + medCardW - 8.0f, seekY + 7.0f), brMuted);
            fmtTime->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        float barL = mx + 38.0f;
        float barR = mx + medCardW - 38.0f;
        float barW = std::max(10.0f, barR - barL);
        m_rectMediaSeekBar = D2D1::RectF(barL, seekY - 6.0f, barR, seekY + 6.0f);
        if (brBarBg) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barL, seekY - 2.0f, barR, seekY + 2.0f), 2.0f, 2.0f), brBarBg);
        }
        float prog = (snap.mediaDurSec > 0)
            ? std::clamp((float)snap.mediaPosSec / (float)snap.mediaDurSec, 0.0f, 1.0f)
            : 0.35f;
        if (brWave) {
            float fillX = barL + barW * prog;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barL, seekY - 2.0f, fillX, seekY + 2.0f), 2.0f, 2.0f), brWave);
            float knobR = (m_hoverButtonId == 16) ? 4.5f : 3.2f;
            if ( brTitle ) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(fillX, seekY), knobR, knobR), brTitle);
        }

        // Bottom Row: 5 Playback Control Buttons (Prev, -10s, Play/Pause, +10s, Next) on 4px grid!
        float ctrlY = my + 84.0f;
        float ctrlH = 28.0f;
        float ctrlW = 32.0f;
        float ctrlGap = 6.0f;
        float totalCtrlW = ctrlW * 5.0f + ctrlGap * 4.0f;
        float ctrlStartX = mx + (medCardW - totalCtrlW) * 0.5f;

        m_btnMediaPrevRect = D2D1::RectF(ctrlStartX + 0 * (ctrlW + ctrlGap), ctrlY, ctrlStartX + 0 * (ctrlW + ctrlGap) + ctrlW, ctrlY + ctrlH);
        m_btnMediaRewRect  = D2D1::RectF(ctrlStartX + 1 * (ctrlW + ctrlGap), ctrlY, ctrlStartX + 1 * (ctrlW + ctrlGap) + ctrlW, ctrlY + ctrlH);
        m_btnMediaPlayRect = D2D1::RectF(ctrlStartX + 2 * (ctrlW + ctrlGap), ctrlY, ctrlStartX + 2 * (ctrlW + ctrlGap) + ctrlW, ctrlY + ctrlH);
        m_btnMediaFfwdRect = D2D1::RectF(ctrlStartX + 3 * (ctrlW + ctrlGap), ctrlY, ctrlStartX + 3 * (ctrlW + ctrlGap) + ctrlW, ctrlY + ctrlH);
        m_btnMediaNextRect = D2D1::RectF(ctrlStartX + 4 * (ctrlW + ctrlGap), ctrlY, ctrlStartX + 4 * (ctrlW + ctrlGap) + ctrlW, ctrlY + ctrlH);

        drawIconBtn(m_btnMediaPrevRect, FA_ICON_STEP_BACK, 11, false);
        drawIconBtn(m_btnMediaRewRect,  FA_ICON_BACKWARD,  12, false);
        drawIconBtn(m_btnMediaPlayRect, snap.mediaIsPlaying ? FA_ICON_PAUSE : FA_ICON_PLAY, 13, true);
        drawIconBtn(m_btnMediaFfwdRect, FA_ICON_FORWARD,   14, false);
        drawIconBtn(m_btnMediaNextRect, FA_ICON_STEP_FWD,  15, false);

        if (brWave) brWave->Release();
        if (brTitle) brTitle->Release();
    } else {
        m_btnMediaSwitchRect = m_btnMediaPrevRect = m_btnMediaRewRect = m_btnMediaPlayRect = m_btnMediaFfwdRect = m_btnMediaNextRect = m_rectMediaSeekBar = {};
    }

    // -------------------- 2B. 2x2 Hardware Telemetry Cards --------------------
    float cardW = (hwBodyW - gap) * 0.5f;
    float cardH = 56.0f;

    float cardSlot1W = std::max(
        MeasureTextWidth(m_dwriteFactory, fmtCardBig, L"100%"),
        MeasureTextWidth(m_dwriteFactory, fmtCardBig, L"9999")
    ) + 2.0f;
    float cardSlot2W = MeasureTextWidth(m_dwriteFactory, fmtCardBig, L"99°C") + 2.0f;

    struct CardInfo {
        FaIconType icon;
        std::wstring title;
        COLORREF titleCol;
        COLORREF val1Col;
        COLORREF val2Col;
        wchar_t val1[32];
        wchar_t val2[32];
        bool hasTemp;
        wchar_t subVal[64];
        float pct;
    };

    CardInfo rawCards[4] = {};
    rawCards[0].icon = FA_ICON_CPU;
    rawCards[0].title = m_config->metrics[0].title;
    rawCards[0].titleCol = m_config->metrics[0].titleColor;
    rawCards[0].val1Col = m_config->metrics[0].valueColor;
    rawCards[0].val2Col = (snap.cpuTempC >= m_config->tempCritC) ? m_config->critColor :
                          (snap.cpuTempC >= m_config->tempWarnC) ? m_config->warnColor : m_config->metrics[0].valueColor;
    rawCards[0].hasTemp = true;
    swprintf(rawCards[0].val1, 32, L"%d%%", snap.cpuUsagePct);
    if (snap.cpuTempC > 0) {
        swprintf(rawCards[0].val2, 32, L"%d°C", snap.cpuTempC);
    } else {
        swprintf(rawCards[0].val2, 32, L"--");
    }
    if (snap.cpuPowerW > 1.0f) {
        swprintf(rawCards[0].subVal, 64, L"%.2f GHz  •  %.0f W", snap.cpuClockGhz, snap.cpuPowerW);
    } else {
        swprintf(rawCards[0].subVal, 64, L"%.2f GHz", snap.cpuClockGhz);
    }
    rawCards[0].pct = snap.cpuUsagePct / 100.0f;

    rawCards[1].icon = FA_ICON_GPU;
    std::wstring gpuCardTitle = m_config->metrics[1].title;
    if (m_config->gpuTarget == 1) {
        gpuCardTitle += L" (AMD)";
    } else if (m_config->gpuTarget == 2) {
        gpuCardTitle += L" (NV)";
    } else {
        gpuCardTitle += (snap.gpuName.find(L"AMD") != std::wstring::npos) ? L" (AMD)" : L" (NV)";
    }
    rawCards[1].title = gpuCardTitle;
    rawCards[1].titleCol = m_config->metrics[1].titleColor;
    rawCards[1].val1Col = m_config->metrics[1].valueColor;
    rawCards[1].val2Col = (snap.gpuTempC >= m_config->tempCritC) ? m_config->critColor :
                          (snap.gpuTempC >= m_config->tempWarnC) ? m_config->warnColor : m_config->metrics[1].valueColor;
    rawCards[1].hasTemp = true;
    swprintf(rawCards[1].val1, 32, L"%d%%", snap.gpuUsagePct);
    if (snap.gpuTempC > 0) {
        swprintf(rawCards[1].val2, 32, L"%d°C", snap.gpuTempC);
    } else {
        swprintf(rawCards[1].val2, 32, L"--");
    }
    if (snap.gpuClockMhz > 100.0f) {
        swprintf(rawCards[1].subVal, 64, L"VRAM %.1f/%.0fG • %.0fW • %.0fMHz", snap.gpuVramUsedGb, snap.gpuVramTotalGb, snap.gpuPowerW, snap.gpuClockMhz);
    } else {
        swprintf(rawCards[1].subVal, 64, L"VRAM %.1f/%.0fG • %.0fW", snap.gpuVramUsedGb, snap.gpuVramTotalGb, snap.gpuPowerW);
    }
    rawCards[1].pct = snap.gpuUsagePct / 100.0f;

    rawCards[2].icon = FA_ICON_RAM;
    rawCards[2].title = m_config->metrics[2].title;
    rawCards[2].titleCol = m_config->metrics[2].titleColor;
    rawCards[2].val1Col = m_config->metrics[2].valueColor;
    rawCards[2].hasTemp = false;
    swprintf(rawCards[2].val1, 32, L"%d%%", snap.ramUsagePct);
    swprintf(rawCards[2].subVal, 64, L"已用 %.1f / %.1f GB", snap.ramUsedGb, snap.ramTotalGb);
    rawCards[2].pct = snap.ramUsagePct / 100.0f;

    rawCards[3].icon = FA_ICON_FPS;
    rawCards[3].title = m_config->metrics[3].title;
    rawCards[3].titleCol = m_config->metrics[3].titleColor;
    rawCards[3].val1Col = m_config->metrics[3].valueColor;
    rawCards[3].hasTemp = false;
    if (snap.currentFps > 0) {
        swprintf(rawCards[3].val1, 32, L"%d", snap.currentFps);
        swprintf(rawCards[3].subVal, 64, L"3D 遊戲即時幀率 (%dHz)", snap.monitorRefreshHz);
        rawCards[3].pct = std::min(1.0f, (float)snap.currentFps / (float)std::max(60, snap.monitorRefreshHz));
    } else {
        if (m_config->fpsIdleMode == 1)
            swprintf(rawCards[3].val1, 32, L"%dHz", snap.monitorRefreshHz);
        else
            swprintf(rawCards[3].val1, 32, L"--");
        swprintf(rawCards[3].subVal, 64, L"桌面待機 (%dHz 螢幕)", snap.monitorRefreshHz);
        rawCards[3].pct = 0.15f;
    }

    for (int pos = 0; pos < 4; ++pos) {
        int mIdx = m_config->itemOrder[pos];
        if (mIdx < 0 || mIdx >= 4) mIdx = pos;
        const CardInfo& card = rawCards[mIdx];

        int r = pos / 2;
        int c = pos % 2;
        float x = hwStartX + c * (cardW + gap);
        float y = gridTop + r * (cardH + gap);

        D2D1_RECT_F rcCard = D2D1::RectF(x, y, x + cardW, y + cardH);
        if (mIdx == 1) {
            m_rectGpuCard = rcCard;
        }
        if (brCardBg) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard, cardRadius, cardRadius), brCardBg);
        }

        ID2D1SolidColorBrush* brTitle = nullptr;
        ID2D1SolidColorBrush* brVal1 = nullptr;
        ID2D1SolidColorBrush* brVal2 = nullptr;
        rt->CreateSolidColorBrush(ColorRefToD2D(card.titleCol, alpha), &brTitle);
        rt->CreateSolidColorBrush(ColorRefToD2D(card.val1Col, alpha), &brVal1);
        rt->CreateSolidColorBrush(ColorRefToD2D(card.val2Col, alpha), &brVal2);

        if (brTitle) {
            DrawFaIcon(rt, card.icon, x + 14.0f, y + 14.0f, 12.0f, brTitle);
            D2D1_RECT_F rcT = D2D1::RectF(x + 24.0f, y + 4.0f, x + cardW * 0.52f, y + 24.0f);
            rt->DrawText(card.title.c_str(), (UINT32)card.title.size(), fmtCardTitle, rcT, brTitle);
        }

        float rightEnd = x + cardW - 8.0f;
        if (card.hasTemp) {
            D2D1_RECT_F rcSlot2 = D2D1::RectF(rightEnd - cardSlot2W, y + 4.0f, rightEnd, y + 24.0f);
            D2D1_RECT_F rcSlot1 = D2D1::RectF(rightEnd - cardSlot2W - 4.0f - cardSlot1W, y + 4.0f, rightEnd - cardSlot2W - 4.0f, y + 24.0f);
            if (brVal1) rt->DrawText(card.val1, (UINT32)wcslen(card.val1), fmtCardBig, rcSlot1, brVal1);
            if (brVal2) rt->DrawText(card.val2, (UINT32)wcslen(card.val2), fmtCardBig, rcSlot2, brVal2);
        } else {
            D2D1_RECT_F rcSlot1 = D2D1::RectF(rightEnd - cardSlot1W, y + 4.0f, rightEnd, y + 24.0f);
            if (brVal1) rt->DrawText(card.val1, (UINT32)wcslen(card.val1), fmtCardBig, rcSlot1, brVal1);
        }

        D2D1_RECT_F rcS = D2D1::RectF(x + 8.0f, y + 24.0f, x + cardW - 8.0f, y + 44.0f);
        if (brMuted) rt->DrawText(card.subVal, (UINT32)wcslen(card.subVal), fmtCardSub, rcS, brMuted);

        float barX = x + 8.0f;
        float barY = y + cardH - 8.0f;
        float barW = cardW - 16.0f;
        float barH = 4.0f;
        if (brBarBg) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + barW, barY + barH), 2.0f, 2.0f), brBarBg);
        }
        if (brTitle) {
            float fillW = std::clamp(card.pct, 0.04f, 1.0f) * barW;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(barX, barY, barX + fillW, barY + barH), 2.0f, 2.0f), brTitle);
        }

        if (brTitle) brTitle->Release();
        if (brVal1) brVal1->Release();
        if (brVal2) brVal2->Release();
    }

    if (brGreen) brGreen->Release();
    if (brMuted) brMuted->Release();
    if (brCardBg) brCardBg->Release();
    if (brBarBg) brBarBg->Release();

    fmtHeader->Release();
    fmtCardTitle->Release();
    fmtCardBig->Release();
    fmtCardSub->Release();
    fmtTime->Release();
}

void NotchWindow::RenderToTarget(ID2D1RenderTarget* rt, float canvasLogW, float drawW, float drawH, float expandRatio, float scale) {
    float offsetX = (canvasLogW - drawW) * 0.5f;

    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Translation(offsetX, 0.0f) * D2D1::Matrix3x2F::Scale(scale, scale));
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    float bgAlpha = std::clamp(m_config->bgOpacity / 255.0f, 0.35f, 1.0f);
    D2D1_COLOR_F fillCol = ColorRefToD2D(m_config->bgColor, bgAlpha);
    D2D1_COLOR_F borderCol = ColorRefToD2D(m_config->borderColor, 0.75f);

    float easeExp = Smoothstep01(expandRatio);
    float colRadius = (float)m_config->cornerRadius;
    float expRadius = std::max(colRadius, 16.0f + 8.0f);
    float curRadius = colRadius + (expRadius - colRadius) * easeExp;
    DrawNotchShape(rt, drawW, drawH, curRadius, fillCol, borderCol);

    float colAlpha = std::clamp(1.0f - (easeExp * 2.0f), 0.0f, 1.0f);
    float expAlpha = std::clamp((easeExp - 0.30f) * 1.43f, 0.0f, 1.0f);

    if (colAlpha > 0.01f) {
        DrawCollapsedContent(rt, drawW, drawH, colAlpha);
    }
    if (expAlpha > 0.01f) {
        DrawExpandedContent(rt, drawW, drawH, expAlpha);
    }

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->EndDraw();
}

void NotchWindow::RenderAndPresent() {
    if (!m_hwnd || !m_dcRenderTarget || !m_config) return;

    float scale = m_config->GetEffectiveScale();
    float wDual = 260.0f, hDual = 36.0f, wSingle = 396.0f, hSingle = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDual, hDual, wSingle, hSingle);

    // Pre-allocate a stable canvas large enough for Dual-Row, Single-Row, and Expanded + Media states
    float maxLogW = std::max({ wDual + 216.0f, wSingle + 216.0f, (float)m_config->expandedWidth + 216.0f, m_currW }) + 24.0f;
    float maxLogH = std::max({ hDual, hSingle, (float)m_config->expandedHeight, m_currH }) + 8.0f;

    int canvasW = (((int)std::ceil(maxLogW * scale) + 3) / 4) * 4;
    int canvasH = (((int)std::ceil(maxLogH * scale) + 3) / 4) * 4;
    EnsureCanvas(canvasW, canvasH);
    if (!m_memDC || !m_dibBmp || !m_dibBits) return;

    float canvasLogW = (float)m_canvasW / scale;
    float drawW = m_currW + 16.0f;
    float drawH = m_currH;

    RECT rcSub = { 0, 0, m_canvasW, m_canvasH };
    if (SUCCEEDED(m_dcRenderTarget->BindDC(m_memDC, &rcSub))) {
        RenderToTarget(m_dcRenderTarget, canvasLogW, drawW, drawH, m_animProgress, scale);
    }

    MonitorEntry mon = GetTargetMonitor(m_config->targetMonitor, m_config->targetMonitorDevice);
    int monW = mon.rcMonitor.right - mon.rcMonitor.left;
    int monLeft = mon.rcMonitor.left;
    int monTop = mon.rcMonitor.top;

    int maxOffset = std::max(0, monW - m_canvasW);
    float pct = std::clamp((float)m_config->positionOffsetPct / 100.0f, 0.0f, 1.0f);
    int posX = monLeft + (int)std::round(pct * (float)maxOffset);
    int posY = monTop;

    HDC hdcScreen = GetDC(NULL);
    POINT ptDst = { posX, posY };
    SIZE szWin = { m_canvasW, m_canvasH };
    POINT ptSrc = { 0, 0 };
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(m_hwnd, hdcScreen, &ptDst, &szWin, m_memDC, &ptSrc, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);
}

bool NotchWindow::SavePreviewBmp(const std::wstring& bmpPath, bool expandedState) {
    if (!m_dcRenderTarget || !m_config) return false;

    m_layoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
    m_mediaDockAnim = (m_config->mediaDockSide == 1) ? 1.0f : 0.0f;
    bool hasMed = (m_telemetry && m_config->mediaEnabled && m_telemetry->GetSnapshot().hasMediaSession);
    m_mediaAnim = hasMed ? 1.0f : 0.0f;

    float easeLayout = Smoothstep01(m_layoutAnim);
    float easeMedia  = Smoothstep01(m_mediaAnim);
    float scale = m_config->GetEffectiveScale();

    float baseW = m_config->GetCollapsedLogicalWidth() + (204.0f + 20.0f * easeLayout) * easeMedia;
    float baseH = m_config->GetCollapsedLogicalHeight();
    float expW  = (easeMedia > 0.02f || m_config->layoutMode == 1) ? baseW : (float)m_config->expandedWidth;
    float expH  = (float)m_config->expandedHeight;

    int logW = (((int)(expandedState ? expW : baseW) + 16 + 3) / 4) * 4;
    int logH = (((int)(expandedState ? expH : baseH) + 3) / 4) * 4;
    int w = (((int)std::round(logW * scale) + 2) / 4) * 4;
    int h = (((int)std::round(logH * scale) + 2) / 4) * 4;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HBITMAP hbm = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    bool ok = false;
    if (hbm && pvBits) {
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);

        RECT rcFull = { 0, 0, w, h };
        if (SUCCEEDED(m_dcRenderTarget->BindDC(hdcMem, &rcFull))) {
            m_dcRenderTarget->BeginDraw();
            m_dcRenderTarget->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
            m_dcRenderTarget->Clear(D2D1::ColorF(0.14f, 0.17f, 0.24f, 1.0f));
            float bgAlpha = std::clamp(m_config->bgOpacity / 255.0f, 0.35f, 1.0f);
            float colRadius = (float)m_config->cornerRadius;
            float expRadius = std::max(colRadius, 16.0f + 8.0f);
            float radius = expandedState ? expRadius : colRadius;
            DrawNotchShape(m_dcRenderTarget, (float)logW, (float)logH, radius,
                           ColorRefToD2D(m_config->bgColor, bgAlpha),
                           ColorRefToD2D(m_config->borderColor, 0.85f));
            if (expandedState) {
                DrawExpandedContent(m_dcRenderTarget, (float)logW, (float)logH, 1.0f);
            } else {
                DrawCollapsedContent(m_dcRenderTarget, (float)logW, (float)logH, 1.0f);
            }
            m_dcRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            m_dcRenderTarget->EndDraw();
        }

        FILE* fp = _wfopen(bmpPath.c_str(), L"wb");
        if (fp) {
            DWORD imgSize = (DWORD)(w * h * 4);
            BITMAPFILEHEADER bfh = {};
            bfh.bfType = 0x4D42;
            bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            bfh.bfSize = bfh.bfOffBits + imgSize;

            BITMAPINFOHEADER bih = bmi.bmiHeader;
            fwrite(&bfh, sizeof(bfh), 1, fp);
            fwrite(&bih, sizeof(bih), 1, fp);
            fwrite(pvBits, 1, imgSize, fp);
            fclose(fp);
            ok = true;
        }

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbm);
    }

    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return ok;
}

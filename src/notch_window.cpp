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
    if (m_dcRenderTarget) { m_dcRenderTarget->Release(); m_dcRenderTarget = nullptr; }
    if (m_dwriteFactory) { m_dwriteFactory->Release(); m_dwriteFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
}

static void GetDualAndSingleLogicalSize(const AppConfig* cfg, float& outDualW, float& outDualH, float& outSingleW, float& outSingleH) {
    float baseW = (float)((cfg->notchWidth / 4) * 4);
    int active = cfg->GetActiveMetricCount();
    float sW = baseW;
    if (active >= 4) sW = cfg->notchWidth * 1.52f;
    else if (active == 3) sW = cfg->notchWidth * 1.24f;
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
    m_currW = m_config->GetCollapsedLogicalWidth();
    m_currH = m_config->GetCollapsedLogicalHeight();
    QueryPerformanceCounter(&m_lastTick);

    float scale = m_config->GetEffectiveScale();
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int logW = (((int)std::ceil(m_currW) + 16 + 3) / 4) * 4;
    int logH = (((int)std::ceil(m_currH) + 3) / 4) * 4;
    int winW = (((int)std::round(logW * scale) + 2) / 4) * 4;
    int winH = (((int)std::round(logH * scale) + 2) / 4) * 4;
    int posX = (scrW - winW) / 2;

    DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE;
    if (m_config->clickThroughLocked) {
        exStyle |= WS_EX_TRANSPARENT;
    }

    m_hwnd = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        L"RCNotchMonitor",
        WS_POPUP,
        posX, 0, winW, winH,
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
    // Do NOT snap m_currW / m_currH or m_layoutAnim here so OnTimerTick() smoothly tweens any layout/size change!
    QueryPerformanceCounter(&m_lastTick);
    KillTimer(m_hwnd, TIMER_DATA_ID);
    SetTimer(m_hwnd, TIMER_DATA_ID, m_config->refreshIntervalMs, NULL);
    if (m_telemetry) {
        m_telemetry->SetIntervalMs(m_config->refreshIntervalMs);
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

        float scale = m_config->GetEffectiveScale();
        float canvasLogW = (m_canvasW > 0) ? ((float)m_canvasW / scale) : (m_currW + 16.0f);
        float drawW = m_currW + 16.0f;
        float offsetX = (canvasLogW - drawW) * 0.5f;

        float mx = (float)GET_X_LPARAM(lParam) / scale - offsetX;
        float my = (float)GET_Y_LPARAM(lParam) / scale;
        int oldBtn = m_hoverButtonId;
        m_hoverButtonId = 0;
        if (m_animProgress > 0.65f) {
            if (PointInRectF(mx, my, m_btnSettingsRect)) m_hoverButtonId = 1;
            else if (PointInRectF(mx, my, m_btnLayoutRect)) m_hoverButtonId = 2;
            else if (PointInRectF(mx, my, m_btnLockRect)) m_hoverButtonId = 3;
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

    case WM_MOUSELEAVE:
        m_trackingMouse = false;
        if (m_isHovered) {
            QueryPerformanceCounter(&m_lastTick);
            m_isHovered = false;
        }
        m_hoverButtonId = 0;
        return 0;

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
            } else if (PointInRectF(mx, my, m_btnLayoutRect)) {
                QueryPerformanceCounter(&m_lastTick);
                m_config->layoutMode = (m_config->layoutMode == 0) ? 1 : 0;
                m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
                if (onConfigChanged) onConfigChanged();
                RenderAndPresent();
                return 0;
            } else if (PointInRectF(mx, my, m_btnLockRect)) {
                SetClickThrough(true);
                m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
                if (onConfigChanged) onConfigChanged();
                RenderAndPresent();
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

    if (m_isHovered && !m_config->clickThroughLocked) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            float scale = m_config->GetEffectiveScale();
            float actualPhysW = (m_currW + 16.0f) * scale;
            float actualPhysH = m_currH * scale;
            float scrCenterX = GetSystemMetrics(SM_CXSCREEN) * 0.5f;
            float left = scrCenterX - actualPhysW * 0.5f - 6.0f;
            float right = scrCenterX + actualPhysW * 0.5f + 6.0f;
            float bottom = actualPhysH + 6.0f;
            if (pt.x < (LONG)left || pt.x > (LONG)right || pt.y < 0 || pt.y > (LONG)bottom) {
                m_isHovered = false;
                m_hoverButtonId = 0;
            }
        }
    }

    float targetProgress = (m_isHovered && !m_config->clickThroughLocked) ? 1.0f : 0.0f;
    float targetLayoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;

    bool changed = false;

    // 1. Advance Expand/Collapse progress (0.22s duration -> ~28 frames at 120Hz, shaped by Smoothstep01)
    if (std::fabs(m_animProgress - targetProgress) > 0.002f) {
        float step = dt / 0.22f;
        if (m_animProgress < targetProgress)
            m_animProgress = std::min(targetProgress, m_animProgress + step);
        else
            m_animProgress = std::max(targetProgress, m_animProgress - step);
        changed = true;
    } else if (m_animProgress != targetProgress) {
        m_animProgress = targetProgress;
        changed = true;
    }

    // 2. Advance Single-Row <-> Dual-Row progress (0.26s duration -> ~32 frames at 120Hz, shaped by Smoothstep01)
    if (std::fabs(m_layoutAnim - targetLayoutAnim) > 0.002f) {
        float step = dt / 0.26f;
        if (m_layoutAnim < targetLayoutAnim)
            m_layoutAnim = std::min(targetLayoutAnim, m_layoutAnim + step);
        else
            m_layoutAnim = std::max(targetLayoutAnim, m_layoutAnim - step);
        changed = true;
    } else if (m_layoutAnim != targetLayoutAnim) {
        m_layoutAnim = targetLayoutAnim;
        changed = true;
    }

    // 3. Compute exact target width & height from Smoothstep(m_layoutAnim) and Smoothstep(m_animProgress)
    float wDual = 260.0f, hDual = 36.0f, wSingle = 396.0f, hSingle = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDual, hDual, wSingle, hSingle);

    float easeLayout = Smoothstep01(m_layoutAnim);
    float easeExpand = Smoothstep01(m_animProgress);

    float collapsedW = wDual + (wSingle - wDual) * easeLayout;
    float collapsedH = hDual + (hSingle - hDual) * easeLayout;

    float desiredW = collapsedW + ((float)m_config->expandedWidth - collapsedW) * easeExpand;
    float desiredH = collapsedH + ((float)m_config->expandedHeight - collapsedH) * easeExpand;

    if (std::fabs(m_currW - desiredW) > 0.05f || std::fabs(m_currH - desiredH) > 0.05f) {
        // When m_animProgress or m_layoutAnim is actively driving the transition, follow desiredW/H directly;
        // if user adjusted base width/height sliders, smooth with exponential decay:
        if (std::fabs(m_animProgress - targetProgress) > 0.001f || std::fabs(m_layoutAnim - targetLayoutAnim) > 0.001f) {
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
    if (SUCCEEDED(dwFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(), fmt, 500.0f, 50.0f, &layout))) {
        DWRITE_TEXT_METRICS tm = {};
        if (SUCCEEDED(layout->GetMetrics(&tm))) {
            w = tm.widthIncludingTrailingWhitespace;
        }
        layout->Release();
    }
    return w;
}

void NotchWindow::DrawCollapsedContent(ID2D1RenderTarget* rt, float w, float h, float alpha) {
    if (alpha <= 0.02f || !m_config || !m_telemetry) return;

    TelemetrySnapshot snap = m_telemetry->GetSnapshot();

    std::vector<int> activeItems;
    for (int i = 0; i < 4; ++i) {
        int idx = m_config->itemOrder[i];
        if (idx >= 0 && idx < 4 && m_config->metrics[idx].enabled) {
            activeItems.push_back(idx);
        }
    }
    if (activeItems.empty()) return;

    int count = (int)activeItems.size();
    // Smooth interpolation factor between Dual-Row (0.0) and Single-Row (1.0)
    float layoutT = (count <= 2) ? 1.0f : Smoothstep01(m_layoutAnim);

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
                swprintf(buf2, 32, L"%d°C", snap.cpuTempC);
                if (snap.cpuTempC >= m_config->tempCritC) items[i].val2Col = m_config->critColor;
                else if (snap.cpuTempC >= m_config->tempWarnC) items[i].val2Col = m_config->warnColor;
            }
        } else if (mIdx == 1) { // GPU
            swprintf(buf1, 32, L"%d%%", snap.gpuUsagePct);
            if (mc.showTemp) {
                items[i].hasTemp = true;
                swprintf(buf2, 32, L"%d°C", snap.gpuTempC);
                if (snap.gpuTempC >= m_config->tempCritC) items[i].val2Col = m_config->critColor;
                else if (snap.gpuTempC >= m_config->tempWarnC) items[i].val2Col = m_config->warnColor;
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

    float padX = 20.0f;
    float innerH = h - 4.0f;
    float titleValGap = 4.0f;
    float tempSlotGap = 4.0f;

    // Reference logical widths for Dual-Row and Single-Row so font scaling is monotonic during tweening
    float wDualRef = 260.0f, hDualRef = 36.0f, wSingleRef = 396.0f, hSingleRef = 28.0f;
    GetDualAndSingleLogicalSize(m_config, wDualRef, hDualRef, wSingleRef, hSingleRef);
    float fullW_Dual = wDualRef + 16.0f;
    float fullW_Single = wSingleRef + 16.0f;
    float innerW_Dual = fullW_Dual - padX * 2.0f;
    float innerW_Single = fullW_Single - padX * 2.0f;

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

    // Check if either Single-Row or Dual-Row needs font shrink for long custom titles
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

    // 1. Compute Single-Row target coordinates relative to horizontal center
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

    // 2. Compute Dual-Row target coordinates relative to horizontal center
    int rowsDual = (count + 1) / 2;
    float cellH_Dual = innerH / (float)rowsDual;
    std::vector<float> dxDual(count), yDual(count), hDual(count), titleSlotDual(count);
    for (int i = 0; i < count; ++i) {
        int r = i / 2;
        int c = i % 2;
        float xInDual = padX + c * cellW_Dual + 4.0f;
        dxDual[i] = xInDual - fullW_Dual * 0.5f;
        yDual[i] = 1.0f + r * cellH_Dual;
        hDual[i] = cellH_Dual;
        titleSlotDual[i] = colMaxTitleW[c];
    }

    // 3. Render each item at the smoothly interpolated (Dual <-> Single) coordinates!
    float centerX = w * 0.5f;
    for (int i = 0; i < count; ++i) {
        float curX = centerX + dxDual[i] + (dxSingle[i] - dxDual[i]) * layoutT;
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

    IDWriteTextFormat* fmtHeader = nullptr;
    IDWriteTextFormat* fmtCardTitle = nullptr;
    IDWriteTextFormat* fmtCardBig = nullptr;
    IDWriteTextFormat* fmtCardSub = nullptr;
    IDWriteTextFormat* fmtTip = nullptr;

    m_dwriteFactory->CreateTextFormat(L"Segoe UI Semibold", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"zh-TW", &fmtHeader);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-TW", &fmtCardTitle);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-TW", &fmtCardBig);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 9.8f, L"zh-TW", &fmtCardSub);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"zh-TW", &fmtTip);

    if (!fmtHeader || !fmtCardTitle || !fmtCardBig || !fmtCardSub || !fmtTip) return;

    fmtCardBig->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtTip->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    float padX = 20.0f;

    ID2D1SolidColorBrush* brGreen = nullptr;
    ID2D1SolidColorBrush* brMuted = nullptr;
    ID2D1SolidColorBrush* brCardBg = nullptr;
    ID2D1SolidColorBrush* brBarBg = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.29f, 0.87f, 0.50f, alpha), &brGreen);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.71f, 0.80f, alpha), &brMuted);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.055f * alpha), &brCardBg);
    rt->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f * alpha), &brBarBg);

    if (brGreen) {
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(padX + 4.0f, 12.0f), 4.0f, 4.0f), brGreen);
    }

    std::wstring shortCpu = snap.cpuName;
    size_t pPos = shortCpu.find(L" 8-Core");
    if (pPos == std::wstring::npos) pPos = shortCpu.find(L" Processor");
    if (pPos != std::wstring::npos) shortCpu = shortCpu.substr(0, pPos);
    std::wstring shortGpu = snap.gpuName;
    size_t gPos = shortGpu.find(L"NVIDIA ");
    if (gPos != std::wstring::npos) shortGpu = shortGpu.substr(gPos + 7);

    std::wstring headerStr = shortCpu + L"  •  " + shortGpu;
    D2D1_RECT_F rcHeader = D2D1::RectF(padX + 12.0f, 4.0f, w - padX, 20.0f);
    if (brMuted) rt->DrawText(headerStr.c_str(), (UINT32)headerStr.size(), fmtHeader, rcHeader, brMuted);

    // 2x2 Hardware Telemetry Cards (4px grid + fixed 4-char right-aligned slots!)
    float gridTop = 24.0f;
    float gap = 8.0f;
    float cardW = (w - padX * 2.0f - gap) * 0.5f;
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
    swprintf(rawCards[0].val2, 32, L"%d°C", snap.cpuTempC);
    swprintf(rawCards[0].subVal, 64, L"%.2f GHz  •  %.0f W", snap.cpuClockGhz, snap.cpuPowerW);
    rawCards[0].pct = snap.cpuUsagePct / 100.0f;

    rawCards[1].icon = FA_ICON_GPU;
    rawCards[1].title = m_config->metrics[1].title;
    rawCards[1].titleCol = m_config->metrics[1].titleColor;
    rawCards[1].val1Col = m_config->metrics[1].valueColor;
    rawCards[1].val2Col = (snap.gpuTempC >= m_config->tempCritC) ? m_config->critColor :
                          (snap.gpuTempC >= m_config->tempWarnC) ? m_config->warnColor : m_config->metrics[1].valueColor;
    rawCards[1].hasTemp = true;
    swprintf(rawCards[1].val1, 32, L"%d%%", snap.gpuUsagePct);
    swprintf(rawCards[1].val2, 32, L"%d°C", snap.gpuTempC);
    swprintf(rawCards[1].subVal, 64, L"VRAM %.1f/%.0fG • %.0fW", snap.gpuVramUsedGb, snap.gpuVramTotalGb, snap.gpuPowerW);
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
        float x = padX + c * (cardW + gap);
        float y = gridTop + r * (cardH + gap);

        D2D1_RECT_F rcCard = D2D1::RectF(x, y, x + cardW, y + cardH);
        if (brCardBg) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard, 8.0f, 8.0f), brCardBg);
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

    // 3. Bottom Bar: 3 Centered Font Awesome Icon Buttons (4px grid!)
    float btnY = gridTop + 2 * (cardH + gap) + 4.0f;
    float btnH = 24.0f;
    float iconBtnW = 40.0f;
    float btnGap = 12.0f;

    float totalBtnsW = iconBtnW * 3.0f + btnGap * 2.0f;
    float startX = (w - totalBtnsW) * 0.5f;

    m_btnSettingsRect = D2D1::RectF(startX, btnY, startX + iconBtnW, btnY + btnH);
    m_btnLayoutRect   = D2D1::RectF(startX + iconBtnW + btnGap, btnY, startX + iconBtnW * 2.0f + btnGap, btnY + btnH);
    m_btnLockRect     = D2D1::RectF(startX + (iconBtnW + btnGap) * 2.0f, btnY, startX + iconBtnW * 3.0f + btnGap * 2.0f, btnY + btnH);

    const D2D1_RECT_F btnRects[3] = { m_btnSettingsRect, m_btnLayoutRect, m_btnLockRect };
    const FaIconType btnIcons[3] = { FA_ICON_GEAR, FA_ICON_LAYOUT, FA_ICON_LOCK };

    for (int b = 0; b < 3; ++b) {
        bool isHov = (m_hoverButtonId == (b + 1));
        COLORREF rawBg = isHov ? m_config->btnHoverColor : m_config->btnColor;
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

        if (brBtnBg) rt->FillRoundedRectangle(D2D1::RoundedRect(btnRects[b], 6.0f, 6.0f), brBtnBg);
        if (brBtnBdr) rt->DrawRoundedRectangle(D2D1::RoundedRect(btnRects[b], 6.0f, 6.0f), brBtnBdr, 1.0f);
        if (brBtnIc) {
            float icx = (btnRects[b].left + btnRects[b].right) * 0.5f;
            float icy = (btnRects[b].top + btnRects[b].bottom) * 0.5f;
            ::DrawFaIcon(m_d2dFactory, rt, btnIcons[b], icx, icy, 12.5f, brBtnIc, bgCol, m_layoutAnim);
        }

        if (brBtnBg) brBtnBg->Release();
        if (brBtnIc) brBtnIc->Release();
        if (brBtnBdr) brBtnBdr->Release();
    }

    if (brGreen) brGreen->Release();
    if (brMuted) brMuted->Release();
    if (brCardBg) brCardBg->Release();
    if (brBarBg) brBarBg->Release();

    fmtHeader->Release();
    fmtCardTitle->Release();
    fmtCardBig->Release();
    fmtCardSub->Release();
    fmtTip->Release();
}

void NotchWindow::RenderToTarget(ID2D1RenderTarget* rt, float canvasLogW, float drawW, float drawH, float expandRatio, float scale) {
    float offsetX = (canvasLogW - drawW) * 0.5f;

    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Translation(offsetX, 0.0f) * D2D1::Matrix3x2F::Scale(scale, scale));
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    float bgAlpha = std::clamp(m_config->bgOpacity / 255.0f, 0.35f, 1.0f);
    D2D1_COLOR_F fillCol = ColorRefToD2D(m_config->bgColor, bgAlpha);
    D2D1_COLOR_F borderCol = ColorRefToD2D(m_config->borderColor, 0.75f);

    DrawNotchShape(rt, drawW, drawH, (float)m_config->cornerRadius, fillCol, borderCol);

    float easeExp = Smoothstep01(expandRatio);
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

    // Pre-allocate a stable canvas large enough for Dual-Row, Single-Row, and Expanded states
    float maxLogW = std::max({ wDual, wSingle, (float)m_config->expandedWidth, m_currW }) + 24.0f;
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

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int posX = (scrW - m_canvasW) / 2;

    HDC hdcScreen = GetDC(NULL);
    POINT ptDst = { posX, 0 };
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
    float scale = m_config->GetEffectiveScale();
    float baseW = m_config->GetCollapsedLogicalWidth();
    float baseH = m_config->GetCollapsedLogicalHeight();
    int logW = (((int)(expandedState ? m_config->expandedWidth : baseW) + 16 + 3) / 4) * 4;
    int logH = (((int)(expandedState ? m_config->expandedHeight : baseH) + 3) / 4) * 4;
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
            DrawNotchShape(m_dcRenderTarget, (float)logW, (float)logH, (float)m_config->cornerRadius,
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

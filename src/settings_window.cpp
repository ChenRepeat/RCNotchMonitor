#include "settings_window.h"
#include "fa_icons.h"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <algorithm>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#define TIMER_SETTINGS_ANIM 201

enum {
    ZONE_WIN_MIN = 1,
    ZONE_WIN_CLOSE = 2,
    ZONE_INPUT_UISCALE = 8,
    ZONE_SLIDER_UISCALE = 9,
    ZONE_SLIDER_WIDTH = 10,
    ZONE_SLIDER_HEIGHT,
    ZONE_SLIDER_OPACITY,
    ZONE_BTN_LAYOUT,
    ZONE_BTN_REFRESH,
    ZONE_BTN_FPS_IDLE,
    ZONE_SWATCH_BG,
    ZONE_SWATCH_BORDER,
    ZONE_SWATCH_BTN,
    ZONE_SWATCH_BTNHOV,
    ZONE_WARN_INPUT,
    ZONE_WARN_INC,
    ZONE_WARN_DEC,
    ZONE_CRIT_INPUT,
    ZONE_CRIT_INC,
    ZONE_CRIT_DEC,
    ZONE_BTN_CLICKTHROUGH,
    ZONE_BTN_AUTOSTART,
    ZONE_BTN_RESET,
    ZONE_BTN_SAVE,

    // Monitor & Top Position Controls
    ZONE_SLIDER_POS_X,
    ZONE_BTN_POS_CENTER,
    ZONE_BTN_MONITOR,

    // 2D Block Canvas interactive blocks (0..3 = CPU/GPU/RAM/FPS, 4 = MEDIA)
    ZONE_BLOCK_BASE = 100, // 100..104

    // Single-Row Property Inspector controls (for currently selected block m_selectedBlock)
    ZONE_INSP_ENABLE = 120,
    ZONE_INSP_TEMP = 121,
    ZONE_INSP_TITLE = 122,
    ZONE_INSP_TCOL = 123,
    ZONE_INSP_VCOL = 124,
    ZONE_INSP_MEDIADOCK = 125
};

static COLORREF g_customColors[16] = {
    RGB(30, 32, 38), RGB(244, 244, 246), RGB(12, 14, 20), RGB(45, 55, 72),
    RGB(56, 189, 248), RGB(74, 222, 128), RGB(192, 132, 252), RGB(250, 204, 21),
    RGB(248, 113, 113), RGB(251, 146, 60), RGB(248, 250, 252), RGB(148, 163, 184),
    RGB(236, 72, 153), RGB(20, 184, 166), RGB(163, 230, 53), RGB(255, 255, 255)
};

static D2D1_COLOR_F ToD2D(COLORREF c, float a = 1.0f) {
    return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, a);
}

static int ColorLum(COLORREF c) {
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000;
}

static bool IsBlockDragZone(int zoneId) {
    return (zoneId >= ZONE_BLOCK_BASE && zoneId <= ZONE_BLOCK_BASE + 4);
}

SettingsWindow::SettingsWindow() {
    QueryPerformanceFrequency(&m_qpcFreq);
    QueryPerformanceCounter(&m_lastTick);
}

SettingsWindow::~SettingsWindow() {
    if (m_hwnd) DestroyWindow(m_hwnd);
    CleanupD2D();
}

void SettingsWindow::FreeOffscreen() {
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
    m_bufW = 0;
    m_bufH = 0;
}

void SettingsWindow::EnsureOffscreen(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (m_memDC && m_dibBmp && m_bufW == w && m_bufH == h) return;
    FreeOffscreen();

    HDC hdcScreen = GetDC(NULL);
    m_memDC = CreateCompatibleDC(hdcScreen);
    ReleaseDC(NULL, hdcScreen);
    if (!m_memDC) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    m_dibBmp = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (m_dibBmp) {
        m_oldBmp = (HBITMAP)SelectObject(m_memDC, m_dibBmp);
        m_bufW = w;
        m_bufH = h;
    }
}

bool SettingsWindow::InitD2D() {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_d2dFactory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwriteFactory)))) return false;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f
    );
    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&props, &m_dcRenderTarget))) return false;
    m_dcRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f
    );
    m_d2dFactory->CreateStrokeStyle(&ssp, nullptr, 0, &m_dashedStroke);
    return true;
}

void SettingsWindow::CleanupD2D() {
    FreeOffscreen();
    if (m_dashedStroke) { m_dashedStroke->Release(); m_dashedStroke = nullptr; }
    if (m_dcRenderTarget) { m_dcRenderTarget->Release(); m_dcRenderTarget = nullptr; }
    if (m_dwriteFactory) { m_dwriteFactory->Release(); m_dwriteFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
}

void SettingsWindow::ComputeTargetBlockRects(float layoutAnim, D2D1_RECT_F outRects[5]) const {
    if (!m_config) return;
    float sLay = std::clamp(layoutAnim, 0.0f, 1.0f);
    sLay = sLay * sLay * (3.0f - 2.0f * sLay);

    // Canvas bounds: x = 32..828 (w = 796), y = 396..546 (h = 150)
    // Canvas inner bounds: x = 42..818 (w = 776), y = 404..538 (h = 134)
    bool dockRight = (m_config->mediaDockSide == 1);

    // 1. Media block (index 4) - widened to 220.0f for relaxed single-line text & generous audio peaks
    float mW0 = 220.0f;
    float mH0 = 122.0f;
    float mX0 = dockRight ? (818.0f - mW0) : 42.0f;
    float mY0 = 408.0f;

    float mW1 = 168.0f;
    float mH1 = 88.0f;
    float mX1 = dockRight ? (818.0f - mW1) : 42.0f;
    float mY1 = 426.0f;

    float mx = mX0 + (mX1 - mX0) * sLay;
    float my = mY0 + (mY1 - mY0) * sLay;
    float mw = mW0 + (mW1 - mW0) * sLay;
    float mh = mH0 + (mH1 - mH0) * sLay;
    outRects[4] = D2D1::RectF(mx, my, mx + mw, my + mh);

    // 2. Hardware blocks (indices 0..3) ordered by itemOrder[pos]
    float hwStartX0 = dockRight ? 42.0f : (42.0f + 220.0f + 20.0f); // 282.0f when media is left
    float hwW0 = 258.0f;
    float hwH0 = 56.0f;
    float gapX0 = 18.0f;
    float gapY0 = 10.0f;

    float hwStartX1 = dockRight ? 42.0f : (42.0f + 168.0f + 20.0f); // 230.0f when media is left
    float hwW1 = 136.0f;
    float hwH1 = 88.0f;
    float gapX1 = 14.0f;

    for (int pos = 0; pos < 4; ++pos) {
        int mIdx = m_config->itemOrder[pos];
        if (mIdx < 0 || mIdx >= 4) continue;

        int col0 = pos % 2;
        int row0 = pos / 2;
        float x0 = hwStartX0 + col0 * (hwW0 + gapX0);
        float y0 = 408.0f + row0 * (hwH0 + gapY0);

        float x1 = hwStartX1 + pos * (hwW1 + gapX1);
        float y1 = 426.0f;

        float bx = x0 + (x1 - x0) * sLay;
        float by = y0 + (y1 - y0) * sLay;
        float bw = hwW0 + (hwW1 - hwW0) * sLay;
        float bh = hwH0 + (hwH1 - hwH0) * sLay;
        outRects[mIdx] = D2D1::RectF(bx, by, bx + bw, by + bh);
    }
}

void SettingsWindow::SnapBlocksToTargets() {
    if (!m_config) return;
    m_layoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
    ComputeTargetBlockRects(m_layoutAnim, m_blockVisual);
    m_blockAnimReady = true;
}

bool SettingsWindow::Create(HINSTANCE hInstance, AppConfig* config) {
    m_hInstance = hInstance;
    m_config = config;
    if (m_config) {
        SnapBlocksToTargets();
    }
    if (!InitD2D()) return false;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SettingsWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"RCNotchMonitor_D2DSettings";
    RegisterClassExW(&wc);

    float resScale = AppConfig::GetAutoResolutionScale();
    int winW = (((int)std::round(860.0f * resScale) + 2) / 4) * 4;
    int winH = (((int)std::round(840.0f * resScale) + 2) / 4) * 4;
    MonitorEntry mon = GetTargetMonitor(m_config ? m_config->targetMonitor : 0, m_config ? m_config->targetMonitorDevice : L"");
    int monW = mon.rcMonitor.right - mon.rcMonitor.left;
    int monH = mon.rcMonitor.bottom - mon.rcMonitor.top;
    int posX = mon.rcMonitor.left + (monW - winW) / 2;
    int posY = mon.rcMonitor.top + (monH - winH) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"設定",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, this
    );
    if (!m_hwnd) return false;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    DWORD cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
    QueryPerformanceCounter(&m_lastTick);
    SetTimer(m_hwnd, TIMER_SETTINGS_ANIM, 8, NULL);
    return true;
}

void SettingsWindow::Show() {
    if (!m_hwnd) return;
    if (m_config) {
        MonitorEntry mon = GetTargetMonitor(m_config->targetMonitor, m_config->targetMonitorDevice);
        int monW = mon.rcMonitor.right - mon.rcMonitor.left;
        int monH = mon.rcMonitor.bottom - mon.rcMonitor.top;
        RECT rcWin = {};
        GetWindowRect(m_hwnd, &rcWin);
        int winW = rcWin.right - rcWin.left;
        int winH = rcWin.bottom - rcWin.top;
        int posX = mon.rcMonitor.left + (monW - winW) / 2;
        int posY = mon.rcMonitor.top + (monH - winH) / 2;
        SetWindowPos(m_hwnd, HWND_TOP, posX, posY, winW, winH, SWP_NOACTIVATE);
    }
    QueryPerformanceCounter(&m_lastTick);
    SnapBlocksToTargets();
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_hwnd);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void SettingsWindow::Hide() {
    if (!m_hwnd) return;
    CommitTempEdit();
    CommitScaleEdit();
    m_focusedTitleRow = -1;
    m_focusedTempIdx = -1;
    m_draggingBlock = -1;
    ShowWindow(m_hwnd, SW_HIDE);
}

void SettingsWindow::RefreshIfVisible() {
    if (m_hwnd && IsWindowVisible(m_hwnd)) {
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void SettingsWindow::CommitTempEdit() {
    if (m_focusedTempIdx < 0 || !m_config) return;
    if (!m_tempEditStr.empty()) {
        int val = _wtoi(m_tempEditStr.c_str());
        if (m_focusedTempIdx == 0) {
            m_config->tempWarnC = std::clamp(val, 40, 105);
        } else if (m_focusedTempIdx == 1) {
            m_config->tempCritC = std::clamp(val, 50, 115);
        }
        NotifyChanged();
    }
    m_focusedTempIdx = -1;
    m_tempEditStr.clear();
}

void SettingsWindow::CommitScaleEdit() {
    if (!m_editingScale || !m_config) return;
    m_editingScale = false;
    if (!m_scaleEditStr.empty()) {
        int val = _wtoi(m_scaleEditStr.c_str());
        val = std::clamp(val, 50, 250);
        m_config->uiScalePct = val;
        NotifyChanged();
    }
    m_scaleEditStr.clear();
}

void SettingsWindow::NotifyChanged(bool saveToDisk) {
    if (!m_config) return;
    if (saveToDisk) {
        m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
    }
    if (onConfigUpdated) onConfigUpdated();
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
}

bool SettingsWindow::PickColor(COLORREF& ioColor) {
    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = m_hwnd;
    cc.lpCustColors = g_customColors;
    cc.rgbResult = ioColor;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (ChooseColorW(&cc)) {
        ioColor = cc.rgbResult;
        return true;
    }
    return false;
}

void SettingsWindow::ApplyAutoStartTask(bool enable) {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t cmd[MAX_PATH * 2] = {0};
    if (enable) {
        swprintf(cmd, MAX_PATH * 2,
                 L"schtasks /create /tn \"RCNotchMonitor_AutoStart\" /tr \"\\\"%ls\\\"\" /sc onlogon /rl highest /f",
                 exePath);
    } else {
        swprintf(cmd, MAX_PATH * 2, L"schtasks /delete /tn \"RCNotchMonitor_AutoStart\" /f");
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void SettingsWindow::RenderToTarget(ID2D1RenderTarget* rt, int w, int h) {
    if (!m_config) return;
    m_hitZones.clear();

    float resScale = AppConfig::GetAutoResolutionScale();
    float logW = (float)w / resScale;
    float logH = (float)h / resScale;

    if (!m_blockAnimReady) {
        SnapBlocksToTargets();
    }

    auto addZone = [&](int id, D2D1_RECT_F rc) {
        m_hitZones.push_back({id, rc});
    };

    IDWriteTextFormat* fmtH1 = nullptr;
    IDWriteTextFormat* fmtSec = nullptr;
    IDWriteTextFormat* fmtSub = nullptr;
    IDWriteTextFormat* fmtLbl = nullptr;
    IDWriteTextFormat* fmtRight = nullptr;
    IDWriteTextFormat* fmtBtn = nullptr;
    IDWriteTextFormat* fmtBtnLeft = nullptr;
    IDWriteTextFormat* fmtBlockTitle = nullptr;
    IDWriteTextFormat* fmtBlockVal = nullptr;

    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 28.0f, L"zh-TW", &fmtH1);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"zh-TW", &fmtSec);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-TW", &fmtSub);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-TW", &fmtLbl);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.0f, L"zh-TW", &fmtRight);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-TW", &fmtBtn);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-TW", &fmtBtnLeft);
    m_dwriteFactory->CreateTextFormat(L"Segoe UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-TW", &fmtBlockTitle);
    m_dwriteFactory->CreateTextFormat(L"Consolas", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"zh-TW", &fmtBlockVal);

    if (!fmtH1 || !fmtSec || !fmtSub || !fmtLbl || !fmtRight || !fmtBtn || !fmtBtnLeft || !fmtBlockTitle || !fmtBlockVal) return;

    fmtH1->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtSec->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtSub->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtSub->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtLbl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtLbl->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtRight->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtRight->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtBtn->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtBtnLeft->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtBtnLeft->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtBlockTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtBlockTitle->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmtBlockVal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtBlockVal->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    auto measureBtnText = [&](const std::wstring& s) -> float {
        if (s.empty()) return 0.0f;
        IDWriteTextLayout* layout = nullptr;
        float tw = 0.0f;
        if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmtBtnLeft, 400.0f, 40.0f, &layout))) {
            DWRITE_TEXT_METRICS tm = {};
            if (SUCCEEDED(layout->GetMetrics(&tm))) tw = tm.widthIncludingTrailingWhitespace;
            layout->Release();
        }
        return tw;
    };

    ID2D1SolidColorBrush* brCard = nullptr;
    ID2D1SolidColorBrush* brCanvasBg = nullptr;
    ID2D1SolidColorBrush* brBorder = nullptr;
    ID2D1SolidColorBrush* brWinBorder = nullptr;
    ID2D1SolidColorBrush* brText = nullptr;
    ID2D1SolidColorBrush* brMuted = nullptr;
    ID2D1SolidColorBrush* brSliderFill = nullptr;
    ID2D1SolidColorBrush* brTrack = nullptr;
    ID2D1SolidColorBrush* brSelect = nullptr;

    rt->CreateSolidColorBrush(D2D1::ColorF(0.082f, 0.086f, 0.102f, 1.0f), &brCard);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.051f, 0.055f, 0.068f, 1.0f), &brCanvasBg);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.180f, 0.192f, 0.224f, 1.0f), &brBorder);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.240f, 0.255f, 0.295f, 1.0f), &brWinBorder);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f), &brText);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.58f, 0.61f, 0.67f, 1.0f), &brMuted);
    rt->CreateSolidColorBrush(ToD2D(m_config->btnHoverColor), &brSliderFill);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.145f, 0.153f, 0.180f, 1.0f), &brTrack);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.290f, 0.871f, 0.502f, 1.0f), &brSelect); // #4ADE80 GPU Green Selection

    rt->BeginDraw();
    rt->SetTransform(D2D1::Matrix3x2F::Scale(resScale, resScale));
    rt->Clear(D2D1::ColorF(0.047f, 0.051f, 0.063f, 1.0f));

    rt->DrawRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, logW - 0.5f, logH - 0.5f), 8.0f, 8.0f),
        brWinBorder, 1.2f
    );

    auto drawButton = [&](int zoneId, const std::wstring& txt, float x, float y, float bw, float bh,
                          bool active = false, int faIcon = -1, bool disabled = false, bool isDangerHover = false) {
        D2D1_RECT_F rc = D2D1::RectF(x, y, x + bw, y + bh);
        if (!disabled) addZone(zoneId, rc);
        bool hov = (!disabled && m_hoveredZone == zoneId);

        COLORREF rawBg = m_config->btnColor;
        if (hov && isDangerHover) {
            rawBg = RGB(220, 38, 38);
        } else if (hov || active) {
            rawBg = m_config->btnHoverColor;
        }

        int lum = ColorLum(rawBg);
        D2D1_COLOR_F bgCol = ToD2D(rawBg, disabled ? 0.45f : 1.0f);
        D2D1_COLOR_F fgCol = disabled
            ? D2D1::ColorF(0.35f, 0.37f, 0.42f, 1.0f)
            : ((lum > 145) ? D2D1::ColorF(0.05f, 0.05f, 0.07f, 1.0f) : D2D1::ColorF(0.96f, 0.96f, 0.98f, 1.0f));

        ID2D1SolidColorBrush* bg = nullptr;
        ID2D1SolidColorBrush* fg = nullptr;
        rt->CreateSolidColorBrush(bgCol, &bg);
        rt->CreateSolidColorBrush(fgCol, &fg);

        if (bg) rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 6.0f, 6.0f), bg);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rc, 6.0f, 6.0f), (hov || active) ? brSliderFill : brBorder, 1.0f);

        if (fg) {
            if (faIcon >= 0 && txt.empty()) {
                DrawFaIcon(m_d2dFactory, rt, (FaIconType)faIcon, x + bw * 0.5f, y + bh * 0.5f, 16.0f, fg, bgCol, m_layoutAnim);
            } else if (faIcon >= 0) {
                float tw = measureBtnText(txt);
                float iconW = 16.0f;
                float gap = 8.0f;
                float totalW = iconW + gap + tw;
                float startX = x + (bw - totalW) * 0.5f;
                DrawFaIcon(m_d2dFactory, rt, (FaIconType)faIcon, startX + iconW * 0.5f, y + bh * 0.5f, 16.0f, fg, bgCol, m_layoutAnim);
                D2D1_RECT_F rcTxt = D2D1::RectF(startX + iconW + gap, y, startX + totalW + 4.0f, y + bh);
                rt->DrawText(txt.c_str(), (UINT32)txt.size(), fmtBtnLeft, rcTxt, fg);
            } else {
                rt->DrawText(txt.c_str(), (UINT32)txt.size(), fmtBtn, rc, fg);
            }
            fg->Release();
        }
        if (bg) bg->Release();
    };

    auto drawSlider = [&](int zoneId, const wchar_t* label, int val, int minV, int maxV, const wchar_t* unit, float x, float y, float sw, float labelW = 88.0f) {
        // 1. Label text (e.g. "整體縮放", "不透明度", "基準寬度", "基準高度", "頂部位置")
        rt->DrawText(label, (UINT32)wcslen(label), fmtLbl, D2D1::RectF(x, y + 2.0f, x + labelW, y + 34.0f), brText);

        // 2. Value badge / input box
        float valBoxX = x + labelW + 4.0f;
        float valBoxW = (zoneId == ZONE_SLIDER_POS_X) ? 56.0f : 74.0f;
        D2D1_RECT_F rcValBox = D2D1::RectF(valBoxX, y, valBoxX + valBoxW, y + 34.0f);

        if (zoneId == ZONE_SLIDER_UISCALE) {
            addZone(ZONE_INPUT_UISCALE, rcValBox);
            bool hovInput = (m_hoveredZone == ZONE_INPUT_UISCALE);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcValBox, 6.0f, 6.0f), brTrack);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(rcValBox, 6.0f, 6.0f),
                                     (m_editingScale || hovInput) ? brSelect : brBorder,
                                     m_editingScale ? 1.8f : 1.0f);
            std::wstring valStr = m_editingScale ? (m_scaleEditStr + L"|%") : (std::to_wstring(val) + L"%");
            rt->DrawText(valStr.c_str(), (UINT32)valStr.size(), fmtBtn, rcValBox, brText);
        } else {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcValBox, 6.0f, 6.0f), brTrack);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(rcValBox, 6.0f, 6.0f), brBorder, 1.0f);
            wchar_t valBuf[32];
            swprintf(valBuf, 32, L"%d%ls", val, unit);
            rt->DrawText(valBuf, (UINT32)wcslen(valBuf), fmtBtn, rcValBox, brText);
        }

        // 3. Slider Track
        float tx = valBoxX + valBoxW + 8.0f;
        float ty = y + 15.0f;
        float tw = (x + sw) - tx;
        D2D1_RECT_F rcHit = D2D1::RectF(tx - 6, y, tx + tw + 6, y + 34.0f);
        addZone(zoneId, rcHit);

        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + tw, ty + 5.0f), 2.5f, 2.5f), brTrack);
        float pct = std::clamp((float)(val - minV) / (float)(maxV - minV), 0.0f, 1.0f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + tw * pct, ty + 5.0f), 2.5f, 2.5f), brSliderFill);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + tw * pct, ty + 2.5f), 7.0f, 7.0f), brText);
    };

    auto drawSwatch = [&](int zoneId, const wchar_t* prefix, COLORREF col, float x, float y, float bw, float bh) {
        D2D1_RECT_F rc = D2D1::RectF(x, y, x + bw, y + bh);
        addZone(zoneId, rc);
        bool hov = (m_hoveredZone == zoneId);
        ID2D1SolidColorBrush* swBr = nullptr;
        rt->CreateSolidColorBrush(ToD2D(col), &swBr);
        if (swBr) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 6.0f, 6.0f), swBr);
            swBr->Release();
        }
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rc, 6.0f, 6.0f), hov ? brText : brBorder, hov ? 1.5f : 1.0f);

        int lum = ColorLum(col);
        ID2D1SolidColorBrush* txtBr = nullptr;
        rt->CreateSolidColorBrush(lum > 145 ? D2D1::ColorF(0.05f, 0.06f, 0.08f) : D2D1::ColorF(0.97f, 0.98f, 0.99f), &txtBr);
        std::wstring label = (prefix && prefix[0]) ? std::wstring(prefix) : ColorToHex(col);
        if (txtBr) {
            rt->DrawText(label.c_str(), (UINT32)label.size(), fmtBtn, rc, txtBr);
            txtBr->Release();
        }
    };

    // ==================== Custom Borderless Title Bar (4px Grid) ====================
    ID2D1SolidColorBrush* brLogo = nullptr;
    rt->CreateSolidColorBrush(D2D1::ColorF(0.902f, 0.745f, 0.353f, 1.0f), &brLogo); // #E6BE5A Gold Logo
    if (brLogo) {
        DrawFaIcon(m_d2dFactory, rt, FA_ICON_LOGO, 32.0f, 32.0f, 22.0f, brLogo);
        brLogo->Release();
    }
    rt->DrawText(L"設定", 2, fmtH1, D2D1::RectF(54, 12, 180, 52), brText);

    const wchar_t* wheelTip = L"瀏海可藉由滾輪縮放";
    rt->DrawText(wheelTip, (UINT32)wcslen(wheelTip), fmtRight, D2D1::RectF(240, 16, logW - 108, 48), brMuted);

    // Custom Minimize (—) & Close (✕) Buttons on top-right (h = 34, w = 38)
    drawButton(ZONE_WIN_MIN,   L"", logW - 96.0f, 14.0f, 38.0f, 34.0f, false, FA_ICON_MINUS, false, false);
    drawButton(ZONE_WIN_CLOSE, L"", logW - 52.0f, 14.0f, 38.0f, 34.0f, false, FA_ICON_XMARK, false, true);

    // ==================== SECTION 1: 外觀與尺寸 (Exterior Title + Grouped Card) ====================
    float effScale = m_config->GetEffectiveScale();
    int physW = (((int)std::round(m_config->GetCollapsedLogicalWidth() * effScale) + 2) / 4) * 4;
    int physH = (((int)std::round(m_config->GetCollapsedLogicalHeight() * effScale) + 2) / 4) * 4;
    wchar_t dimBuf[96];
    swprintf(dimBuf, 96, L"%d × %d px (%.2fx)", physW, physH, effScale);

    // Section 1 Exterior Title Header (y = 62..94, h = 32)
    DrawFaIcon(m_d2dFactory, rt, FA_ICON_SLIDERS, 32.0f, 78.0f, 18.0f, brText);
    rt->DrawText(L"外觀與尺寸", 5, fmtSec, D2D1::RectF(54, 62, 260, 94), brText);
    rt->DrawText(dimBuf, (UINT32)wcslen(dimBuf), fmtRight, D2D1::RectF(300, 62, logW - 28, 94), brMuted);

    // Card 1 Container (y = 98..334, h = 236)
    D2D1_RECT_F rcCard1 = D2D1::RectF(20, 98, logW - 20, 334);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard1, 8.0f, 8.0f), brCard);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCard1, 8.0f, 8.0f), brBorder, 1.0f);

    // Row 1: Scale & Opacity (y = 112..148, h = 36)
    drawSlider(ZONE_SLIDER_UISCALE, L"整體縮放", m_config->uiScalePct, 50, 250, L"%",   36, 112, 380, 88.0f);
    drawSlider(ZONE_SLIDER_OPACITY, L"不透明度", (m_config->bgOpacity * 100) / 255, 35, 100, L"%", 444, 112, 380, 88.0f);

    // Row 2: Base Width & Base Height (y = 162..198, h = 36)
    drawSlider(ZONE_SLIDER_WIDTH,   L"基準寬度", m_config->notchWidth, 200, 420, L" px", 36, 162, 380, 88.0f);
    drawSlider(ZONE_SLIDER_HEIGHT,  L"基準高度", m_config->notchHeight, 28,  68, L" px", 444, 162, 380, 88.0f);

    // Row 3: Top Position (Slider + Center reset button) & Monitor Selector Button (y = 212..248, h = 36)
    drawSlider(ZONE_SLIDER_POS_X, L"頂部位置", m_config->positionOffsetPct, 0, 100, L"%", 36, 212, 302, 88.0f);
    drawButton(ZONE_BTN_POS_CENTER, L"置中", 346, 212, 70, 36, m_config->positionOffsetPct == 50);

    MonitorEntry curMon = GetTargetMonitor(m_config->targetMonitor, m_config->targetMonitorDevice);
    std::wstring monTxt = curMon.label + L" ⇄";
    drawButton(ZONE_BTN_MONITOR, monTxt, 444, 212, 380, 36, false, FA_ICON_DESKTOP);

    // Row 4: 6 Quick Controls (y = 268..312, h = 44)
    wchar_t refTxt[32];
    swprintf(refTxt, 32, L"更新: %d ms", m_config->refreshIntervalMs);
    drawButton(ZONE_BTN_REFRESH, refTxt, 36, 268, 140, 44, false);

    std::wstring fpsTxt = (m_config->fpsIdleMode == 0) ? L"FPS待機: --" : L"FPS待機: Hz";
    drawButton(ZONE_BTN_FPS_IDLE, fpsTxt, 184, 268, 148, 44, false);

    drawSwatch(ZONE_SWATCH_BG,     L"底色", m_config->bgColor,       340, 268, 110, 44);
    drawSwatch(ZONE_SWATCH_BORDER, L"邊框", m_config->borderColor,   458, 268, 110, 44);
    drawSwatch(ZONE_SWATCH_BTN,    L"按鈕", m_config->btnColor,      576, 268, 110, 44);
    drawSwatch(ZONE_SWATCH_BTNHOV, L"懸停", m_config->btnHoverColor, 694, 268, 130, 44);

    // ==================== SECTION 2: 區塊拖曳版面配置 (Exterior Title + Grouped Card) ====================
    std::wstring layBtnTxt = (m_config->layoutMode == 0) ? L"雙行網格" : L"單行橫條";

    // Section 2 Exterior Title Header + Layout Toggle Button (y = 348..380)
    DrawFaIcon(m_d2dFactory, rt, FA_ICON_LAYOUT, 32.0f, 364.0f, 18.0f, brText, D2D1::ColorF(0, 0, 0, 0), m_layoutAnim);
    rt->DrawText(L"區塊拖曳版面配置", 8, fmtSec, D2D1::RectF(54, 348, 280, 380), brText);
    rt->DrawText(L"拖曳積木調整位置・點擊積木於下方設定屬性", 20, fmtSub, D2D1::RectF(288, 352, 650, 380), brMuted);
    drawButton(ZONE_BTN_LAYOUT, layBtnTxt, logW - 176.0f, 346, 156, 36, m_config->layoutMode == 1, FA_ICON_LAYOUT);

    // Card 2 Container (y = 386..642, h = 256)
    D2D1_RECT_F rcCard2 = D2D1::RectF(20, 386, logW - 20, 642);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard2, 8.0f, 8.0f), brCard);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCard2, 8.0f, 8.0f), brBorder, 1.0f);

    // 2D Visual Simulated Notch Blueprint Canvas (y = 396..546, h = 150)
    D2D1_RECT_F rcCanvas = D2D1::RectF(32, 396, logW - 32, 546);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCanvas, 8.0f, 8.0f), brCanvasBg);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCanvas, 8.0f, 8.0f), brBorder, 1.0f);

    // Subtle simulated top screen bezel bar inside the canvas
    rt->FillRectangle(D2D1::RectF(260, 396, logW - 260, 400), brBorder);

    const wchar_t* sampleValues[4] = { L"24%  52°C", L"41%  58°C", L"48%", L"120" };
    const FaIconType blockIcons[5] = { FA_ICON_CPU, FA_ICON_GPU, FA_ICON_RAM, FA_ICON_FPS, FA_ICON_MUSIC };

    auto drawCanvasBlock = [&](int bIdx, bool isDragged) {
        D2D1_RECT_F rc = m_blockVisual[bIdx];
        addZone(ZONE_BLOCK_BASE + bIdx, rc);

        bool isMedia = (bIdx == 4);
        bool enabled = isMedia ? m_config->mediaEnabled : m_config->metrics[bIdx].enabled;
        bool selected = (m_selectedBlock == bIdx);
        bool hovered = (m_hoveredZone == ZONE_BLOCK_BASE + bIdx);

        COLORREF titleCol = isMedia ? m_config->mediaTitleColor : m_config->metrics[bIdx].titleColor;
        COLORREF valCol   = isMedia ? m_config->mediaWaveColor  : m_config->metrics[bIdx].valueColor;

        D2D1_COLOR_F blockBg = enabled
            ? (selected ? D2D1::ColorF(0.145f, 0.165f, 0.215f, 0.98f) : D2D1::ColorF(0.110f, 0.120f, 0.150f, 0.95f))
            : D2D1::ColorF(0.080f, 0.085f, 0.105f, 0.35f);

        ID2D1SolidColorBrush* brB = nullptr;
        rt->CreateSolidColorBrush(blockBg, &brB);
        if (brB) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 6.0f, 6.0f), brB);
            brB->Release();
        }

        // Border: GPU Green (#4ADE80) if selected, solid white if dragged, dashed semi-transparent if disabled!
        if (!enabled && m_dashedStroke) {
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(rc, 6.0f, 6.0f),
                selected ? brSelect : brMuted,
                selected ? 2.0f : 1.2f,
                m_dashedStroke
            );
        } else {
            rt->DrawRoundedRectangle(
                D2D1::RoundedRect(rc, 6.0f, 6.0f),
                isDragged ? brText : (selected ? brSelect : (hovered ? brText : brBorder)),
                (selected || isDragged) ? 2.0f : 1.0f
            );
        }

        float alpha = enabled ? 1.0f : 0.35f;
        ID2D1SolidColorBrush* brT = nullptr;
        ID2D1SolidColorBrush* brV = nullptr;
        rt->CreateSolidColorBrush(ToD2D(titleCol, alpha), &brT);
        rt->CreateSolidColorBrush(ToD2D(valCol, alpha), &brV);

        // Top-right grip handle icon inside block (Enlarged to 16.0f FontAwesome grip)
        DrawFaIcon(m_d2dFactory, rt, FA_ICON_GRIP, rc.right - 18.0f, rc.top + 18.0f, 16.0f, selected ? brSelect : brMuted);

        if (isMedia) {
            float bh = rc.bottom - rc.top;
            if (bh > 90.0f) {
                // Tall Dual-Row Media Block on Canvas
                DrawFaIcon(m_d2dFactory, rt, FA_ICON_MUSIC, rc.left + 20.0f, rc.top + 26.0f, 18.0f, brV ? brV : brText);
                if (brT) {
                    std::wstring mTitle = L"MEDIA 媒體";
                    rt->DrawText(mTitle.c_str(), (UINT32)mTitle.size(), fmtBlockTitle,
                                 D2D1::RectF(rc.left + 42.0f, rc.top + 12.0f, rc.right - 24.0f, rc.top + 40.0f), brT);
                }
                const wchar_t* dockDesc = (m_config->mediaDockSide == 0) ? L"靠左停駐 (可左右拖曳)" : L"靠右停駐 (可左右拖曳)";
                rt->DrawText(dockDesc, (UINT32)wcslen(dockDesc), fmtSub,
                             D2D1::RectF(rc.left + 16.0f, rc.top + 48.0f, rc.right - 10.0f, rc.top + 74.0f), brMuted);

                // Draw miniature audio peak bars preview (6 bars)
                if (brV) {
                    const float barH[6] = { 10.0f, 22.0f, 14.0f, 24.0f, 18.0f, 22.0f };
                    for (int b = 0; b < 6; ++b) {
                        float bx = rc.left + 18.0f + b * 7.0f;
                        float by = rc.bottom - 16.0f;
                        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, by - barH[b], bx + 4.0f, by), 1.5f, 1.5f), brV);
                    }
                    rt->DrawText(L"Spotify / YouTube", 17, fmtSub,
                                 D2D1::RectF(rc.left + 68.0f, rc.bottom - 38.0f, rc.right - 8.0f, rc.bottom - 10.0f), brV);
                }
            } else {
                // Compact Single-Row Media Block on Canvas
                DrawFaIcon(m_d2dFactory, rt, FA_ICON_MUSIC, rc.left + 18.0f, rc.top + 24.0f, 16.0f, brV ? brV : brText);
                if (brT) {
                    std::wstring mTitle = L"MEDIA";
                    rt->DrawText(mTitle.c_str(), (UINT32)mTitle.size(), fmtBlockTitle,
                                 D2D1::RectF(rc.left + 36.0f, rc.top + 8.0f, rc.right - 24.0f, rc.top + 38.0f), brT);
                }
                if (brV) {
                    std::wstring subStr = (m_config->mediaDockSide == 0) ? L"ılılıl 靠左" : L"ılılıl 靠右";
                    rt->DrawText(subStr.c_str(), (UINT32)subStr.size(), fmtSub,
                                 D2D1::RectF(rc.left + 16.0f, rc.top + 46.0f, rc.right - 8.0f, rc.bottom - 8.0f), brV);
                }
            }
        } else {
            // Hardware Block (0..3)
            const MetricConfig& mc = m_config->metrics[bIdx];
            float bh = rc.bottom - rc.top;
            DrawFaIcon(m_d2dFactory, rt, blockIcons[bIdx], rc.left + 18.0f, rc.top + 22.0f, 16.0f, brT ? brT : brText);
            if (brT) {
                std::wstring dispT = mc.title;
                rt->DrawText(dispT.c_str(), (UINT32)dispT.size(), fmtBlockTitle,
                             D2D1::RectF(rc.left + 38.0f, rc.top + 6.0f, rc.right - 24.0f, rc.top + 36.0f), brT);
            }
            if (brV) {
                if (bh > 64.0f) {
                    // Single-Row taller block: value on second line
                    rt->DrawText(sampleValues[bIdx], (UINT32)wcslen(sampleValues[bIdx]), fmtBlockVal,
                                 D2D1::RectF(rc.left + 14.0f, rc.top + 44.0f, rc.right - 8.0f, rc.bottom - 8.0f), brV);
                } else {
                    // Dual-Row wide block: value on right side
                    rt->DrawText(sampleValues[bIdx], (UINT32)wcslen(sampleValues[bIdx]), fmtBlockVal,
                                 D2D1::RectF(rc.left + 114.0f, rc.top + 8.0f, rc.right - 26.0f, rc.bottom - 8.0f), brV);
                }
            }
        }

        if (brT) brT->Release();
        if (brV) brV->Release();
    };

    // Draw non-dragged blocks first, then the actively dragged block on top
    for (int bIdx = 0; bIdx < 5; ++bIdx) {
        if (bIdx != m_draggingBlock) {
            drawCanvasBlock(bIdx, false);
        }
    }
    if (m_draggingBlock >= 0 && m_draggingBlock < 5) {
        drawCanvasBlock(m_draggingBlock, true);
    }

    // ==================== Single-Row Property Inspector (y = 564..622, h = 58) ====================
    if (m_selectedBlock >= 0 && m_selectedBlock < 4) {
        int mIdx = m_selectedBlock;
        const MetricConfig& mc = m_config->metrics[mIdx];
        const wchar_t* shortNames[4] = { L"CPU", L"GPU", L"RAM", L"FPS" };

        // 1. Enable Toggle
        std::wstring enTxt = std::wstring(mc.enabled ? L"啟用: " : L"停用: ") + shortNames[mIdx];
        drawButton(ZONE_INSP_ENABLE, enTxt, 32, 566, 152, 48, mc.enabled, blockIcons[mIdx]);

        // 2. Show Temp Toggle (CPU/GPU only)
        if (mIdx == 0 || mIdx == 1) {
            drawButton(ZONE_INSP_TEMP, mc.showTemp ? L"顯示 °C" : L"隱藏 °C", 194, 566, 136, 48, mc.showTemp, FA_ICON_THERM);
        } else {
            drawButton(ZONE_INSP_TEMP, L"無溫度項", 194, 566, 136, 48, false, -1, true);
        }

        // 3. Editable Custom Title Input Box
        D2D1_RECT_F rcTitleBox = D2D1::RectF(340, 566, 506, 614);
        addZone(ZONE_INSP_TITLE, rcTitleBox);
        bool focused = (m_focusedTitleRow == mIdx);
        bool hovTitle = (m_hoveredZone == ZONE_INSP_TITLE);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rcTitleBox, 6.0f, 6.0f), brTrack);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rcTitleBox, 6.0f, 6.0f),
                                 (focused || hovTitle) ? brSelect : brBorder,
                                 focused ? 1.8f : 1.0f);
        std::wstring dispTitle = L"標題: " + mc.title + (focused ? L"|" : L"");
        rt->DrawText(dispTitle.c_str(), (UINT32)dispTitle.size(), fmtBtn, rcTitleBox, brText);

        // 4. Title Color Swatch
        std::wstring tHex = L"標題 " + ColorToHex(mc.titleColor);
        drawSwatch(ZONE_INSP_TCOL, tHex.c_str(), mc.titleColor, 516, 566, 150, 48);

        // 5. Value Color Swatch
        std::wstring vHex = L"數值 " + ColorToHex(mc.valueColor);
        drawSwatch(ZONE_INSP_VCOL, vHex.c_str(), mc.valueColor, 676, 566, 152, 48);
    } else {
        // Selected Block == 4 (MEDIA 媒體)
        drawButton(ZONE_INSP_ENABLE,
                   m_config->mediaEnabled ? L"啟用: 媒體" : L"停用: 媒體",
                   32, 566, 160, 48, m_config->mediaEnabled, FA_ICON_MUSIC);

        drawButton(ZONE_INSP_MEDIADOCK,
                   (m_config->mediaDockSide == 0) ? L"位置: 靠左停駐" : L"位置: 靠右停駐",
                   202, 566, 200, 48, m_config->mediaDockSide == 1, FA_ICON_EXCHANGE);

        std::wstring mTHex = L"曲名色 " + ColorToHex(m_config->mediaTitleColor);
        drawSwatch(ZONE_INSP_TCOL, mTHex.c_str(), m_config->mediaTitleColor, 412, 566, 200, 48);

        std::wstring mVHex = L"音波色 " + ColorToHex(m_config->mediaWaveColor);
        drawSwatch(ZONE_INSP_VCOL, mVHex.c_str(), m_config->mediaWaveColor, 622, 566, 206, 48);
    }

    // ==================== SECTION 3: 系統行為與溫度警戒 (Exterior Title + Grouped Card) ====================
    // Section 3 Exterior Title Header (y = 654..686, h = 32)
    DrawFaIcon(m_d2dFactory, rt, FA_ICON_BOLT, 32.0f, 670.0f, 18.0f, brText);
    rt->DrawText(L"系統行為與溫度警戒", 9, fmtSec, D2D1::RectF(54, 654, 340, 686), brText);

    // Card 3 Container (y = 692..762, h = 70)
    D2D1_RECT_F rcCard3 = D2D1::RectF(20, 692, logW - 20, 762);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard3, 8.0f, 8.0f), brCard);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCard3, 8.0f, 8.0f), brBorder, 1.0f);

    auto drawTempField = [&](const wchar_t* label, int tempIdx, int zoneInput, int zoneUp, int zoneDown, int curVal, float x, float y) {
        rt->DrawText(label, (UINT32)wcslen(label), fmtLbl, D2D1::RectF(x, y + 4.0f, x + 48.0f, y + 40.0f), brText);

        D2D1_RECT_F rcBox = D2D1::RectF(x + 52.0f, y, x + 130.0f, y + 42.0f);
        addZone(zoneInput, rcBox);
        bool focused = (m_focusedTempIdx == tempIdx);
        bool hov = (m_hoveredZone == zoneInput);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rcBox, 6.0f, 6.0f), brTrack);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rcBox, 6.0f, 6.0f), (focused || hov) ? brSelect : brBorder, focused ? 1.8f : 1.0f);

        std::wstring valStr = focused ? (m_tempEditStr + L"|°C") : (std::to_wstring(curVal) + L"°C");
        rt->DrawText(valStr.c_str(), (UINT32)valStr.size(), fmtBtn, rcBox, brText);

        drawButton(zoneUp,   L"", x + 136.0f, y, 36.0f, 42.0f, false, FA_ICON_UP);
        drawButton(zoneDown, L"", x + 176.0f, y, 36.0f, 42.0f, false, FA_ICON_DOWN);
    };

    drawTempField(L"警戒", 0, ZONE_WARN_INPUT, ZONE_WARN_INC, ZONE_WARN_DEC, m_config->tempWarnC, 36.0f,  706.0f);
    drawTempField(L"高溫", 1, ZONE_CRIT_INPUT, ZONE_CRIT_INC, ZONE_CRIT_DEC, m_config->tempCritC, 266.0f, 706.0f);

    drawButton(ZONE_BTN_CLICKTHROUGH,
               m_config->clickThroughLocked ? L"穿透鎖定" : L"自動展開",
               496.0f, 706.0f, 156.0f, 42.0f,
               m_config->clickThroughLocked,
               m_config->clickThroughLocked ? FA_ICON_LOCK : FA_ICON_UNLOCK);

    drawButton(ZONE_BTN_AUTOSTART,
               L"開機自啟",
               668.0f, 706.0f, 160.0f, 42.0f,
               m_config->autoStartAdmin,
               FA_ICON_BOLT);

    // ==================== Bottom Action Bar (4px Grid, Centered Equal Size) ====================
    float botY = 780.0f; // 38px margin below Card 3 (692..762)
    float actionBtnW = 180.0f;
    float actionGap = 24.0f;
    float totalActionW = actionBtnW * 2.0f + actionGap; // 384.0f
    float actionStartX = (logW - totalActionW) * 0.5f; // (860 - 384) / 2 = 238.0f
    drawButton(ZONE_BTN_RESET, L"還原",       actionStartX,                          botY, actionBtnW, 46.0f, false, FA_ICON_RESET);
    drawButton(ZONE_BTN_SAVE,  L"儲存並關閉", actionStartX + actionBtnW + actionGap, botY, actionBtnW, 46.0f, true,  FA_ICON_SAVE);

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->EndDraw();

    brCard->Release();
    brCanvasBg->Release();
    brBorder->Release();
    brWinBorder->Release();
    brText->Release();
    brMuted->Release();
    brSliderFill->Release();
    brTrack->Release();
    brSelect->Release();

    fmtH1->Release();
    fmtSec->Release();
    fmtSub->Release();
    fmtLbl->Release();
    fmtRight->Release();
    fmtBtn->Release();
    fmtBtnLeft->Release();
    fmtBlockTitle->Release();
    fmtBlockVal->Release();
}

void SettingsWindow::HandleZoneClick(int zoneId, float mx, float my, const D2D1_RECT_F& rc) {
    if (!m_config) return;
    if (zoneId != ZONE_INPUT_UISCALE) {
        CommitScaleEdit();
    }
    if (zoneId != ZONE_WARN_INPUT && zoneId != ZONE_CRIT_INPUT) {
        CommitTempEdit();
    }
    if (zoneId != ZONE_INSP_TITLE) {
        m_focusedTitleRow = -1;
    }

    auto updateSlider = [&](int& field, int minV, int maxV, int step) {
        float pct = std::clamp((mx - (rc.left + 4.0f)) / std::max(1.0f, (rc.right - rc.left - 8.0f)), 0.0f, 1.0f);
        int raw = minV + (int)std::round(pct * (maxV - minV));
        if (step > 1) raw = ((raw + step / 2) / step) * step;
        field = std::clamp(raw, minV, maxV);
        NotifyChanged(false);
    };

    switch (zoneId) {
    case ZONE_WIN_MIN:
        ShowWindow(m_hwnd, SW_MINIMIZE);
        return;
    case ZONE_WIN_CLOSE:
        Hide();
        return;
    case ZONE_INPUT_UISCALE:
        m_editingScale = true;
        m_scaleEditStr = std::to_wstring(m_config->uiScalePct);
        m_focusedTitleRow = -1;
        m_focusedTempIdx = -1;
        if (m_hwnd) InvalidateRect(m_hwnd, NULL, FALSE);
        return;
    case ZONE_SLIDER_UISCALE:
        m_draggingSliderZone = zoneId;
        updateSlider(m_config->uiScalePct, 50, 250, 1);
        return;
    case ZONE_SLIDER_WIDTH:
        m_draggingSliderZone = zoneId;
        updateSlider(m_config->notchWidth, 200, 420, 4);
        return;
    case ZONE_SLIDER_HEIGHT:
        m_draggingSliderZone = zoneId;
        updateSlider(m_config->notchHeight, 28, 68, 4);
        return;
    case ZONE_SLIDER_OPACITY: {
        m_draggingSliderZone = zoneId;
        int pctVal = (m_config->bgOpacity * 100) / 255;
        updateSlider(pctVal, 35, 100, 1);
        m_config->bgOpacity = std::clamp((pctVal * 255) / 100, 89, 255);
        NotifyChanged(false);
        return;
    }
    case ZONE_SLIDER_POS_X:
        m_draggingSliderZone = zoneId;
        updateSlider(m_config->positionOffsetPct, 0, 100, 1);
        return;
    case ZONE_BTN_POS_CENTER:
        m_config->positionOffsetPct = 50;
        NotifyChanged(false);
        return;
    case ZONE_BTN_MONITOR: {
        auto monList = GetAllMonitors();
        if (!monList.empty()) {
            int curIdx = 0;
            for (size_t i = 0; i < monList.size(); ++i) {
                if (monList[i].deviceName == m_config->targetMonitorDevice || (int)i == m_config->targetMonitor) {
                    curIdx = (int)i;
                    break;
                }
            }
            int nextIdx = (curIdx + 1) % (int)monList.size();
            m_config->targetMonitor = nextIdx;
            m_config->targetMonitorDevice = monList[nextIdx].deviceName;
            NotifyChanged(false);
        }
        return;
    }
    case ZONE_BTN_LAYOUT:
        QueryPerformanceCounter(&m_lastTick);
        m_config->layoutMode = (m_config->layoutMode == 0) ? 1 : 0;
        NotifyChanged(true);
        return;
    case ZONE_BTN_REFRESH:
        if (m_config->refreshIntervalMs == 250) m_config->refreshIntervalMs = 500;
        else if (m_config->refreshIntervalMs == 500) m_config->refreshIntervalMs = 1000;
        else if (m_config->refreshIntervalMs == 1000) m_config->refreshIntervalMs = 2000;
        else m_config->refreshIntervalMs = 250;
        NotifyChanged();
        return;
    case ZONE_BTN_FPS_IDLE:
        m_config->fpsIdleMode = (m_config->fpsIdleMode == 0) ? 1 : 0;
        NotifyChanged();
        return;
    case ZONE_SWATCH_BG:
        if (PickColor(m_config->bgColor)) NotifyChanged();
        return;
    case ZONE_SWATCH_BORDER:
        if (PickColor(m_config->borderColor)) NotifyChanged();
        return;
    case ZONE_SWATCH_BTN:
        if (PickColor(m_config->btnColor)) NotifyChanged();
        return;
    case ZONE_SWATCH_BTNHOV:
        if (PickColor(m_config->btnHoverColor)) NotifyChanged();
        return;
    case ZONE_WARN_INPUT:
        if (m_focusedTempIdx != 0) {
            CommitTempEdit();
            m_focusedTempIdx = 0;
            m_tempEditStr = std::to_wstring(m_config->tempWarnC);
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return;
    case ZONE_WARN_INC:
        m_config->tempWarnC = std::clamp(m_config->tempWarnC + 1, 40, 105);
        NotifyChanged();
        return;
    case ZONE_WARN_DEC:
        m_config->tempWarnC = std::clamp(m_config->tempWarnC - 1, 40, 105);
        NotifyChanged();
        return;
    case ZONE_CRIT_INPUT:
        if (m_focusedTempIdx != 1) {
            CommitTempEdit();
            m_focusedTempIdx = 1;
            m_tempEditStr = std::to_wstring(m_config->tempCritC);
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return;
    case ZONE_CRIT_INC:
        m_config->tempCritC = std::clamp(m_config->tempCritC + 1, 50, 115);
        NotifyChanged();
        return;
    case ZONE_CRIT_DEC:
        m_config->tempCritC = std::clamp(m_config->tempCritC - 1, 50, 115);
        NotifyChanged();
        return;
    case ZONE_BTN_AUTOSTART:
        m_config->autoStartAdmin = !m_config->autoStartAdmin;
        ApplyAutoStartTask(m_config->autoStartAdmin);
        NotifyChanged();
        return;
    case ZONE_BTN_CLICKTHROUGH:
        m_config->clickThroughLocked = !m_config->clickThroughLocked;
        NotifyChanged();
        return;
    case ZONE_BTN_RESET:
        QueryPerformanceCounter(&m_lastTick);
        m_config->ResetDefaults();
        SnapBlocksToTargets();
        NotifyChanged();
        return;
    case ZONE_BTN_SAVE:
        NotifyChanged();
        Hide();
        return;

    // Single-Row Property Inspector Handlers
    case ZONE_INSP_ENABLE:
        if (m_selectedBlock >= 0 && m_selectedBlock < 4) {
            m_config->metrics[m_selectedBlock].enabled = !m_config->metrics[m_selectedBlock].enabled;
        } else if (m_selectedBlock == 4) {
            m_config->mediaEnabled = !m_config->mediaEnabled;
        }
        NotifyChanged();
        return;
    case ZONE_INSP_TEMP:
        if (m_selectedBlock == 0 || m_selectedBlock == 1) {
            m_config->metrics[m_selectedBlock].showTemp = !m_config->metrics[m_selectedBlock].showTemp;
            NotifyChanged();
        }
        return;
    case ZONE_INSP_TITLE:
        if (m_selectedBlock >= 0 && m_selectedBlock < 4) {
            m_focusedTitleRow = m_selectedBlock;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return;
    case ZONE_INSP_TCOL:
        if (m_selectedBlock >= 0 && m_selectedBlock < 4) {
            if (PickColor(m_config->metrics[m_selectedBlock].titleColor)) NotifyChanged();
        } else if (m_selectedBlock == 4) {
            if (PickColor(m_config->mediaTitleColor)) NotifyChanged();
        }
        return;
    case ZONE_INSP_VCOL:
        if (m_selectedBlock >= 0 && m_selectedBlock < 4) {
            if (PickColor(m_config->metrics[m_selectedBlock].valueColor)) NotifyChanged();
        } else if (m_selectedBlock == 4) {
            if (PickColor(m_config->mediaWaveColor)) NotifyChanged();
        }
        return;
    case ZONE_INSP_MEDIADOCK:
        QueryPerformanceCounter(&m_lastTick);
        m_config->mediaDockSide = (m_config->mediaDockSide == 0) ? 1 : 0;
        NotifyChanged();
        return;
    }

    // 2D Visual Block Click & Drag Start
    if (IsBlockDragZone(zoneId)) {
        int bIdx = zoneId - ZONE_BLOCK_BASE;
        m_selectedBlock = bIdx;
        m_draggingBlock = bIdx;
        m_dragGrabOffsetX = mx - m_blockVisual[bIdx].left;
        m_dragGrabOffsetY = my - m_blockVisual[bIdx].top;
        QueryPerformanceCounter(&m_lastTick);
        SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    float resScale = AppConfig::GetAutoResolutionScale();

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wParam == TIMER_SETTINGS_ANIM && m_config && IsWindowVisible(m_hwnd)) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            float dt = (m_qpcFreq.QuadPart > 0)
                ? (float)(now.QuadPart - m_lastTick.QuadPart) / (float)m_qpcFreq.QuadPart
                : 0.008f;
            m_lastTick = now;
            dt = std::clamp(dt, 0.001f, 0.050f);

            bool dirty = false;
            float targetLay = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
            if (std::fabs(m_layoutAnim - targetLay) > 0.002f) {
                float step = dt / 0.24f;
                if (m_layoutAnim < targetLay)
                    m_layoutAnim = std::min(targetLay, m_layoutAnim + step);
                else
                    m_layoutAnim = std::max(targetLay, m_layoutAnim - step);
                dirty = true;
            } else if (m_layoutAnim != targetLay) {
                m_layoutAnim = targetLay;
                dirty = true;
            }

            D2D1_RECT_F targets[5];
            ComputeTargetBlockRects(m_layoutAnim, targets);
            float alpha = 1.0f - std::exp(-24.0f * dt);

            for (int bIdx = 0; bIdx < 5; ++bIdx) {
                float curW = m_blockVisual[bIdx].right - m_blockVisual[bIdx].left;
                float curH = m_blockVisual[bIdx].bottom - m_blockVisual[bIdx].top;
                float tgtW = targets[bIdx].right - targets[bIdx].left;
                float tgtH = targets[bIdx].bottom - targets[bIdx].top;

                if (bIdx == m_draggingBlock) {
                    // Keep width/height smoothly synced to layout mode even while dragging
                    float nw = curW + (tgtW - curW) * alpha;
                    float nh = curH + (tgtH - curH) * alpha;
                    m_blockVisual[bIdx].right = m_blockVisual[bIdx].left + nw;
                    m_blockVisual[bIdx].bottom = m_blockVisual[bIdx].top + nh;
                    continue;
                }

                auto stepVal = [&](float& c, float t) {
                    if (std::fabs(c - t) > 0.15f) {
                        c += (t - c) * alpha;
                        dirty = true;
                    } else if (c != t) {
                        c = t;
                        dirty = true;
                    }
                };
                stepVal(m_blockVisual[bIdx].left, targets[bIdx].left);
                stepVal(m_blockVisual[bIdx].top, targets[bIdx].top);
                stepVal(m_blockVisual[bIdx].right, targets[bIdx].right);
                stepVal(m_blockVisual[bIdx].bottom, targets[bIdx].bottom);
            }

            if (dirty) {
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;
        if (m_dcRenderTarget && w > 0 && h > 0) {
            EnsureOffscreen(w, h);
            if (m_memDC && m_dibBmp) {
                RECT rcBuf = { 0, 0, w, h };
                if (SUCCEEDED(m_dcRenderTarget->BindDC(m_memDC, &rcBuf))) {
                    RenderToTarget(m_dcRenderTarget, w, h);
                }
                BitBlt(hdc, 0, 0, w, h, m_memDC, 0, 0, SRCCOPY);
            }
        }
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        float mx = (float)GET_X_LPARAM(lParam) / resScale;
        float my = (float)GET_Y_LPARAM(lParam) / resScale;

        // 1. Handle active 2D block drag on the canvas
        if (m_draggingBlock >= 0 && m_draggingBlock < 5 && (wParam & MK_LBUTTON)) {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            float bw = m_blockVisual[m_draggingBlock].right - m_blockVisual[m_draggingBlock].left;
            float bh = m_blockVisual[m_draggingBlock].bottom - m_blockVisual[m_draggingBlock].top;
            float newL = std::clamp(mx - m_dragGrabOffsetX, 36.0f, 824.0f - bw);
            float newT = std::clamp(my - m_dragGrabOffsetY, 400.0f, 542.0f - bh);
            m_blockVisual[m_draggingBlock] = D2D1::RectF(newL, newT, newL + bw, newT + bh);

            float cx = newL + bw * 0.5f;
            float cy = newT + bh * 0.5f;

            if (m_draggingBlock == 4) {
                // Dragging MEDIA block horizontally across midpoint swaps left/right dock!
                int newDock = (cx >= 430.0f) ? 1 : 0;
                if (newDock != m_config->mediaDockSide) {
                    QueryPerformanceCounter(&m_lastTick);
                    m_config->mediaDockSide = newDock;
                    NotifyChanged(false);
                } else {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            } else {
                // Dragging a Hardware Block (0..3) in 2D: find closest hardware slot center
                D2D1_RECT_F targets[5];
                ComputeTargetBlockRects(m_layoutAnim, targets);

                // Compute slot centers for pos = 0..3
                int curPos = -1;
                for (int p = 0; p < 4; ++p) {
                    if (m_config->itemOrder[p] == m_draggingBlock) {
                        curPos = p;
                        break;
                    }
                }

                int bestPos = curPos;
                float bestDist2 = 1e9f;
                bool dockRight = (m_config->mediaDockSide == 1);
                float sLay = m_layoutAnim * m_layoutAnim * (3.0f - 2.0f * m_layoutAnim);
                float hwStartX0 = dockRight ? 42.0f : 282.0f;
                float hwStartX1 = dockRight ? 42.0f : 230.0f;

                for (int p = 0; p < 4; ++p) {
                    float x0 = hwStartX0 + (p % 2) * (258.0f + 18.0f) + 129.0f;
                    float y0 = 408.0f + (p / 2) * (56.0f + 10.0f) + 28.0f;
                    float x1 = hwStartX1 + p * (136.0f + 14.0f) + 68.0f;
                    float y1 = 426.0f + 44.0f;
                    float sx = x0 + (x1 - x0) * sLay;
                    float sy = y0 + (y1 - y0) * sLay;
                    float dx = cx - sx;
                    float dy = cy - sy;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < bestDist2) {
                        bestDist2 = d2;
                        bestPos = p;
                    }
                }

                if (curPos >= 0 && bestPos >= 0 && bestPos != curPos) {
                    QueryPerformanceCounter(&m_lastTick);
                    int step = (bestPos > curPos) ? 1 : -1;
                    for (int p = curPos; p != bestPos; p += step) {
                        m_config->itemOrder[p] = m_config->itemOrder[p + step];
                    }
                    m_config->itemOrder[bestPos] = m_draggingBlock;
                    NotifyChanged(false);
                } else {
                    InvalidateRect(m_hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        // 2. Handle active slider drag
        if (m_draggingSliderZone > 0 && (wParam & MK_LBUTTON)) {
            for (const auto& z : m_hitZones) {
                if (z.id == m_draggingSliderZone) {
                    HandleZoneClick(z.id, mx, my, z.rect);
                    break;
                }
            }
            return 0;
        }

        // 3. Hover detection & cursor selection (IDC_SIZEALL for draggable blocks, IDC_HAND for buttons)
        int hit = -1;
        for (const auto& z : m_hitZones) {
            if (mx >= z.rect.left && mx <= z.rect.right && my >= z.rect.top && my <= z.rect.bottom) {
                hit = z.id;
                break;
            }
        }
        if (IsBlockDragZone(hit)) {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
        } else {
            SetCursor(LoadCursor(NULL, hit > 0 ? IDC_HAND : IDC_ARROW));
        }
        if (hit != m_hoveredZone) {
            m_hoveredZone = hit;
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        float mx = (float)GET_X_LPARAM(lParam) / resScale;
        float my = (float)GET_Y_LPARAM(lParam) / resScale;
        SetCapture(m_hwnd);
        for (const auto& z : m_hitZones) {
            if (mx >= z.rect.left && mx <= z.rect.right && my >= z.rect.top && my <= z.rect.bottom) {
                HandleZoneClick(z.id, mx, my, z.rect);
                if (m_draggingBlock < 0 && m_draggingSliderZone < 0) {
                    ReleaseCapture();
                }
                return 0;
            }
        }
        CommitScaleEdit();
        CommitTempEdit();
        m_focusedTitleRow = -1;
        InvalidateRect(m_hwnd, NULL, FALSE);
        ReleaseCapture();

        if (my < 44.0f) {
            SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (m_draggingBlock >= 0 || m_draggingSliderZone >= 0) {
            m_draggingBlock = -1;
            m_draggingSliderZone = -1;
            QueryPerformanceCounter(&m_lastTick);
            ReleaseCapture();
            if (m_config) m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return 0;

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wParam;
        if (m_editingScale && m_config) {
            if (ch == VK_BACK) {
                if (!m_scaleEditStr.empty()) m_scaleEditStr.pop_back();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (ch == VK_RETURN) {
                CommitScaleEdit();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (ch == VK_ESCAPE) {
                m_editingScale = false;
                m_scaleEditStr.clear();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (ch >= L'0' && ch <= L'9' && m_scaleEditStr.size() < 3) {
                m_scaleEditStr.push_back(ch);
                int val = _wtoi(m_scaleEditStr.c_str());
                if (val >= 50 && val <= 250) {
                    m_config->uiScalePct = val;
                    if (onConfigUpdated) onConfigUpdated();
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (m_focusedTempIdx >= 0 && m_config) {
            if (ch == VK_BACK) {
                if (!m_tempEditStr.empty()) m_tempEditStr.pop_back();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (ch == VK_RETURN || ch == VK_ESCAPE) {
                CommitTempEdit();
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (ch >= L'0' && ch <= L'9' && m_tempEditStr.size() < 3) {
                m_tempEditStr.push_back(ch);
                int val = _wtoi(m_tempEditStr.c_str());
                if (val >= 40 && val <= 115) {
                    if (m_focusedTempIdx == 0) m_config->tempWarnC = std::clamp(val, 40, 105);
                    else m_config->tempCritC = std::clamp(val, 50, 115);
                    if (onConfigUpdated) onConfigUpdated();
                }
                InvalidateRect(m_hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (m_focusedTitleRow >= 0 && m_focusedTitleRow < 4 && m_config) {
            std::wstring& t = m_config->metrics[m_focusedTitleRow].title;
            if (ch == VK_BACK) {
                if (!t.empty()) t.pop_back();
                NotifyChanged();
            } else if (ch == VK_RETURN || ch == VK_ESCAPE) {
                m_focusedTitleRow = -1;
                InvalidateRect(m_hwnd, NULL, FALSE);
            } else if (ch >= 32 && t.size() < 10) {
                t.push_back(ch);
                NotifyChanged();
            }
        }
        return 0;
    }

    case WM_CLOSE:
        Hide();
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

bool SettingsWindow::SaveWindowBmp(const std::wstring& bmpPath) {
    if (!m_dcRenderTarget || !m_config) return false;
    float resScale = AppConfig::GetAutoResolutionScale();
    int w = (((int)std::round(860.0f * resScale) + 2) / 4) * 4;
    int h = (((int)std::round(840.0f * resScale) + 2) / 4) * 4;

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
        RECT rc = { 0, 0, w, h };
        if (SUCCEEDED(m_dcRenderTarget->BindDC(hdcMem, &rc))) {
            RenderToTarget(m_dcRenderTarget, w, h);
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

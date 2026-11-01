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

    // Indexed by metric ID (mIdx 0..3): base = 100 + mIdx * 10 + sub
    // sub: 1=enable toggle, 2=temp toggle, 3=title edit box, 4=title color, 5=value color, 6=drag handle
    ZONE_ROW_BASE = 100
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

static bool IsDragZoneId(int zoneId) {
    return (zoneId >= ZONE_ROW_BASE && zoneId < ZONE_ROW_BASE + 40 && ((zoneId - ZONE_ROW_BASE) % 10 == 6));
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
    return true;
}

void SettingsWindow::CleanupD2D() {
    FreeOffscreen();
    if (m_dcRenderTarget) { m_dcRenderTarget->Release(); m_dcRenderTarget = nullptr; }
    if (m_dwriteFactory) { m_dwriteFactory->Release(); m_dwriteFactory = nullptr; }
    if (m_d2dFactory) { m_d2dFactory->Release(); m_d2dFactory = nullptr; }
}

bool SettingsWindow::Create(HINSTANCE hInstance, AppConfig* config) {
    m_hInstance = hInstance;
    m_config = config;
    if (m_config) {
        m_layoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
        for (int pos = 0; pos < 4; ++pos) {
            int mIdx = m_config->itemOrder[pos];
            if (mIdx >= 0 && mIdx < 4) {
                m_rowVisualY[mIdx] = 236.0f + pos * 40.0f;
            }
        }
        m_rowAnimReady = true;
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
    int winW = (((int)std::round(600.0f * resScale) + 2) / 4) * 4;
    int winH = (((int)std::round(524.0f * resScale) + 2) / 4) * 4;
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"設定",
        WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU,
        ((scrW - winW) / 8) * 4, ((scrH - winH) / 8) * 4, winW, winH,
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
    QueryPerformanceCounter(&m_lastTick);
    if (m_config) {
        m_layoutAnim = (m_config->layoutMode == 1) ? 1.0f : 0.0f;
        for (int pos = 0; pos < 4; ++pos) {
            int mIdx = m_config->itemOrder[pos];
            if (mIdx >= 0 && mIdx < 4) m_rowVisualY[mIdx] = 236.0f + pos * 40.0f;
        }
    }
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(m_hwnd);
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void SettingsWindow::Hide() {
    if (!m_hwnd) return;
    CommitTempEdit();
    m_focusedTitleRow = -1;
    m_focusedTempIdx = -1;
    m_draggingMetric = -1;
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

    if (!m_rowAnimReady) {
        for (int pos = 0; pos < 4; ++pos) {
            int mIdx = m_config->itemOrder[pos];
            if (mIdx >= 0 && mIdx < 4) m_rowVisualY[mIdx] = 236.0f + pos * 40.0f;
        }
        m_rowAnimReady = true;
    }

    auto addZone = [&](int id, D2D1_RECT_F rc) {
        m_hitZones.push_back({id, rc});
    };

    IDWriteTextFormat* fmtH1 = nullptr;
    IDWriteTextFormat* fmtSec = nullptr;
    IDWriteTextFormat* fmtTh = nullptr;
    IDWriteTextFormat* fmtLbl = nullptr;
    IDWriteTextFormat* fmtRight = nullptr;
    IDWriteTextFormat* fmtBtn = nullptr;
    IDWriteTextFormat* fmtBtnLeft = nullptr;

    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"zh-TW", &fmtH1);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-TW", &fmtSec);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.2f, L"zh-TW", &fmtTh);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.6f, L"zh-TW", &fmtLbl);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-TW", &fmtRight);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.2f, L"zh-TW", &fmtBtn);
    m_dwriteFactory->CreateTextFormat(L"Microsoft JhengHei UI", NULL, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.2f, L"zh-TW", &fmtBtnLeft);

    if (!fmtH1 || !fmtSec || !fmtTh || !fmtLbl || !fmtRight || !fmtBtn || !fmtBtnLeft) return;

    fmtH1->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtSec->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtTh->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmtTh->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtLbl->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtRight->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtBtnLeft->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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
    ID2D1SolidColorBrush* brThBg = nullptr;
    ID2D1SolidColorBrush* brBorder = nullptr;
    ID2D1SolidColorBrush* brWinBorder = nullptr;
    ID2D1SolidColorBrush* brText = nullptr;
    ID2D1SolidColorBrush* brMuted = nullptr;
    ID2D1SolidColorBrush* brSliderFill = nullptr;
    ID2D1SolidColorBrush* brTrack = nullptr;

    rt->CreateSolidColorBrush(D2D1::ColorF(0.082f, 0.086f, 0.102f, 1.0f), &brCard);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.118f, 0.125f, 0.149f, 1.0f), &brThBg);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.180f, 0.192f, 0.224f, 1.0f), &brBorder);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.240f, 0.255f, 0.295f, 1.0f), &brWinBorder);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.97f, 1.0f), &brText);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.58f, 0.61f, 0.67f, 1.0f), &brMuted);
    rt->CreateSolidColorBrush(ToD2D(m_config->btnHoverColor), &brSliderFill);
    rt->CreateSolidColorBrush(D2D1::ColorF(0.145f, 0.153f, 0.180f, 1.0f), &brTrack);

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

        if (bg) rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 4.0f, 4.0f), bg);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rc, 4.0f, 4.0f), (hov || active) ? brSliderFill : brBorder, 1.0f);

        if (fg) {
            if (faIcon >= 0 && txt.empty()) {
                DrawFaIcon(m_d2dFactory, rt, (FaIconType)faIcon, x + bw * 0.5f, y + bh * 0.5f, 12.0f, fg, bgCol, m_layoutAnim);
            } else if (faIcon >= 0) {
                float tw = measureBtnText(txt);
                float iconW = 12.0f;
                float gap = 6.0f;
                float totalW = iconW + gap + tw;
                float startX = x + (bw - totalW) * 0.5f;
                DrawFaIcon(m_d2dFactory, rt, (FaIconType)faIcon, startX + iconW * 0.5f, y + bh * 0.5f, 12.0f, fg, bgCol, m_layoutAnim);
                D2D1_RECT_F rcTxt = D2D1::RectF(startX + iconW + gap, y, startX + totalW + 4.0f, y + bh);
                rt->DrawText(txt.c_str(), (UINT32)txt.size(), fmtBtnLeft, rcTxt, fg);
            } else {
                rt->DrawText(txt.c_str(), (UINT32)txt.size(), fmtBtn, rc, fg);
            }
            fg->Release();
        }
        if (bg) bg->Release();
    };

    auto drawSlider = [&](int zoneId, const wchar_t* label, int val, int minV, int maxV, const wchar_t* unit, float x, float y, float sw, float labelW = 112.0f) {
        wchar_t lblBuf[64];
        swprintf(lblBuf, 64, L"%ls: %d%ls", label, val, unit);
        rt->DrawText(lblBuf, (UINT32)wcslen(lblBuf), fmtLbl, D2D1::RectF(x, y, x + labelW, y + 24), brText);

        float tx = x + labelW;
        float ty = y + 10.0f;
        float tw = sw - labelW;
        D2D1_RECT_F rcHit = D2D1::RectF(tx - 4, y, tx + tw + 4, y + 24);
        addZone(zoneId, rcHit);

        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + tw, ty + 4.0f), 2.0f, 2.0f), brTrack);
        float pct = std::clamp((float)(val - minV) / (float)(maxV - minV), 0.0f, 1.0f);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + tw * pct, ty + 4.0f), 2.0f, 2.0f), brSliderFill);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + tw * pct, ty + 2.0f), 6.0f, 6.0f), brText);
    };

    auto drawSwatch = [&](int zoneId, const wchar_t* prefix, COLORREF col, float x, float y, float bw, float bh) {
        D2D1_RECT_F rc = D2D1::RectF(x, y, x + bw, y + bh);
        addZone(zoneId, rc);
        bool hov = (m_hoveredZone == zoneId);
        ID2D1SolidColorBrush* swBr = nullptr;
        rt->CreateSolidColorBrush(ToD2D(col), &swBr);
        if (swBr) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(rc, 4.0f, 4.0f), swBr);
            swBr->Release();
        }
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rc, 4.0f, 4.0f), hov ? brText : brBorder, hov ? 1.5f : 1.0f);

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
        DrawFaIcon(m_d2dFactory, rt, FA_ICON_LOGO, 28.0f, 20.0f, 16.0f, brLogo);
        brLogo->Release();
    }
    rt->DrawText(L"設定", 2, fmtH1, D2D1::RectF(44, 8, 120, 32), brText);

    const wchar_t* wheelTip = L"瀏海可藉由滾輪縮放";
    rt->DrawText(wheelTip, (UINT32)wcslen(wheelTip), fmtRight, D2D1::RectF(200, 8, logW - 84, 32), brMuted);

    // Custom Minimize (—) & Close (✕) Buttons on top-right
    drawButton(ZONE_WIN_MIN,   L"", logW - 76.0f, 8.0f, 28.0f, 24.0f, false, FA_ICON_MINUS, false, false);
    drawButton(ZONE_WIN_CLOSE, L"", logW - 44.0f, 8.0f, 28.0f, 24.0f, false, FA_ICON_XMARK, false, true);

    // ==================== CARD 1: 外觀與尺寸 (4px Grid, 2x2 Sliders, No Font Size Slider) ====================
    D2D1_RECT_F rcCard1 = D2D1::RectF(16, 40, logW - 16, 184);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard1, 8.0f, 8.0f), brCard);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCard1, 8.0f, 8.0f), brBorder, 1.0f);

    DrawFaIcon(m_d2dFactory, rt, FA_ICON_SLIDERS, 36.0f, 56.0f, 12.0f, brText);
    rt->DrawText(L"外觀與尺寸", 5, fmtSec, D2D1::RectF(48, 44, 220, 68), brText);

    float effScale = m_config->GetEffectiveScale();
    int physW = (((int)std::round(m_config->GetCollapsedLogicalWidth() * effScale) + 2) / 4) * 4;
    int physH = (((int)std::round(m_config->GetCollapsedLogicalHeight() * effScale) + 2) / 4) * 4;
    wchar_t dimBuf[96];
    swprintf(dimBuf, 96, L"%d × %d px (%.2fx)", physW, physH, effScale);
    rt->DrawText(dimBuf, (UINT32)wcslen(dimBuf), fmtRight, D2D1::RectF(300, 44, logW - 28, 68), brMuted);

    // 2x2 Slider Grid (Scale %, Opacity %, Base Width px, Base Height px)
    drawSlider(ZONE_SLIDER_UISCALE, L"整體縮放", m_config->uiScalePct, 60, 220, L"%",   28,  72, 256, 112.0f);
    drawSlider(ZONE_SLIDER_OPACITY, L"不透明度", (m_config->bgOpacity * 100) / 255, 35, 100, L"%", 308, 72, 264, 112.0f);
    drawSlider(ZONE_SLIDER_WIDTH,   L"基準寬度", m_config->notchWidth, 200, 420, L" px", 28,  104, 256, 112.0f);
    drawSlider(ZONE_SLIDER_HEIGHT,  L"基準高度", m_config->notchHeight, 28,  68, L" px", 308, 104, 264, 112.0f);

    // 7 Quick Controls on bottom row of Card 1 (y = 140, h = 32)
    std::wstring layTxt = (m_config->layoutMode == 0) ? L"雙行" : L"單行";
    drawButton(ZONE_BTN_LAYOUT, layTxt, 28, 140, 80, 32, m_config->layoutMode == 1, FA_ICON_LAYOUT);

    wchar_t refTxt[32];
    swprintf(refTxt, 32, L"%d ms", m_config->refreshIntervalMs);
    drawButton(ZONE_BTN_REFRESH, refTxt, 112, 140, 76, 32, false);

    std::wstring fpsTxt = (m_config->fpsIdleMode == 0) ? L"待機: --" : L"待機: Hz";
    drawButton(ZONE_BTN_FPS_IDLE, fpsTxt, 192, 140, 84, 32, false);

    drawSwatch(ZONE_SWATCH_BG,     L"底色", m_config->bgColor,       284, 140, 68, 32);
    drawSwatch(ZONE_SWATCH_BORDER, L"邊框", m_config->borderColor,   356, 140, 68, 32);
    drawSwatch(ZONE_SWATCH_BTN,    L"按鈕", m_config->btnColor,      428, 140, 68, 32);
    drawSwatch(ZONE_SWATCH_BTNHOV, L"懸停", m_config->btnHoverColor, 500, 140, 72, 32);

    // ==================== CARD 2: 監控欄位表格 (with <th> Header + Drag-to-Reorder + Tween Animation!) ====================
    D2D1_RECT_F rcCard2 = D2D1::RectF(16, 196, logW - 16, 400);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard2, 8.0f, 8.0f), brCard);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCard2, 8.0f, 8.0f), brBorder, 1.0f);

    // Table Header (<th>) Bar (y = 204..228, h = 24)
    D2D1_RECT_F rcThBar = D2D1::RectF(24, 204, logW - 24, 228);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcThBar, 4.0f, 4.0f), brThBg);

    rt->DrawText(L"項目",   2, fmtTh, D2D1::RectF(28,  204, 128, 228), brText);
    rt->DrawText(L"溫度",   2, fmtTh, D2D1::RectF(136, 204, 204, 228), brText);
    rt->DrawText(L"標題",   2, fmtTh, D2D1::RectF(212, 204, 320, 228), brText);
    rt->DrawText(L"標題色", 3, fmtTh, D2D1::RectF(328, 204, 412, 228), brText);
    rt->DrawText(L"數值色", 3, fmtTh, D2D1::RectF(420, 204, 504, 228), brText);
    rt->DrawText(L"排序",   2, fmtTh, D2D1::RectF(512, 204, 572, 228), brText);

    const wchar_t* shortNames[4] = { L"CPU", L"GPU", L"RAM", L"FPS" };
    const FaIconType metricIcons[4] = { FA_ICON_CPU, FA_ICON_GPU, FA_ICON_RAM, FA_ICON_FPS };

    auto drawMetricRow = [&](int mIdx, bool isDragged) {
        const MetricConfig& mc = m_config->metrics[mIdx];
        float ry = m_rowVisualY[mIdx];
        int base = ZONE_ROW_BASE + mIdx * 10;

        if (isDragged) {
            D2D1_RECT_F rcRowHighlight = D2D1::RectF(24.0f, ry - 2.0f, logW - 24.0f, ry + 34.0f);
            rt->FillRoundedRectangle(D2D1::RoundedRect(rcRowHighlight, 6.0f, 6.0f), brThBg);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(rcRowHighlight, 6.0f, 6.0f), brText, 1.2f);
        }

        // 1. Enable Toggle Button (FA Icon + short name)
        drawButton(base + 1, shortNames[mIdx], 28, ry, 100, 32, mc.enabled, metricIcons[mIdx]);

        // 2. Show Temp Toggle (CPU & GPU only)
        if (mIdx == 0 || mIdx == 1) {
            drawButton(base + 2, L"°C", 136, ry, 68, 32, mc.showTemp, FA_ICON_THERM);
        } else {
            rt->DrawText(L"—", 1, fmtBtn, D2D1::RectF(136, ry, 204, ry + 32), brMuted);
        }

        // 3. Editable Custom Title Box
        D2D1_RECT_F rcTitleBox = D2D1::RectF(212, ry, 320, ry + 32);
        addZone(base + 3, rcTitleBox);
        bool focused = (m_focusedTitleRow == mIdx);
        bool hovTitle = (m_hoveredZone == base + 3);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rcTitleBox, 4.0f, 4.0f), brTrack);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rcTitleBox, 4.0f, 4.0f),
                                 (focused || hovTitle) ? brText : brBorder,
                                 focused ? 1.6f : 1.0f);

        std::wstring dispTitle = mc.title + (focused ? L"|" : L"");
        rt->DrawText(dispTitle.c_str(), (UINT32)dispTitle.size(), fmtBtn, rcTitleBox, brText);

        // 4. Title Color Swatch
        drawSwatch(base + 4, L"", mc.titleColor, 328, ry, 84, 32);

        // 5. Value Color Swatch
        drawSwatch(base + 5, L"", mc.valueColor, 420, ry, 84, 32);

        // 6. Drag Handle (FA_ICON_GRIP, shows drag cursor IDC_SIZEALL on hover/drag!)
        drawButton(base + 6, L"", 512, ry, 60, 32, isDragged, FA_ICON_GRIP);
    };

    // Draw non-dragged rows first, then the actively dragged row on top!
    for (int pos = 0; pos < 4; ++pos) {
        int mIdx = m_config->itemOrder[pos];
        if (mIdx >= 0 && mIdx < 4 && mIdx != m_draggingMetric) {
            drawMetricRow(mIdx, false);
        }
    }
    if (m_draggingMetric >= 0 && m_draggingMetric < 4) {
        drawMetricRow(m_draggingMetric, true);
    }

    // ==================== CARD 3: 溫度警戒 (可輸入 + 上下改動) 與系統狀態 ====================
    D2D1_RECT_F rcCard3 = D2D1::RectF(16, 408, logW - 16, 460);
    rt->FillRoundedRectangle(D2D1::RoundedRect(rcCard3, 8.0f, 8.0f), brCard);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(rcCard3, 8.0f, 8.0f), brBorder, 1.0f);

    auto drawTempField = [&](const wchar_t* label, int tempIdx, int zoneInput, int zoneUp, int zoneDown, int curVal, float x, float y) {
        rt->DrawText(label, (UINT32)wcslen(label), fmtLbl, D2D1::RectF(x, y, x + 36, y + 28), brText);

        D2D1_RECT_F rcBox = D2D1::RectF(x + 36, y, x + 92, y + 28);
        addZone(zoneInput, rcBox);
        bool focused = (m_focusedTempIdx == tempIdx);
        bool hov = (m_hoveredZone == zoneInput);
        rt->FillRoundedRectangle(D2D1::RoundedRect(rcBox, 4.0f, 4.0f), brTrack);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(rcBox, 4.0f, 4.0f), (focused || hov) ? brText : brBorder, focused ? 1.6f : 1.0f);

        std::wstring valStr = focused ? (m_tempEditStr + L"|°C") : (std::to_wstring(curVal) + L"°C");
        rt->DrawText(valStr.c_str(), (UINT32)valStr.size(), fmtBtn, rcBox, brText);

        drawButton(zoneUp,   L"", x + 96,  y, 24, 28, false, FA_ICON_UP);
        drawButton(zoneDown, L"", x + 124, y, 24, 28, false, FA_ICON_DOWN);
    };

    drawTempField(L"警戒", 0, ZONE_WARN_INPUT, ZONE_WARN_INC, ZONE_WARN_DEC, m_config->tempWarnC, 28.0f,  420.0f);
    drawTempField(L"高溫", 1, ZONE_CRIT_INPUT, ZONE_CRIT_INC, ZONE_CRIT_DEC, m_config->tempCritC, 188.0f, 420.0f);

    drawButton(ZONE_BTN_CLICKTHROUGH,
               m_config->clickThroughLocked ? L"穿透鎖定" : L"自動展開",
               348, 420, 112, 28,
               m_config->clickThroughLocked,
               m_config->clickThroughLocked ? FA_ICON_LOCK : FA_ICON_UNLOCK);

    drawButton(ZONE_BTN_AUTOSTART,
               L"開機自啟",
               468, 420, 104, 28,
               m_config->autoStartAdmin,
               FA_ICON_BOLT);

    // ==================== Bottom Action Bar (4px Grid) ====================
    float botY = 472.0f;
    drawButton(ZONE_BTN_RESET, L"還原",       16.0f,  botY, 276.0f, 36.0f, false, FA_ICON_RESET);
    drawButton(ZONE_BTN_SAVE,  L"儲存並關閉", 308.0f, botY, 276.0f, 36.0f, true,  FA_ICON_SAVE);

    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    rt->EndDraw();

    brCard->Release();
    brThBg->Release();
    brBorder->Release();
    brWinBorder->Release();
    brText->Release();
    brMuted->Release();
    brSliderFill->Release();
    brTrack->Release();

    fmtH1->Release();
    fmtSec->Release();
    fmtTh->Release();
    fmtLbl->Release();
    fmtRight->Release();
    fmtBtn->Release();
    fmtBtnLeft->Release();
}

void SettingsWindow::HandleZoneClick(int zoneId, float mx, float my, const D2D1_RECT_F& rc) {
    if (!m_config) return;
    if (zoneId != ZONE_WARN_INPUT && zoneId != ZONE_CRIT_INPUT) {
        CommitTempEdit();
    }
    m_focusedTitleRow = -1;

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
    case ZONE_SLIDER_UISCALE:
        m_draggingSliderZone = zoneId;
        updateSlider(m_config->uiScalePct, 60, 220, 4);
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
        m_config->ResetDefaults();
        for (int pos = 0; pos < 4; ++pos) {
            int mIdx = m_config->itemOrder[pos];
            if (mIdx >= 0 && mIdx < 4) m_rowVisualY[mIdx] = 236.0f + pos * 40.0f;
        }
        NotifyChanged();
        return;
    case ZONE_BTN_SAVE:
        NotifyChanged();
        Hide();
        return;
    }

    if (zoneId >= ZONE_ROW_BASE && zoneId < ZONE_ROW_BASE + 40) {
        int mIdx = (zoneId - ZONE_ROW_BASE) / 10;
        int sub = (zoneId - ZONE_ROW_BASE) % 10;
        if (mIdx < 0 || mIdx >= 4) return;

        if (sub == 1) {
            m_config->metrics[mIdx].enabled = !m_config->metrics[mIdx].enabled;
            NotifyChanged();
        } else if (sub == 2) {
            m_config->metrics[mIdx].showTemp = !m_config->metrics[mIdx].showTemp;
            NotifyChanged();
        } else if (sub == 3) {
            m_focusedTitleRow = mIdx;
            InvalidateRect(m_hwnd, NULL, FALSE);
        } else if (sub == 4) {
            if (PickColor(m_config->metrics[mIdx].titleColor)) NotifyChanged();
        } else if (sub == 5) {
            if (PickColor(m_config->metrics[mIdx].valueColor)) NotifyChanged();
        } else if (sub == 6) { // Start dragging this row!
            m_draggingMetric = mIdx;
            m_dragGrabOffsetY = my - m_rowVisualY[mIdx];
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
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
                float step = dt / 0.26f;
                if (m_layoutAnim < targetLay)
                    m_layoutAnim = std::min(targetLay, m_layoutAnim + step);
                else
                    m_layoutAnim = std::max(targetLay, m_layoutAnim - step);
                dirty = true;
            } else if (m_layoutAnim != targetLay) {
                m_layoutAnim = targetLay;
                dirty = true;
            }

            // High-FPS frame-rate-independent tween animation for row vertical positions
            float rowAlpha = 1.0f - std::exp(-24.0f * dt);
            for (int pos = 0; pos < 4; ++pos) {
                int mIdx = m_config->itemOrder[pos];
                if (mIdx < 0 || mIdx >= 4) continue;
                if (mIdx == m_draggingMetric) continue; // Actively dragged row follows cursor directly
                float targetY = 236.0f + pos * 40.0f;
                if (std::fabs(m_rowVisualY[mIdx] - targetY) > 0.15f) {
                    m_rowVisualY[mIdx] += (targetY - m_rowVisualY[mIdx]) * rowAlpha;
                    dirty = true;
                } else if (m_rowVisualY[mIdx] != targetY) {
                    m_rowVisualY[mIdx] = targetY;
                    dirty = true;
                }
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

        // 1. Handle active row drag-reorder with live slot swapping & tween animation
        if (m_draggingMetric >= 0 && m_draggingMetric < 4 && (wParam & MK_LBUTTON)) {
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            m_rowVisualY[m_draggingMetric] = std::clamp(my - m_dragGrabOffsetY, 236.0f, 356.0f);
            int targetPos = std::clamp((int)std::round((m_rowVisualY[m_draggingMetric] - 236.0f) / 40.0f), 0, 3);

            int curPos = -1;
            for (int p = 0; p < 4; ++p) {
                if (m_config->itemOrder[p] == m_draggingMetric) {
                    curPos = p;
                    break;
                }
            }
            if (curPos >= 0 && targetPos != curPos) {
                QueryPerformanceCounter(&m_lastTick);
                int step = (targetPos > curPos) ? 1 : -1;
                for (int p = curPos; p != targetPos; p += step) {
                    m_config->itemOrder[p] = m_config->itemOrder[p + step];
                }
                m_config->itemOrder[targetPos] = m_draggingMetric;
                NotifyChanged(false);
            } else {
                InvalidateRect(m_hwnd, NULL, FALSE);
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

        // 3. Hover detection & cursor selection (IDC_SIZEALL for drag handle, IDC_HAND for buttons)
        int hit = -1;
        for (const auto& z : m_hitZones) {
            if (mx >= z.rect.left && mx <= z.rect.right && my >= z.rect.top && my <= z.rect.bottom) {
                hit = z.id;
                break;
            }
        }
        if (IsDragZoneId(hit)) {
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
                if (m_draggingMetric < 0 && m_draggingSliderZone < 0) {
                    ReleaseCapture();
                }
                return 0;
            }
        }
        CommitTempEdit();
        m_focusedTitleRow = -1;
        InvalidateRect(m_hwnd, NULL, FALSE);
        ReleaseCapture();

        if (my < 40.0f) {
            SendMessageW(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (m_draggingMetric >= 0 || m_draggingSliderZone >= 0) {
            m_draggingMetric = -1;
            m_draggingSliderZone = -1;
            ReleaseCapture();
            if (m_config) m_config->SaveToFile(AppConfig::GetDefaultConfigPath());
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
        return 0;

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wParam;
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
    int w = (((int)std::round(600.0f * resScale) + 2) / 4) * 4;
    int h = (((int)std::round(524.0f * resScale) + 2) / 4) * 4;

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

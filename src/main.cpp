#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <psapi.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include "config.h"
#include "telemetry.h"
#include "notch_window.h"
#include "settings_window.h"

#define WM_TRAYICON (WM_USER + 200)
#define ID_TRAY_SETTINGS    2001
#define ID_TRAY_LAYOUT      2002
#define ID_TRAY_LOCK        2003
#define ID_TRAY_EXIT        2004

#define HOTKEY_TOGGLE_LOCK  3001
#define HOTKEY_OPEN_SET     3002

static NOTIFYICONDATAW g_nid = {};
static HICON g_hTrayIcon = NULL;
static AppConfig* g_pConfig = nullptr;
static NotchWindow* g_pNotch = nullptr;
static SettingsWindow* g_pSettings = nullptr;

static bool IsProcessElevated() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

static bool RelaunchAsAdmin() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = L"--elevated";
    sei.nShow = SW_NORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

static HICON CreateNotchTrayIcon() {
    int w = 16, h = 16;
    BITMAPV5HEADER bi = {};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    HDC hdc = GetDC(NULL);
    void* lpBits = nullptr;
    HBITMAP hColor = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &lpBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (hColor && lpBits) {
        DWORD* px = (DWORD*)lpBits;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (y >= 2 && y <= 13 && x >= 1 && x <= 14) {
                    if (y <= 5) px[y * w + x] = 0xFF38BDF8; // Cyan top bar
                    else if (y >= 9 && x >= 3 && x <= 12) px[y * w + x] = 0xFF4ADE80; // Green indicator
                    else px[y * w + x] = 0xFF0F172A; // Dark body
                } else {
                    px[y * w + x] = 0x00000000;
                }
            }
        }
    }

    HBITMAP hMask = CreateBitmap(w, h, 1, 1, NULL);
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = hMask;
    ii.hbmColor = hColor;
    HICON hIcon = CreateIconIndirect(&ii);
    if (hColor) DeleteObject(hColor);
    if (hMask) DeleteObject(hMask);
    return hIcon;
}

static void ShowTrayContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu || !g_pConfig) return;

    AppendMenuW(hMenu, MF_STRING | (g_pConfig->clickThroughLocked ? MF_CHECKED : 0),
                ID_TRAY_LOCK, L"穿透鎖定");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"設置");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"關閉");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

static LRESULT CALLBACK TrayHostWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayContextMenu(hwnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            if (g_pSettings) g_pSettings->Show();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SETTINGS:
            if (g_pSettings) g_pSettings->Show();
            break;
        case ID_TRAY_LAYOUT:
            if (g_pConfig && g_pNotch) {
                g_pConfig->layoutMode = (g_pConfig->layoutMode == 0) ? 1 : 0;
                g_pConfig->SaveToFile(AppConfig::GetDefaultConfigPath());
                g_pNotch->ApplyConfig();
            }
            break;
        case ID_TRAY_LOCK:
            if (g_pConfig && g_pNotch) {
                g_pNotch->SetClickThrough(!g_pConfig->clickThroughLocked);
                g_pConfig->SaveToFile(AppConfig::GetDefaultConfigPath());
                g_pNotch->ApplyConfig();
            }
            break;
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_HOTKEY:
        if (wParam == HOTKEY_TOGGLE_LOCK && g_pConfig && g_pNotch) {
            g_pNotch->SetClickThrough(!g_pConfig->clickThroughLocked);
            g_pConfig->SaveToFile(AppConfig::GetDefaultConfigPath());
            g_pNotch->ApplyConfig();
        } else if (wParam == HOTKEY_OPEN_SET && g_pSettings) {
            g_pSettings->Show();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void LogStep(const std::wstring& outDir, const char* step) {
    std::wstring p = outDir + L"\\verify_trace.txt";
    FILE* fp = _wfopen(p.c_str(), L"ab");
    if (fp) {
        fprintf(fp, "%s\n", step);
        fclose(fp);
    }
}

static LONG WINAPI VerifyVeh(PEXCEPTION_POINTERS pEx) {
    if (pEx && pEx->ExceptionRecord && pEx->ExceptionRecord->ExceptionCode >= 0x80000000) {
        FILE* fp = _wfopen(L"C:\\Users\\User\\.gemini\\antigravity\\scratch\\win-notch-monitor\\verify_out\\verify_trace.txt", L"ab");
        if (fp) {
            fprintf(fp, "  [VEH] code=0x%08lX addr=%p\n",
                    (unsigned long)pEx->ExceptionRecord->ExceptionCode,
                    pEx->ExceptionRecord->ExceptionAddress);
            fclose(fp);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static int RunSelfVerification(HINSTANCE hInstance, const std::wstring& outDir) {
    CreateDirectoryW(outDir.c_str(), NULL);
    AddVectoredExceptionHandler(1, VerifyVeh);
    LogStep(outDir, "1. Start RunSelfVerification");

    AppConfig config;
    std::wstring cfgPath = outDir + L"\\test_config.json";
    config.SaveToFile(cfgPath);
    config.LoadFromFile(cfgPath);
    LogStep(outDir, "2. Config saved and loaded");

    TelemetryEngine telemetry(true);
    LogStep(outDir, "3. TelemetryEngine constructed");
    telemetry.SetPreviewMockMedia(true);
    telemetry.PollOnce();
    TelemetrySnapshot snap = telemetry.GetSnapshot();
    LogStep(outDir, "4. Telemetry snapshot polled");

    NotchWindow notch;
    bool notchOk = notch.Create(hInstance, &config, &telemetry);
    LogStep(outDir, notchOk ? "5. NotchWindow created OK" : "5. NotchWindow FAILED");

    // 1. Save Dual-Row Collapsed Preview (Resolution-scaled default)
    config.layoutMode = 0;
    config.notchWidth = 260;
    config.notchHeight = 36;
    std::wstring bmpDual = outDir + L"\\notch_collapsed_dual.bmp";
    bool bmp1Ok = notch.SavePreviewBmp(bmpDual, false);
    LogStep(outDir, "7. Saved Dual-Row BMP");

    // 2. Save Single-Row Collapsed Preview (Auto-expands width via GetCollapsedLogicalWidth)
    config.layoutMode = 1;
    std::wstring bmpSingle = outDir + L"\\notch_collapsed_single.bmp";
    bool bmp2Ok = notch.SavePreviewBmp(bmpSingle, false);
    LogStep(outDir, "8. Saved Single-Row BMP");

    // Restore Dual-Row default
    config.layoutMode = 0;

    // 3. Save Expanded Dynamic Island Preview (with Hover Tooltip on Icon 1)
    notch.SetHoverButtonForPreview(1);
    std::wstring bmpExpanded = outDir + L"\\notch_expanded_island.bmp";
    bool bmp3Ok = notch.SavePreviewBmp(bmpExpanded, true);
    notch.SetHoverButtonForPreview(0);
    LogStep(outDir, "9. Saved Expanded BMP");

    // 4. Create & Save Settings Window Preview
    SettingsWindow settings;
    bool settingsOk = settings.Create(hInstance, &config);
    LogStep(outDir, settingsOk ? "10. SettingsWindow created OK" : "10. SettingsWindow FAILED");
    std::wstring bmpSettings = outDir + L"\\settings_panel.bmp";
    bool bmp4Ok = settings.SaveWindowBmp(bmpSettings);

    // 5. Verify Custom Titles & Custom Colors (e.g. "9800X3D", "4070S", Custom Theme)
    config.metrics[0].title = L"9800X3D";
    config.metrics[0].titleColor = RGB(244, 114, 182); // Pink-Magenta
    config.metrics[1].title = L"RTX4070S";
    config.metrics[1].titleColor = RGB(52, 211, 153);  // Emerald
    config.notchWidth = 300;
    std::wstring bmpCustom = outDir + L"\\notch_custom_theme.bmp";
    notch.SavePreviewBmp(bmpCustom, false);
    config.ResetDefaults();

    // Trim startup transient pages and render active frame to measure steady-state working set
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    telemetry.PollOnce();
    notch.ForceRedraw();
    Sleep(50);

    // Measure memory footprint
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    double workingSetMb = (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
    double privateMb = (double)pmc.PrivateUsage / (1024.0 * 1024.0);

    // Write verification report JSON
    std::wstring reportPath = outDir + L"\\verify_report.json";
    FILE* fp = _wfopen(reportPath.c_str(), L"wb");
    if (fp) {
        char utf8Cpu[128] = {0}, utf8Gpu[128] = {0};
        WideCharToMultiByte(CP_UTF8, 0, snap.cpuName.c_str(), -1, utf8Cpu, 127, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, snap.gpuName.c_str(), -1, utf8Gpu, 127, NULL, NULL);

        fprintf(fp,
            "{\n"
            "  \"status\": \"PASS\",\n"
            "  \"elevated\": %s,\n"
            "  \"notchWindowCreated\": %s,\n"
            "  \"settingsWindowCreated\": %s,\n"
            "  \"previewsGenerated\": [%s, %s, %s, %s],\n"
            "  \"memoryWorkingSetMB\": %.2f,\n"
            "  \"memoryPrivateMB\": %.2f,\n"
            "  \"telemetry\": {\n"
            "    \"cpuName\": \"%s\",\n"
            "    \"cpuUsagePct\": %d,\n"
            "    \"cpuTempC\": %d,\n"
            "    \"cpuTempFromLhm\": %s,\n"
            "    \"cpuClockGhz\": %.2f,\n"
            "    \"cpuPowerW\": %.1f,\n"
            "    \"gpuName\": \"%s\",\n"
            "    \"gpuUsagePct\": %d,\n"
            "    \"gpuTempC\": %d,\n"
            "    \"gpuVramUsedGb\": %.2f,\n"
            "    \"gpuVramTotalGb\": %.2f,\n"
            "    \"gpuPowerW\": %.1f,\n"
            "    \"gpuFromNvml\": %s,\n"
            "    \"ramUsagePct\": %d,\n"
            "    \"ramUsedGb\": %.2f,\n"
            "    \"ramTotalGb\": %.2f,\n"
            "    \"monitorRefreshHz\": %d,\n"
            "    \"currentFps\": %d\n"
            "  }\n"
            "}\n",
            IsProcessElevated() ? "true" : "false",
            notchOk ? "true" : "false",
            settingsOk ? "true" : "false",
            bmp1Ok ? "true" : "false",
            bmp2Ok ? "true" : "false",
            bmp3Ok ? "true" : "false",
            bmp4Ok ? "true" : "false",
            workingSetMb,
            privateMb,
            utf8Cpu,
            snap.cpuUsagePct,
            snap.cpuTempC,
            snap.cpuTempFromLhm ? "true" : "false",
            snap.cpuClockGhz,
            snap.cpuPowerW,
            utf8Gpu,
            snap.gpuUsagePct,
            snap.gpuTempC,
            snap.gpuVramUsedGb,
            snap.gpuVramTotalGb,
            snap.gpuPowerW,
            snap.gpuFromNvml ? "true" : "false",
            snap.ramUsagePct,
            snap.ramUsedGb,
            snap.ramTotalGb,
            snap.monitorRefreshHz,
            snap.currentFps
        );
        fclose(fp);
    }

    telemetry.Stop();
    notch.Destroy();
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    std::wstring cmdLine = lpCmdLine ? lpCmdLine : L"";

    // Handle automated verification mode (--verify <dir>)
    size_t verifyPos = cmdLine.find(L"--verify");
    if (verifyPos != std::wstring::npos) {
        std::wstring outDir = L"C:\\Users\\User\\.gemini\\antigravity\\scratch\\win-notch-monitor\\verify_out";
        size_t argPos = verifyPos + 8;
        while (argPos < cmdLine.size() && cmdLine[argPos] == L' ') argPos++;
        if (argPos < cmdLine.size()) {
            std::wstring rawArg = cmdLine.substr(argPos);
            // Trim trailing and leading whitespace and quotes robustly
            while (!rawArg.empty() && (rawArg.back() == L' ' || rawArg.back() == L'\t' || rawArg.back() == L'\r' || rawArg.back() == L'\n' || rawArg.back() == L'"')) {
                rawArg.pop_back();
            }
            while (!rawArg.empty() && (rawArg.front() == L' ' || rawArg.front() == L'\t' || rawArg.front() == L'"')) {
                rawArg.erase(0, 1);
            }
            if (!rawArg.empty()) {
                outDir = rawArg;
            }
        }
        return RunSelfVerification(hInstance, outDir);
    }

    // Force Administrator elevation unless --no-elevate or --elevated is passed
    if (!IsProcessElevated() &&
        cmdLine.find(L"--no-elevate") == std::wstring::npos &&
        cmdLine.find(L"--elevated") == std::wstring::npos) {
        if (RelaunchAsAdmin()) {
            return 0;
        }
    }

    // Close any previous running instance (so launching a newly built exe replaces the old one cleanly)
    DWORD myPid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID != myPid &&
                    (_wcsnicmp(pe.szExeFile, L"RCNotchMonitor", 14) == 0 ||
                     _wcsnicmp(pe.szExeFile, L"WinNotchMonitor", 15) == 0)) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        WaitForSingleObject(hProc, 1500);
                        CloseHandle(hProc);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    // Also close any old overlay/tray window if still lingering
    HWND hOldTray = FindWindowW(L"RCNotchMonitor_TrayHost", NULL);
    if (hOldTray) SendMessageTimeoutW(hOldTray, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 500, NULL);

    // Single instance mutex
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Local\\RCNotchMonitor_SingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    timeBeginPeriod(1);

    AppConfig config;
    config.LoadFromFile(AppConfig::GetDefaultConfigPath());
    g_pConfig = &config;

    TelemetryEngine telemetry;
    telemetry.Start(config.refreshIntervalMs);

    NotchWindow notch;
    if (!notch.Create(hInstance, &config, &telemetry)) {
        telemetry.Stop();
        return 1;
    }
    g_pNotch = &notch;

    SettingsWindow settings;
    settings.Create(hInstance, &config);
    g_pSettings = &settings;

    notch.onOpenSettings = [&]() {
        settings.Show();
    };
    notch.onConfigChanged = [&]() {
        settings.RefreshIfVisible();
    };
    settings.onConfigUpdated = [&]() {
        notch.ApplyConfig();
    };

    // Create hidden message window for System Tray & Global Hotkeys
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayHostWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"RCNotchMonitor_TrayHost";
    RegisterClassExW(&wc);

    HWND hTrayWnd = CreateWindowExW(0, wc.lpszClassName, L"TrayHost", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    g_hTrayIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!g_hTrayIcon) {
        g_hTrayIcon = CreateNotchTrayIcon();
    }
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hTrayWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_hTrayIcon ? g_hTrayIcon : LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Windows 螢幕瀏海系統監控 (右鍵設定 / Ctrl+Alt+N 切換穿透)");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    RegisterHotKey(hTrayWnd, HOTKEY_TOGGLE_LOCK, MOD_CONTROL | MOD_ALT, 'N');
    RegisterHotKey(hTrayWnd, HOTKEY_OPEN_SET, MOD_CONTROL | MOD_ALT, 'S');

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(hTrayWnd, HOTKEY_TOGGLE_LOCK);
    UnregisterHotKey(hTrayWnd, HOTKEY_OPEN_SET);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_hTrayIcon) DestroyIcon(g_hTrayIcon);

    telemetry.Stop();
    notch.Destroy();
    timeEndPeriod(1);
    CloseHandle(hMutex);
    return 0;
}

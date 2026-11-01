#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>

struct MonitorEntry {
    int index;
    HMONITOR hMon;
    RECT rcMonitor;
    RECT rcWork;
    bool isPrimary;
    std::wstring deviceName;
    std::wstring label;
};

inline std::vector<MonitorEntry> GetAllMonitors() {
    std::vector<MonitorEntry> list;
    EnumDisplayMonitors(NULL, NULL, [](HMONITOR hMon, HDC, LPRECT, LPARAM lParam) -> BOOL {
        auto* vec = reinterpret_cast<std::vector<MonitorEntry>*>(lParam);
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, (LPMONITORINFO)&mi)) {
            MonitorEntry me;
            me.index = (int)vec->size();
            me.hMon = hMon;
            me.rcMonitor = mi.rcMonitor;
            me.rcWork = mi.rcWork;
            me.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            me.deviceName = mi.szDevice;
            int w = mi.rcMonitor.right - mi.rcMonitor.left;
            int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
            wchar_t buf[96];
            if (me.isPrimary) {
                swprintf(buf, 96, L"螢幕 %d (主 %d×%d)", me.index + 1, w, h);
            } else {
                swprintf(buf, 96, L"螢幕 %d (%d×%d)", me.index + 1, w, h);
            }
            me.label = buf;
            vec->push_back(me);
        }
        return TRUE;
    }, (LPARAM)&list);

    if (list.empty()) {
        MonitorEntry def;
        def.index = 0;
        def.hMon = NULL;
        def.rcMonitor = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
        def.rcWork = def.rcMonitor;
        def.isPrimary = true;
        def.deviceName = L"\\\\.\\DISPLAY1";
        def.label = L"螢幕 1 (主)";
        list.push_back(def);
    }
    return list;
}

inline MonitorEntry GetTargetMonitor(int targetIdx, const std::wstring& targetDevice = L"") {
    auto list = GetAllMonitors();
    if (!targetDevice.empty()) {
        for (const auto& m : list) {
            if (m.deviceName == targetDevice) return m;
        }
    }
    if (targetIdx >= 0 && targetIdx < (int)list.size()) {
        return list[targetIdx];
    }
    for (const auto& m : list) {
        if (m.isPrimary) return m;
    }
    return list[0];
}

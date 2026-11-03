#include "config.h"
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>

std::wstring ColorToHex(COLORREF c) {
    wchar_t buf[16];
    swprintf(buf, 16, L"#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    return buf;
}

COLORREF HexToColor(const std::wstring& hex, COLORREF fallback) {
    std::wstring s = hex;
    if (!s.empty() && s[0] == L'#') s = s.substr(1);
    if (s.length() != 6) return fallback;
    unsigned int r = 0, g = 0, b = 0;
    if (swscanf(s.c_str(), L"%02x%02x%02x", &r, &g, &b) == 3) {
        return RGB(r, g, b);
    }
    return fallback;
}

AppConfig::AppConfig() {
    ResetDefaults();
}

void AppConfig::ResetDefaults() {
    configVersion = 5;
    uiScalePct = 100;
    notchWidth = 260;
    notchHeight = 36;
    expandedWidth = 420;
    expandedHeight = 176;
    cornerRadius = 24;
    layoutMode = 0; // 0 = Dual Row, 1 = Single Row
    refreshIntervalMs = 500;
    fontSize = 11;
    bgOpacity = 240;
    bgColor = RGB(12, 14, 20);
    borderColor = RGB(45, 55, 72);
    btnColor = RGB(30, 32, 38);
    btnHoverColor = RGB(244, 244, 246);
    accentColor = RGB(244, 244, 246);

    tempWarnC = 75;
    tempCritC = 85;
    warnColor = RGB(251, 146, 60);
    critColor = RGB(248, 113, 113);

    clickThroughLocked = false;
    fpsIdleMode = 0;
    autoStartAdmin = false;
    gpuTarget = 0;

    targetMonitor = 0;
    targetMonitorDevice = L"";
    positionOffsetPct = 50;

    mediaEnabled = true;
    mediaDockSide = 0; // 0 = Left, 1 = Right
    mediaTitleColor = RGB(248, 250, 252);
    mediaWaveColor = RGB(74, 222, 128);

    // 0: CPU
    metrics[0].enabled = true;
    metrics[0].showTemp = true;
    metrics[0].title = L"CPU";
    metrics[0].titleColor = RGB(56, 189, 248);   // #38BDF8 Cyan
    metrics[0].valueColor = RGB(248, 250, 252);

    // 1: GPU
    metrics[1].enabled = true;
    metrics[1].showTemp = true;
    metrics[1].title = L"GPU";
    metrics[1].titleColor = RGB(74, 222, 128);   // #4ADE80 Green
    metrics[1].valueColor = RGB(248, 250, 252);

    // 2: RAM
    metrics[2].enabled = true;
    metrics[2].showTemp = false;
    metrics[2].title = L"RAM";
    metrics[2].titleColor = RGB(192, 132, 252);  // #C084FC Purple
    metrics[2].valueColor = RGB(248, 250, 252);

    // 3: FPS
    metrics[3].enabled = true;
    metrics[3].showTemp = false;
    metrics[3].title = L"FPS";
    metrics[3].titleColor = RGB(250, 204, 21);   // #FACC15 Gold
    metrics[3].valueColor = RGB(248, 250, 252);

    itemOrder[0] = 0;
    itemOrder[1] = 1;
    itemOrder[2] = 2;
    itemOrder[3] = 3;
}

int AppConfig::GetActiveMetricCount() const {
    int c = 0;
    for (int i = 0; i < 4; ++i) {
        if (metrics[i].enabled) c++;
    }
    return std::max(1, c);
}

float AppConfig::GetCollapsedLogicalWidth() const {
    float w = (float)((notchWidth / 4) * 4);
    if (layoutMode == 1) {
        int active = GetActiveMetricCount();
        if (active >= 4) w = notchWidth * 1.76f;
        else if (active == 3) w = notchWidth * 1.36f;
    }
    return (float)(((int)std::round(w / 4.0f)) * 4);
}

float AppConfig::GetCollapsedLogicalHeight() const {
    int h = (notchHeight / 4) * 4;
    if (layoutMode == 1) {
        return (float)std::max(28, ((h - 8) / 4) * 4);
    }
    return (float)std::max(28, h);
}

float AppConfig::GetAutoResolutionScale() {
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    if (scrW <= 1920) return 1.0f;
    float scale = std::sqrt((float)scrW / 1920.0f);
    return std::clamp(scale, 1.0f, 2.2f);
}

float AppConfig::GetEffectiveScale() const {
    return GetAutoResolutionScale() * ((float)uiScalePct / 100.0f);
}

std::wstring AppConfig::GetDefaultConfigPath() {
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return path.substr(0, pos + 1) + L"config.json";
    }
    return L"config.json";
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &res[0], sz, NULL, NULL);
    return res;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &res[0], sz);
    return res;
}

static std::wstring ExtractJsonString(const std::wstring& json, const std::wstring& key, const std::wstring& defVal) {
    std::wstring searchKey = L"\"" + key + L"\"";
    size_t pos = json.find(searchKey);
    if (pos == std::wstring::npos) return defVal;
    pos = json.find(L':', pos + searchKey.size());
    if (pos == std::wstring::npos) return defVal;
    size_t q1 = json.find(L'"', pos + 1);
    if (q1 == std::wstring::npos) return defVal;
    size_t q2 = json.find(L'"', q1 + 1);
    if (q2 == std::wstring::npos) return defVal;
    return json.substr(q1 + 1, q2 - q1 - 1);
}

static int ExtractJsonInt(const std::wstring& json, const std::wstring& key, int defVal) {
    std::wstring searchKey = L"\"" + key + L"\"";
    size_t pos = json.find(searchKey);
    if (pos == std::wstring::npos) return defVal;
    pos = json.find(L':', pos + searchKey.size());
    if (pos == std::wstring::npos) return defVal;
    pos++;
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t' || json[pos] == L'\r' || json[pos] == L'\n')) pos++;
    return _wtoi(json.c_str() + pos);
}

static bool ExtractJsonBool(const std::wstring& json, const std::wstring& key, bool defVal) {
    std::wstring searchKey = L"\"" + key + L"\"";
    size_t pos = json.find(searchKey);
    if (pos == std::wstring::npos) return defVal;
    pos = json.find(L':', pos + searchKey.size());
    if (pos == std::wstring::npos) return defVal;
    pos++;
    while (pos < json.size() && (json[pos] == L' ' || json[pos] == L'\t' || json[pos] == L'\r' || json[pos] == L'\n')) pos++;
    if (json.compare(pos, 4, L"true") == 0) return true;
    if (json.compare(pos, 5, L"false") == 0) return false;
    return defVal;
}

bool AppConfig::LoadFromFile(const std::wstring& path) {
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0) {
        fclose(fp);
        return false;
    }
    std::string raw(len, 0);
    fread(&raw[0], 1, len, fp);
    fclose(fp);

    std::wstring json = Utf8ToWide(raw);

    int ver = ExtractJsonInt(json, L"configVersion", 1);
    uiScalePct = std::clamp(ExtractJsonInt(json, L"uiScalePct", uiScalePct), 60, 220);
    notchWidth = (std::clamp(ExtractJsonInt(json, L"notchWidth", notchWidth), 200, 420) / 4) * 4;
    notchHeight = (std::clamp(ExtractJsonInt(json, L"notchHeight", notchHeight), 28, 68) / 4) * 4;
    cornerRadius = (std::clamp(ExtractJsonInt(json, L"cornerRadius", cornerRadius), 8, 32) / 4) * 4;
    layoutMode = std::clamp(ExtractJsonInt(json, L"layoutMode", layoutMode), 0, 1);
    refreshIntervalMs = std::clamp(ExtractJsonInt(json, L"refreshIntervalMs", refreshIntervalMs), 200, 5000);
    fontSize = 11;
    bgOpacity = std::clamp(ExtractJsonInt(json, L"bgOpacity", bgOpacity), 80, 255);
    bgColor = HexToColor(ExtractJsonString(json, L"bgColor", ColorToHex(bgColor)), bgColor);
    borderColor = HexToColor(ExtractJsonString(json, L"borderColor", ColorToHex(borderColor)), borderColor);
    btnColor = HexToColor(ExtractJsonString(json, L"btnColor", ColorToHex(btnColor)), btnColor);
    btnHoverColor = HexToColor(ExtractJsonString(json, L"btnHoverColor", ColorToHex(btnHoverColor)), btnHoverColor);
    tempWarnC = std::clamp(ExtractJsonInt(json, L"tempWarnC", tempWarnC), 50, 100);
    tempCritC = std::clamp(ExtractJsonInt(json, L"tempCritC", tempCritC), 60, 110);
    clickThroughLocked = ExtractJsonBool(json, L"clickThroughLocked", clickThroughLocked);
    fpsIdleMode = std::clamp(ExtractJsonInt(json, L"fpsIdleMode", fpsIdleMode), 0, 1);
    autoStartAdmin = ExtractJsonBool(json, L"autoStartAdmin", autoStartAdmin);
    gpuTarget = std::clamp(ExtractJsonInt(json, L"gpuTarget", gpuTarget), 0, 2);

    targetMonitor = std::max(0, ExtractJsonInt(json, L"targetMonitor", targetMonitor));
    targetMonitorDevice = ExtractJsonString(json, L"targetMonitorDevice", targetMonitorDevice);
    positionOffsetPct = std::clamp(ExtractJsonInt(json, L"positionOffsetPct", positionOffsetPct), 0, 100);

    mediaEnabled = ExtractJsonBool(json, L"mediaEnabled", mediaEnabled);
    mediaDockSide = std::clamp(ExtractJsonInt(json, L"mediaDockSide", mediaDockSide), 0, 1);
    mediaTitleColor = HexToColor(ExtractJsonString(json, L"mediaTitleColor", ColorToHex(mediaTitleColor)), mediaTitleColor);
    mediaWaveColor = HexToColor(ExtractJsonString(json, L"mediaWaveColor", ColorToHex(mediaWaveColor)), mediaWaveColor);

    if (ver < 3) {
        uiScalePct = 100;
        notchWidth = 260;
        notchHeight = 36;
        fontSize = 11;
    }
    if (ver < 4) {
        btnColor = RGB(30, 32, 38);
        btnHoverColor = RGB(244, 244, 246);
    }
    configVersion = 5;

    const wchar_t* prefixes[4] = {L"cpu", L"gpu", L"ram", L"fps"};
    for (int i = 0; i < 4; ++i) {
        std::wstring p = prefixes[i];
        metrics[i].enabled = ExtractJsonBool(json, p + L"_enabled", metrics[i].enabled);
        metrics[i].showTemp = ExtractJsonBool(json, p + L"_showTemp", metrics[i].showTemp);
        metrics[i].title = ExtractJsonString(json, p + L"_title", metrics[i].title);
        metrics[i].titleColor = HexToColor(ExtractJsonString(json, p + L"_titleColor", ColorToHex(metrics[i].titleColor)), metrics[i].titleColor);
        metrics[i].valueColor = HexToColor(ExtractJsonString(json, p + L"_valueColor", ColorToHex(metrics[i].valueColor)), metrics[i].valueColor);
        itemOrder[i] = std::clamp(ExtractJsonInt(json, L"order_" + std::to_wstring(i), itemOrder[i]), 0, 3);
    }

    return true;
}

bool AppConfig::SaveToFile(const std::wstring& path) const {
    std::wstringstream ss;
    ss << L"{\n";
    ss << L"  \"configVersion\": 5,\n";
    ss << L"  \"uiScalePct\": " << uiScalePct << L",\n";
    ss << L"  \"notchWidth\": " << notchWidth << L",\n";
    ss << L"  \"notchHeight\": " << notchHeight << L",\n";
    ss << L"  \"cornerRadius\": " << cornerRadius << L",\n";
    ss << L"  \"layoutMode\": " << layoutMode << L",\n";
    ss << L"  \"refreshIntervalMs\": " << refreshIntervalMs << L",\n";
    ss << L"  \"fontSize\": " << fontSize << L",\n";
    ss << L"  \"bgOpacity\": " << bgOpacity << L",\n";
    ss << L"  \"bgColor\": \"" << ColorToHex(bgColor) << L"\",\n";
    ss << L"  \"borderColor\": \"" << ColorToHex(borderColor) << L"\",\n";
    ss << L"  \"btnColor\": \"" << ColorToHex(btnColor) << L"\",\n";
    ss << L"  \"btnHoverColor\": \"" << ColorToHex(btnHoverColor) << L"\",\n";
    ss << L"  \"tempWarnC\": " << tempWarnC << L",\n";
    ss << L"  \"tempCritC\": " << tempCritC << L",\n";
    ss << L"  \"clickThroughLocked\": " << (clickThroughLocked ? L"true" : L"false") << L",\n";
    ss << L"  \"fpsIdleMode\": " << fpsIdleMode << L",\n";
    ss << L"  \"autoStartAdmin\": " << (autoStartAdmin ? L"true" : L"false") << L",\n";
    ss << L"  \"gpuTarget\": " << gpuTarget << L",\n";
    ss << L"  \"targetMonitor\": " << targetMonitor << L",\n";
    ss << L"  \"targetMonitorDevice\": \"" << targetMonitorDevice << L"\",\n";
    ss << L"  \"positionOffsetPct\": " << positionOffsetPct << L",\n";
    ss << L"  \"mediaEnabled\": " << (mediaEnabled ? L"true" : L"false") << L",\n";
    ss << L"  \"mediaDockSide\": " << mediaDockSide << L",\n";
    ss << L"  \"mediaTitleColor\": \"" << ColorToHex(mediaTitleColor) << L"\",\n";
    ss << L"  \"mediaWaveColor\": \"" << ColorToHex(mediaWaveColor) << L"\",\n";

    const wchar_t* prefixes[4] = {L"cpu", L"gpu", L"ram", L"fps"};
    for (int i = 0; i < 4; ++i) {
        std::wstring p = prefixes[i];
        ss << L"  \"" << p << L"_enabled\": " << (metrics[i].enabled ? L"true" : L"false") << L",\n";
        ss << L"  \"" << p << L"_showTemp\": " << (metrics[i].showTemp ? L"true" : L"false") << L",\n";
        ss << L"  \"" << p << L"_title\": \"" << metrics[i].title << L"\",\n";
        ss << L"  \"" << p << L"_titleColor\": \"" << ColorToHex(metrics[i].titleColor) << L"\",\n";
        ss << L"  \"" << p << L"_valueColor\": \"" << ColorToHex(metrics[i].valueColor) << L"\",\n";
    }
    for (int i = 0; i < 4; ++i) {
        ss << L"  \"order_" << i << L"\": " << itemOrder[i] << (i == 3 ? L"\n" : L",\n");
    }
    ss << L"}\n";

    std::string utf8 = WideToUtf8(ss.str());
    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (!fp) return false;
    fwrite(utf8.data(), 1, utf8.size(), fp);
    fclose(fp);
    return true;
}

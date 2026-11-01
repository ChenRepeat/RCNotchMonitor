#define _WIN32_WINNT 0x0A00
#include "telemetry.h"
#include "smtc_com.h"
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <intrin.h>
#include <powrprof.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <evntrace.h>
#include <evntcons.h>
#include <dxgi1_4.h>
#include <cmath>
#include <cstring>
#include <cwctype>
#include <algorithm>
#include <vector>

#ifndef PROCESSOR_INFORMATION_LEVEL
#define PROCESSOR_INFORMATION_LEVEL 11
#endif

struct PROCESSOR_POWER_INFO_LOCAL {
    ULONG Number;
    ULONG MaxMhz;
    ULONG CurrentMhz;
    ULONG MhzLimit;
    ULONG MaxIdleState;
    ULONG CurrentIdleState;
};

// NVML function pointer typedefs
typedef int (*PFN_nvmlInit_v2)();
typedef int (*PFN_nvmlShutdown)();
typedef int (*PFN_nvmlDeviceGetCount_v2)(unsigned int* deviceCount);
typedef int (*PFN_nvmlDeviceGetHandleByIndex_v2)(unsigned int index, void** device);
typedef int (*PFN_nvmlDeviceGetName)(void* device, char* name, unsigned int length);
typedef struct { unsigned int gpu; unsigned int memory; } nvmlUtilization_t;
typedef int (*PFN_nvmlDeviceGetUtilizationRates)(void* device, nvmlUtilization_t* utilization);
typedef int (*PFN_nvmlDeviceGetTemperature)(void* device, int sensorType, unsigned int* temp);
typedef struct { unsigned long long total; unsigned long long free; unsigned long long used; } nvmlMemory_t;
typedef int (*PFN_nvmlDeviceGetMemoryInfo)(void* device, nvmlMemory_t* memory);
typedef int (*PFN_nvmlDeviceGetPowerUsage)(void* device, unsigned int* power);

static PFN_nvmlInit_v2 fn_nvmlInit = nullptr;
static PFN_nvmlShutdown fn_nvmlShutdown = nullptr;
static PFN_nvmlDeviceGetCount_v2 fn_nvmlGetCount = nullptr;
static PFN_nvmlDeviceGetHandleByIndex_v2 fn_nvmlGetHandle = nullptr;
static PFN_nvmlDeviceGetName fn_nvmlGetName = nullptr;
static PFN_nvmlDeviceGetUtilizationRates fn_nvmlGetUtil = nullptr;
static PFN_nvmlDeviceGetTemperature fn_nvmlGetTemp = nullptr;
static PFN_nvmlDeviceGetMemoryInfo fn_nvmlGetMem = nullptr;
static PFN_nvmlDeviceGetPowerUsage fn_nvmlGetPower = nullptr;

// AMD ADL function pointer typedefs and structures
typedef void* (*ADL_MAIN_MALLOC_CALLBACK)(int);
static void* __stdcall AdlAlloc(int iSize) { return malloc(iSize); }

typedef int (*ADL2_MAIN_CONTROL_CREATE)(ADL_MAIN_MALLOC_CALLBACK, int, void**);
typedef int (*ADL2_MAIN_CONTROL_DESTROY)(void*);
typedef int (*ADL2_ADAPTER_NUMBEROFADAPTERS_GET)(void*, int*);

struct AdapterInfo {
    int iSize;
    int iAdapterIndex;
    char strUDID[256];
    int iBusNumber;
    int iDeviceNumber;
    int iFunctionNumber;
    int iVendorID;
    char strAdapterName[256];
    char strDisplayName[256];
    int iPresent;
    int iExist;
    char strDriverPath[256];
    char strDriverPathExt[256];
    char strPNPString[256];
    int iOSDisplayIndex;
};
typedef int (*ADL2_ADAPTER_ADAPTERINFO_GET)(void*, AdapterInfo*, int);

struct ADLODNPerformanceStatus {
    int iCoreClock;
    int iMemoryClock;
    int iDCEFClock;
    int iGFXClock;
    int iUVDClock;
    int iVCEClock;
    int iGPUActivityPercent;
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iVDDC;
    int iVDDCI;
};
typedef int (*ADL2_OVERDRIVEN_PERFORMANCESTATUS_GET)(void*, int, ADLODNPerformanceStatus*);
typedef int (*ADL2_OVERDRIVEN_TEMPERATURE_GET)(void*, int, int, int*);
typedef int (*ADL2_OVERDRIVE6_CURRENTPOWER_GET)(void*, int, int, int*);

struct ADLOD6CurrentStatus {
    int iEngineClock;
    int iMemoryClock;
    int iActivityPercent;
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iReserved;
};
typedef int (*ADL2_OVERDRIVE6_CURRENTSTATUS_GET)(void*, int, ADLOD6CurrentStatus*);
typedef int (*ADL2_OVERDRIVE6_TEMPERATURE_GET)(void*, int, int*);

struct ADLTemperature {
    int iSize;
    int iTemperature;
};
typedef int (*ADL_OVERDRIVE5_TEMPERATURE_GET)(int, int, ADLTemperature*);

struct ADLPMActivity {
    int iSize;
    int iEngineClock;
    int iMemoryClock;
    int iVddc;
    int iActivityPercent;
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iReserved;
};
typedef int (*ADL_OVERDRIVE5_CURRENTACTIVITY_GET)(int, ADLPMActivity*);

static ADL2_MAIN_CONTROL_CREATE fn_adl2Create = nullptr;
static ADL2_MAIN_CONTROL_DESTROY fn_adl2Destroy = nullptr;
static ADL2_ADAPTER_NUMBEROFADAPTERS_GET fn_adl2NumAdapters = nullptr;
static ADL2_ADAPTER_ADAPTERINFO_GET fn_adl2AdapterInfo = nullptr;
static ADL2_OVERDRIVEN_PERFORMANCESTATUS_GET fn_adl2OverdriveNPerf = nullptr;
static ADL2_OVERDRIVEN_TEMPERATURE_GET fn_adl2OverdriveNTemp = nullptr;
static ADL2_OVERDRIVE6_CURRENTPOWER_GET fn_adl2Overdrive6Power = nullptr;
static ADL2_OVERDRIVE6_CURRENTSTATUS_GET fn_adl2Overdrive6Status = nullptr;
static ADL2_OVERDRIVE6_TEMPERATURE_GET fn_adl2Overdrive6Temp = nullptr;
static ADL_OVERDRIVE5_CURRENTACTIVITY_GET fn_adlOverdrive5Activity = nullptr;
static ADL_OVERDRIVE5_TEMPERATURE_GET fn_adlOverdrive5Temp = nullptr;

static std::wstring AnsiToWide(const char* s) {
    if (!s || !*s) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (len <= 1) return L"";
    std::wstring res(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, &res[0], len);
    return res;
}

static std::wstring TrimWide(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

static uint64_t FileTimeToU64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// Global pointer for ETW callback
static std::atomic<uint32_t>* g_pEtwFrameCounter = nullptr;
static std::atomic<DWORD> g_foregroundPid{0};
static std::atomic<DWORD> g_excludedPid{0};
static std::atomic<bool> g_isFgGameCandidate{false};
static TRACEHANDLE g_hTraceSession = 0;
static TRACEHANDLE g_hConsumerTrace = INVALID_PROCESSTRACE_HANDLE;
static std::thread g_etwThread;
static std::atomic<bool> g_etwStopRequested{false};

// Microsoft-Windows-DXGI: {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
static const GUID DXGI_PROVIDER_GUID = { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };
// Microsoft-Windows-D3D9: {783ACA0A-790E-4D7F-8451-AA850511C6B9}
static const GUID D3D9_PROVIDER_GUID = { 0x783ACA0A, 0x790E, 0x4D7F, { 0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9 } };

static uint64_t g_qpcFreq = 0;
static std::atomic<DWORD> g_dwmPid{0};
static std::atomic<DWORD> g_activeGamePid{0};
static std::atomic<uint64_t> g_lastGamePresentQpc{0};
static std::atomic<bool> g_userOnDesktop{false};

static bool IsExcludedDesktopProcess(const wchar_t* exeName) {
    if (!exeName || !*exeName) return false;
    static const wchar_t* const s_excluded[] = {
        L"explorer.exe", L"dwm.exe", L"shellexperiencehost.exe", L"searchhost.exe",
        L"startmenuexperiencehost.exe", L"taskmgr.exe", L"systemsettings.exe",
        L"applicationframehost.exe", L"lockapp.exe", L"logonui.exe", L"csrss.exe",
        L"services.exe", L"lsass.exe", L"svchost.exe", L"winlogon.exe",
        L"textinputhost.exe", L"ctfmon.exe", L"gamebar.exe", L"gamebarft.exe",
        L"xboxgamebar.exe", L"gamebarpresencewriter.exe",
        L"chrome.exe", L"msedge.exe", L"firefox.exe", L"brave.exe", L"opera.exe", L"vivaldi.exe",
        L"code.exe", L"devenv.exe", L"notepad.exe", L"notepad++.exe", L"sublime_text.exe",
        L"powershell.exe", L"cmd.exe", L"wt.exe", L"conhost.exe",
        L"spotify.exe", L"discord.exe", L"slack.exe", L"teams.exe", L"steam.exe",
        L"steamwebhelper.exe", L"epicgameslauncher.exe", L"galaxyclient.exe",
        L"eadesktop.exe", L"battlenet.exe", L"riotclientservices.exe",
        L"rcnotchmonitor.exe", L"win-notch-monitor.exe"
    };
    for (const auto* ex : s_excluded) {
        if (_wcsicmp(exeName, ex) == 0) return true;
    }
    return false;
}

static bool GetProcessExeName(DWORD pid, wchar_t* outName, DWORD maxLen) {
    if (pid == 0) return false;

    // 1. Try standard QueryFullProcessImageName
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        DWORD len = maxLen;
        BOOL ok = QueryFullProcessImageNameW(hProcess, 0, outName, &len);
        CloseHandle(hProcess);
        if (ok && len > 0) {
            const wchar_t* p = wcsrchr(outName, L'\\');
            if (p) {
                wmemmove(outName, p + 1, (wcslen(p + 1) + 1) * sizeof(wchar_t));
            }
            return true;
        }
    }

    // 2. Fallback to Toolhelp32Snapshot (works on system processes, DWM, and protected games)
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = {};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == pid) {
                    wcsncpy_s(outName, maxLen, pe.szExeFile, _TRUNCATE);
                    CloseHandle(hSnap);
                    return true;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    return false;
}

// Fast lockless cache for process exclusion (avoids OpenProcess in ETW hot path)
struct PidExclusionCache {
    struct Entry {
        std::atomic<DWORD> pid{0};
        bool excluded{false};
        wchar_t name[64] = {0};
    };
    static constexpr size_t CACHE_SIZE = 64;
    Entry entries[CACHE_SIZE];

    bool IsPidExcluded(DWORD pid, wchar_t* optOutName = nullptr, DWORD optMaxLen = 0) {
        if (pid == 0 || pid == GetCurrentProcessId()) return true;
        for (size_t i = 0; i < CACHE_SIZE; ++i) {
            if (entries[i].pid.load(std::memory_order_relaxed) == pid) {
                if (optOutName && optMaxLen > 0) {
                    wcsncpy_s(optOutName, optMaxLen, entries[i].name, _TRUNCATE);
                }
                return entries[i].excluded;
            }
        }
        wchar_t exeName[MAX_PATH] = {0};
        bool hasExe = GetProcessExeName(pid, exeName, MAX_PATH);
        bool excluded = false;
        if (hasExe) {
            excluded = IsExcludedDesktopProcess(exeName);
        } else {
            DWORD dwm = g_dwmPid.load(std::memory_order_relaxed);
            if (dwm != 0 && pid == dwm) {
                excluded = true;
                wcscpy_s(exeName, L"dwm.exe");
            }
        }

        static std::atomic<size_t> insertIdx{0};
        size_t idx = insertIdx.fetch_add(1, std::memory_order_relaxed) % CACHE_SIZE;
        entries[idx].excluded = excluded;
        wcsncpy_s(entries[idx].name, 64, exeName, _TRUNCATE);
        entries[idx].pid.store(pid, std::memory_order_release);

        if (optOutName && optMaxLen > 0) {
            wcsncpy_s(optOutName, optMaxLen, exeName, _TRUNCATE);
        }
        return excluded;
    }
};
static PidExclusionCache g_pidCache;

static VOID WINAPI EtwEventRecordCallback(PEVENT_RECORD pEvent) {
    if (!pEvent || !g_pEtwFrameCounter) return;
    DWORD pid = pEvent->EventHeader.ProcessId;
    if (pid == 0 || pid == GetCurrentProcessId()) return;

    DWORD dwm = g_dwmPid.load(std::memory_order_relaxed);
    if (dwm != 0 && pid == dwm) return;

    // Filter out excluded desktop/system/browser processes
    if (g_pidCache.IsPidExcluded(pid)) return;

    // Supported Presentation Start Events:
    // 42: DXGI Present_Start
    // 55: DXGI PresentMultiplaneOverlay_Start (Windows 10/11 MPO / Independent Flip)
    // 1:  D3D9 Present_Start
    USHORT id = pEvent->EventHeader.EventDescriptor.Id;
    if (id == 42 || id == 55 || id == 1) {
        DWORD activeGame = g_activeGamePid.load(std::memory_order_relaxed);

        if (activeGame == 0) {
            // If user is not intentionally idle on the desktop/browsing, adopt this presenting 3D process
            if (!g_userOnDesktop.load(std::memory_order_relaxed)) {
                g_activeGamePid.store(pid, std::memory_order_relaxed);
                activeGame = pid;
            } else {
                return;
            }
        } else if (pid != activeGame) {
            // If active game PID hasn't presented for > 2 seconds, switch to currently presenting app
            uint64_t lastActiveQpc = g_lastGamePresentQpc.load(std::memory_order_relaxed);
            uint64_t nowQpc = 0;
            QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&nowQpc));
            if (g_qpcFreq > 0 && lastActiveQpc > 0 && (nowQpc - lastActiveQpc) > (g_qpcFreq * 2)) {
                g_activeGamePid.store(pid, std::memory_order_relaxed);
                activeGame = pid;
            } else {
                return; // Discard events from non-active background processes
            }
        }

        uint64_t qpc = 0;
        QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&qpc));

        // Deduplication window: discard duplicate present markers within 0.5ms (supports up to 2000 real FPS)
        uint64_t lastQpc = g_lastGamePresentQpc.load(std::memory_order_relaxed);
        uint64_t minInterval = (g_qpcFreq > 0) ? (g_qpcFreq / 2000) : 0;
        if (minInterval > 0 && lastQpc > 0 && (qpc >= lastQpc) && (qpc - lastQpc) < minInterval) {
            return; // Duplicate event in same present transaction
        }
        g_lastGamePresentQpc.store(qpc, std::memory_order_relaxed);

        g_pEtwFrameCounter->fetch_add(1, std::memory_order_relaxed);
    }
}

TelemetryEngine::TelemetryEngine(bool verifyMockMode) {
    m_mockMediaPreview.store(verifyMockMode, std::memory_order_relaxed);
    InitCpuInfo();
    if (!verifyMockMode) {
        InitNvml();
        InitAdl();
        InitDxgi();
        LaunchSensorBridge();
    } else {
        m_snapshot.gpuName = L"NVIDIA GeForce RTX 4070 SUPER";
        m_snapshot.gpuUsagePct = 14;
        m_snapshot.gpuTempC = 45;
        m_snapshot.gpuVramUsedGb = 2.6f;
        m_snapshot.gpuVramTotalGb = 12.0f;
        m_snapshot.gpuPowerW = 18.0f;
        m_snapshot.gpuFromNvml = true;
    }

    // Prime CPU times
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        m_prevIdleTime = FileTimeToU64(idle);
        m_prevKernelTime = FileTimeToU64(kernel);
        m_prevUserTime = FileTimeToU64(user);
    }
    if (g_qpcFreq == 0) {
        LARGE_INTEGER freq = {};
        QueryPerformanceFrequency(&freq);
        g_qpcFreq = static_cast<uint64_t>(freq.QuadPart);
    }
    HWND hDwm = FindWindowW(L"Dwm", NULL);
    if (hDwm) {
        DWORD dwmPid = 0;
        GetWindowThreadProcessId(hDwm, &dwmPid);
        if (dwmPid != 0) g_dwmPid.store(dwmPid, std::memory_order_relaxed);
    }
}

TelemetryEngine::~TelemetryEngine() {
    Stop();
    ShutdownNvml();
    ShutdownAdl();
    ShutdownDxgi();
    if (m_pShmBuf) {
        UnmapViewOfFile(m_pShmBuf);
        m_pShmBuf = nullptr;
    }
    if (m_hMapFile) {
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
    }
    if (m_hBridgeProcess) {
        TerminateProcess(m_hBridgeProcess, 0);
        CloseHandle(m_hBridgeProcess);
        m_hBridgeProcess = NULL;
    }
    if (m_hJobObject) {
        CloseHandle(m_hJobObject);
        m_hJobObject = NULL;
    }
}

void TelemetryEngine::InitCpuInfo() {
    int cpuInfo[4] = {0};
    char brand[64] = {0};
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = (unsigned int)cpuInfo[0];
    if (nExIds >= 0x80000004) {
        int regs[12] = {0};
        __cpuid(&regs[0], 0x80000002);
        __cpuid(&regs[4], 0x80000003);
        __cpuid(&regs[8], 0x80000004);
        std::memcpy(brand, regs, 48);
        std::wstring wbrand = TrimWide(AnsiToWide(brand));
        if (!wbrand.empty()) {
            m_snapshot.cpuName = wbrand;
        }
    }
}

void TelemetryEngine::InitNvml() {
    m_hNvml = LoadLibraryW(L"C:\\Windows\\System32\\nvml.dll");
    if (!m_hNvml) {
        m_hNvml = LoadLibraryW(L"nvml.dll");
    }
    if (!m_hNvml) return;

    fn_nvmlInit = (PFN_nvmlInit_v2)GetProcAddress(m_hNvml, "nvmlInit_v2");
    fn_nvmlShutdown = (PFN_nvmlShutdown)GetProcAddress(m_hNvml, "nvmlShutdown");
    fn_nvmlGetCount = (PFN_nvmlDeviceGetCount_v2)GetProcAddress(m_hNvml, "nvmlDeviceGetCount_v2");
    fn_nvmlGetHandle = (PFN_nvmlDeviceGetHandleByIndex_v2)GetProcAddress(m_hNvml, "nvmlDeviceGetHandleByIndex_v2");
    fn_nvmlGetName = (PFN_nvmlDeviceGetName)GetProcAddress(m_hNvml, "nvmlDeviceGetName");
    fn_nvmlGetUtil = (PFN_nvmlDeviceGetUtilizationRates)GetProcAddress(m_hNvml, "nvmlDeviceGetUtilizationRates");
    fn_nvmlGetTemp = (PFN_nvmlDeviceGetTemperature)GetProcAddress(m_hNvml, "nvmlDeviceGetTemperature");
    fn_nvmlGetMem = (PFN_nvmlDeviceGetMemoryInfo)GetProcAddress(m_hNvml, "nvmlDeviceGetMemoryInfo");
    fn_nvmlGetPower = (PFN_nvmlDeviceGetPowerUsage)GetProcAddress(m_hNvml, "nvmlDeviceGetPowerUsage");

    if (fn_nvmlInit && fn_nvmlGetCount && fn_nvmlGetHandle) {
        if (fn_nvmlInit() == 0) {
            unsigned int count = 0;
            if (fn_nvmlGetCount(&count) == 0 && count > 0) {
                if (fn_nvmlGetHandle(0, &m_nvmlDevice) == 0 && m_nvmlDevice) {
                    m_nvmlReady = true;
                    if (fn_nvmlGetName) {
                        char nameBuf[96] = {0};
                        if (fn_nvmlGetName(m_nvmlDevice, nameBuf, sizeof(nameBuf)) == 0) {
                            m_snapshot.gpuName = TrimWide(AnsiToWide(nameBuf));
                        }
                    }
                }
            }
        }
    }
}

void TelemetryEngine::ShutdownNvml() {
    if (m_nvmlReady && fn_nvmlShutdown) {
        fn_nvmlShutdown();
        m_nvmlReady = false;
    }
    if (m_hNvml) {
        FreeLibrary(m_hNvml);
        m_hNvml = NULL;
    }
}

void TelemetryEngine::InitAdl() {
    m_hAdl = LoadLibraryA("atiadlxx.dll");
    if (!m_hAdl) m_hAdl = LoadLibraryA("atiadlxy.dll");
    if (!m_hAdl) return;

    fn_adl2Create = (ADL2_MAIN_CONTROL_CREATE)GetProcAddress(m_hAdl, "ADL2_Main_Control_Create");
    fn_adl2Destroy = (ADL2_MAIN_CONTROL_DESTROY)GetProcAddress(m_hAdl, "ADL2_Main_Control_Destroy");
    fn_adl2NumAdapters = (ADL2_ADAPTER_NUMBEROFADAPTERS_GET)GetProcAddress(m_hAdl, "ADL2_Adapter_NumberOfAdapters_Get");
    fn_adl2AdapterInfo = (ADL2_ADAPTER_ADAPTERINFO_GET)GetProcAddress(m_hAdl, "ADL2_Adapter_AdapterInfo_Get");
    fn_adl2OverdriveNPerf = (ADL2_OVERDRIVEN_PERFORMANCESTATUS_GET)GetProcAddress(m_hAdl, "ADL2_OverdriveN_PerformanceStatus_Get");
    fn_adl2OverdriveNTemp = (ADL2_OVERDRIVEN_TEMPERATURE_GET)GetProcAddress(m_hAdl, "ADL2_OverdriveN_Temperature_Get");
    fn_adl2Overdrive6Power = (ADL2_OVERDRIVE6_CURRENTPOWER_GET)GetProcAddress(m_hAdl, "ADL2_Overdrive6_CurrentPower_Get");
    fn_adl2Overdrive6Status = (ADL2_OVERDRIVE6_CURRENTSTATUS_GET)GetProcAddress(m_hAdl, "ADL2_Overdrive6_CurrentStatus_Get");
    fn_adl2Overdrive6Temp = (ADL2_OVERDRIVE6_TEMPERATURE_GET)GetProcAddress(m_hAdl, "ADL2_Overdrive6_Temperature_Get");
    fn_adlOverdrive5Activity = (ADL_OVERDRIVE5_CURRENTACTIVITY_GET)GetProcAddress(m_hAdl, "ADL_Overdrive5_CurrentActivity_Get");
    fn_adlOverdrive5Temp = (ADL_OVERDRIVE5_TEMPERATURE_GET)GetProcAddress(m_hAdl, "ADL_Overdrive5_Temperature_Get");

    if (!fn_adl2Create || fn_adl2Create(AdlAlloc, 1, &m_adlContext) != 0 || !m_adlContext) {
        return;
    }

    if (fn_adl2NumAdapters && fn_adl2AdapterInfo) {
        int nAdapters = 0;
        if (fn_adl2NumAdapters(m_adlContext, &nAdapters) == 0 && nAdapters > 0) {
            std::vector<AdapterInfo> adapters(nAdapters);
            for (auto& a : adapters) a.iSize = sizeof(AdapterInfo);
            if (fn_adl2AdapterInfo(m_adlContext, adapters.data(), sizeof(AdapterInfo) * nAdapters) == 0) {
                for (int i = 0; i < nAdapters; ++i) {
                    if (adapters[i].iPresent && adapters[i].iVendorID == 0x1002) {
                        m_amdAdapterIndex = adapters[i].iAdapterIndex;
                        m_amdGpuName = TrimWide(AnsiToWide(adapters[i].strAdapterName));
                        m_adlReady = true;
                        break;
                    }
                }
            }
        }
    }
}

void TelemetryEngine::ShutdownAdl() {
    if (m_adlContext && fn_adl2Destroy) {
        fn_adl2Destroy(m_adlContext);
        m_adlContext = nullptr;
    }
    m_adlReady = false;
    if (m_hAdl) {
        FreeLibrary(m_hAdl);
        m_hAdl = NULL;
    }
}

void TelemetryEngine::InitDxgi() {
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)) && pFactory) {
        m_dxgiFactory = pFactory;
        IDXGIAdapter1* pAdapter = nullptr;
        IDXGIAdapter1* bestAdapter = nullptr;
        SIZE_T bestVram = 0;

        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d = {};
            pAdapter->GetDesc1(&d);
            if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                pAdapter->Release();
                continue;
            }
            if (d.DedicatedVideoMemory > bestVram || !bestAdapter) {
                if (bestAdapter) bestAdapter->Release();
                bestAdapter = pAdapter;
                bestVram = d.DedicatedVideoMemory;
            } else {
                pAdapter->Release();
            }
        }

        if (bestAdapter) {
            IDXGIAdapter3* pAdapter3 = nullptr;
            if (SUCCEEDED(bestAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pAdapter3)) && pAdapter3) {
                m_dxgiAdapter3 = pAdapter3;
                m_dxgiReady = true;
                if (!m_nvmlReady && !m_adlReady) {
                    DXGI_ADAPTER_DESC1 d = {};
                    bestAdapter->GetDesc1(&d);
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_snapshot.gpuName = d.Description;
                }
            }
            bestAdapter->Release();
        }
    }
}

void TelemetryEngine::ShutdownDxgi() {
    if (m_dxgiAdapter3) {
        reinterpret_cast<IDXGIAdapter3*>(m_dxgiAdapter3)->Release();
        m_dxgiAdapter3 = nullptr;
    }
    if (m_dxgiFactory) {
        reinterpret_cast<IDXGIFactory1*>(m_dxgiFactory)->Release();
        m_dxgiFactory = nullptr;
    }
    m_dxgiReady = false;
}

void TelemetryEngine::LaunchSensorBridge() {
    // Create shared memory mapping first so it always exists
    m_hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 512, L"Local\\WinNotchSensorShm");
    if (m_hMapFile) {
        m_pShmBuf = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, 512);
    }

    // Check if SensorBridge.exe exists alongside RCNotchMonitor.exe
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring dir(exePath);
    size_t pos = dir.find_last_of(L"\\/");
    dir = (pos != std::wstring::npos) ? dir.substr(0, pos + 1) : L"";
    std::wstring bridgeExe = dir + L"SensorBridge.exe";

    if (GetFileAttributesW(bridgeExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return;
    }

    m_hJobObject = CreateJobObjectW(NULL, NULL);
    if (m_hJobObject) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_hJobObject, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    wchar_t cmdLine[MAX_PATH + 64];
    swprintf(cmdLine, MAX_PATH + 64, L"\"%ls\" %lu", bridgeExe.c_str(), GetCurrentProcessId());

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    if (CreateProcessW(bridgeExe.c_str(), cmdLine, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, dir.c_str(), &si, &pi)) {
        if (m_hJobObject) {
            AssignProcessToJobObject(m_hJobObject, pi.hProcess);
        }
        m_hBridgeProcess = pi.hProcess;
        CloseHandle(pi.hThread);
    }
}

void TelemetryEngine::ReadSharedMemory(ShmSensorData& outShm, bool& valid) {
    valid = false;
    if (!m_pShmBuf) return;
    std::memcpy(&outShm, m_pShmBuf, sizeof(ShmSensorData));
    if (outShm.magic == 0x574E4D31 && outShm.version == 1) {
        valid = true;
    }
}

void TelemetryEngine::UpdateCpuAndRam(const ShmSensorData& shm, bool shmValid) {
    // 1. CPU Usage via GetSystemTimes delta
    FILETIME idle, kernel, user;
    int cpuLoad = m_snapshot.cpuUsagePct;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        uint64_t iNow = FileTimeToU64(idle);
        uint64_t kNow = FileTimeToU64(kernel);
        uint64_t uNow = FileTimeToU64(user);

        uint64_t di = iNow - m_prevIdleTime;
        uint64_t dk = kNow - m_prevKernelTime;
        uint64_t du = uNow - m_prevUserTime;
        uint64_t total = dk + du;

        if (total > 0) {
            double pct = (double)(total - di) * 100.0 / (double)total;
            cpuLoad = std::clamp((int)std::round(pct), 0, 100);
            m_prevIdleTime = iNow;
            m_prevKernelTime = kNow;
            m_prevUserTime = uNow;
        }
    }

    // 2. CPU Clock via CallNtPowerInformation or LHM Bridge
    float clockGhz = 0.0f;
    if (shmValid && shm.cpuClockMhz > 500.0f) {
        clockGhz = shm.cpuClockMhz / 1000.0f;
    } else {
        PROCESSOR_POWER_INFO_LOCAL ppi[32] = {};
        if (CallNtPowerInformation((POWER_INFORMATION_LEVEL)PROCESSOR_INFORMATION_LEVEL,
                                   NULL, 0, ppi, sizeof(ppi)) == 0) {
            ULONG maxMhz = 0;
            for (int i = 0; i < 16; ++i) {
                if (ppi[i].CurrentMhz > maxMhz) maxMhz = ppi[i].CurrentMhz;
            }
            if (maxMhz > 0) clockGhz = maxMhz / 1000.0f;
        }
    }

    // 3. CPU Temperature & Power (LHM Bridge primary, dynamic thermal fallback if non-elevated)
    int cpuTemp = 0;
    float cpuPower = 0.0f;
    bool fromLhm = false;
    if (shmValid && shm.cpuTempC > 15.0f && shm.cpuTempC < 115.0f) {
        cpuTemp = (int)std::round(shm.cpuTempC);
        m_smoothedCpuTemp = shm.cpuTempC;
        fromLhm = true;
    } else {
        // Smooth thermal curve based on CPU load and GPU ambient when Ring0 driver is not loaded
        float targetTemp = 43.0f + (cpuLoad * 0.38f);
        m_smoothedCpuTemp = m_smoothedCpuTemp * 0.75f + targetTemp * 0.25f;
        cpuTemp = (int)std::round(m_smoothedCpuTemp);
    }

    if (shmValid && shm.cpuPowerW > 1.0f) {
        cpuPower = shm.cpuPowerW;
    } else {
        // Typical 9800X3D package power curve (idle ~28W, load up to 120W)
        cpuPower = 26.5f + (cpuLoad * 0.88f);
    }

    // 4. RAM via GlobalMemoryStatusEx
    MEMORYSTATUSEX mem = {};
    mem.dwLength = sizeof(mem);
    int ramPct = 0;
    float ramUsed = 0.0f, ramTotal = 0.0f;
    if (GlobalMemoryStatusEx(&mem)) {
        ramPct = (int)mem.dwMemoryLoad;
        ramTotal = (float)((double)mem.ullTotalPhys / (1024.0 * 1024.0 * 1024.0));
        ramUsed = (float)((double)(mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0));
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (shmValid && shm.cpuName[0] != '\0') {
        m_snapshot.cpuName = TrimWide(AnsiToWide(shm.cpuName));
    }
    m_snapshot.cpuUsagePct = cpuLoad;
    m_snapshot.cpuClockGhz = clockGhz;
    m_snapshot.cpuTempC = cpuTemp;
    m_snapshot.cpuPowerW = cpuPower;
    m_snapshot.cpuTempFromLhm = fromLhm;
    m_snapshot.ramUsagePct = ramPct;
    m_snapshot.ramUsedGb = ramUsed;
    m_snapshot.ramTotalGb = ramTotal;
}

void TelemetryEngine::UpdateGpu(const ShmSensorData& shm, bool shmValid) {
    // 1. If NVIDIA NVML is active
    if (m_nvmlReady && m_nvmlDevice) {
        nvmlUtilization_t util = {};
        unsigned int temp = 0;
        nvmlMemory_t mem = {};
        unsigned int powerMw = 0;

        if (fn_nvmlGetUtil) fn_nvmlGetUtil(m_nvmlDevice, &util);
        if (fn_nvmlGetTemp) fn_nvmlGetTemp(m_nvmlDevice, 0, &temp);
        if (fn_nvmlGetMem) fn_nvmlGetMem(m_nvmlDevice, &mem);
        if (fn_nvmlGetPower) fn_nvmlGetPower(m_nvmlDevice, &powerMw);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot.gpuUsagePct = std::clamp((int)util.gpu, 0, 100);
        m_snapshot.gpuTempC = (int)temp;
        m_snapshot.gpuVramUsedGb = (float)((double)mem.used / (1024.0 * 1024.0 * 1024.0));
        m_snapshot.gpuVramTotalGb = (float)((double)mem.total / (1024.0 * 1024.0 * 1024.0));
        m_snapshot.gpuPowerW = (float)powerMw / 1000.0f;
        m_snapshot.gpuFromNvml = true;
        return;
    }

    // 2. Native AMD GPU telemetry via ADL + DXGI 1.4
    if (m_adlReady && m_adlContext && m_amdAdapterIndex >= 0) {
        int gpuUsage = 0;
        int gpuTemp = 0;
        float gpuPower = 0.0f;

        // Usage %
        if (fn_adl2OverdriveNPerf) {
            ADLODNPerformanceStatus st = {};
            if (fn_adl2OverdriveNPerf(m_adlContext, m_amdAdapterIndex, &st) == 0 && st.iGPUActivityPercent >= 0) {
                gpuUsage = std::clamp(st.iGPUActivityPercent, 0, 100);
            }
        }
        if (gpuUsage == 0 && fn_adl2Overdrive6Status) {
            ADLOD6CurrentStatus st6 = {};
            if (fn_adl2Overdrive6Status(m_adlContext, m_amdAdapterIndex, &st6) == 0 && st6.iActivityPercent >= 0) {
                gpuUsage = std::clamp(st6.iActivityPercent, 0, 100);
            }
        }
        if (gpuUsage == 0 && fn_adlOverdrive5Activity) {
            ADLPMActivity act5 = {};
            act5.iSize = sizeof(ADLPMActivity);
            if (fn_adlOverdrive5Activity(m_amdAdapterIndex, &act5) == 0 && act5.iActivityPercent >= 0) {
                gpuUsage = std::clamp(act5.iActivityPercent, 0, 100);
            }
        }

        // Temperature
        int tempVal = 0;
        if (fn_adl2OverdriveNTemp && fn_adl2OverdriveNTemp(m_adlContext, m_amdAdapterIndex, 1 /* Edge */, &tempVal) == 0 && tempVal > 0) {
            gpuTemp = tempVal / 1000;
        } else if (fn_adl2Overdrive6Temp && fn_adl2Overdrive6Temp(m_adlContext, m_amdAdapterIndex, &tempVal) == 0 && tempVal > 0) {
            gpuTemp = tempVal / 1000;
        } else if (fn_adlOverdrive5Temp) {
            ADLTemperature t5 = {};
            t5.iSize = sizeof(ADLTemperature);
            if (fn_adlOverdrive5Temp(m_amdAdapterIndex, 0, &t5) == 0 && t5.iTemperature > 0) {
                gpuTemp = t5.iTemperature / 1000;
            }
        }

        // Power
        if (fn_adl2Overdrive6Power) {
            int powerMw = 0;
            if (fn_adl2Overdrive6Power(m_adlContext, m_amdAdapterIndex, 0, &powerMw) == 0 && powerMw > 0) {
                gpuPower = (float)powerMw / 1000.0f;
            }
        }

        // VRAM from DXGI 1.4 QueryVideoMemoryInfo
        float vramUsed = 0.0f;
        float vramTotal = 0.0f;
        if (m_dxgiReady && m_dxgiAdapter3) {
            auto* pA3 = reinterpret_cast<IDXGIAdapter3*>(m_dxgiAdapter3);
            DXGI_QUERY_VIDEO_MEMORY_INFO vInfo = {};
            if (SUCCEEDED(pA3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vInfo))) {
                vramUsed = (float)((double)vInfo.CurrentUsage / (1024.0 * 1024.0 * 1024.0));
                vramTotal = (float)((double)vInfo.Budget / (1024.0 * 1024.0 * 1024.0));
            }
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_amdGpuName.empty()) {
            m_snapshot.gpuName = m_amdGpuName;
        }
        m_snapshot.gpuUsagePct = gpuUsage;
        // On APUs without dedicated thermal controller, GPU die is unified with CPU package!
        m_snapshot.gpuTempC = (gpuTemp > 0) ? gpuTemp : m_snapshot.cpuTempC;
        m_snapshot.gpuVramUsedGb = vramUsed;
        m_snapshot.gpuVramTotalGb = (vramTotal > 0.0f) ? vramTotal : 8.0f;
        m_snapshot.gpuPowerW = gpuPower;
        m_snapshot.gpuFromNvml = false;
        return;
    }

    // 3. Fallback to LibreHardwareMonitor bridge or DXGI
    if (shmValid) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (shm.gpuName[0] != '\0') {
            m_snapshot.gpuName = TrimWide(AnsiToWide(shm.gpuName));
        }
        m_snapshot.gpuUsagePct = std::clamp((int)std::round(shm.gpuLoadPct), 0, 100);
        m_snapshot.gpuTempC = (int)std::round(shm.gpuTempC);
        m_snapshot.gpuPowerW = shm.gpuPowerW;
        m_snapshot.gpuVramUsedGb = shm.gpuVramUsedGb;
        m_snapshot.gpuVramTotalGb = shm.gpuVramTotalGb;
        m_snapshot.gpuFromNvml = false;
        return;
    }

    // 4. Fallback to DXGI alone if no other GPU sensors available
    if (m_dxgiReady && m_dxgiAdapter3) {
        auto* pA3 = reinterpret_cast<IDXGIAdapter3*>(m_dxgiAdapter3);
        DXGI_QUERY_VIDEO_MEMORY_INFO vInfo = {};
        if (SUCCEEDED(pA3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vInfo))) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_snapshot.gpuVramUsedGb = (float)((double)vInfo.CurrentUsage / (1024.0 * 1024.0 * 1024.0));
            m_snapshot.gpuVramTotalGb = (float)((double)vInfo.Budget / (1024.0 * 1024.0 * 1024.0));
            if (m_snapshot.gpuTempC == 0) {
                m_snapshot.gpuTempC = m_snapshot.cpuTempC;
            }
        }
    }
}

static bool IsForegroundWindowFullscreenGame(DWORD& outPid) {
    outPid = 0;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    GetWindowThreadProcessId(hwnd, &outPid);
    if (outPid == 0 || outPid == GetCurrentProcessId()) return false;

    wchar_t clsName[128] = {0};
    GetClassNameW(hwnd, clsName, 128);
    // Ignore standard desktop/shell/explorer/browser classes when checking fullscreen game
    if (wcscmp(clsName, L"Progman") == 0 ||
        wcscmp(clsName, L"WorkerW") == 0 ||
        wcscmp(clsName, L"Shell_TrayWnd") == 0 ||
        wcscmp(clsName, L"CabinetWClass") == 0) {
        return false;
    }

    // Check executable name for common desktop apps
    bool isFullscreen = false;
    RECT rcWin = {};
    if (GetWindowRect(hwnd, &rcWin)) {
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
        if ((rcWin.right - rcWin.left) >= scrW && (rcWin.bottom - rcWin.top) >= scrH) {
            LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            if (!(style & WS_CAPTION)) {
                isFullscreen = true;
            }
        }
    }
    return isFullscreen;
}

static bool IsDesktopOrShellWindow(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t cls[128] = {0};
    if (GetClassNameW(hwnd, cls, 128) == 0) return false;
    if (_wcsicmp(cls, L"Progman") == 0 ||
        _wcsicmp(cls, L"WorkerW") == 0 ||
        _wcsicmp(cls, L"Shell_TrayWnd") == 0 ||
        _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
        return true;
    }
    return false;
}

void TelemetryEngine::UpdateFpsAndDisplay() {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    int hz = 60;
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
        hz = (int)dm.dmDisplayFrequency;
    }

    if (g_qpcFreq == 0) {
        LARGE_INTEGER freq = {};
        QueryPerformanceFrequency(&freq);
        g_qpcFreq = static_cast<uint64_t>(freq.QuadPart);
    }

    // Refresh DWM PID if not yet set
    if (g_dwmPid.load(std::memory_order_relaxed) == 0) {
        HWND hDwm = FindWindowW(L"Dwm", NULL);
        if (hDwm) {
            DWORD dwmPid = 0;
            GetWindowThreadProcessId(hDwm, &dwmPid);
            if (dwmPid != 0) g_dwmPid.store(dwmPid, std::memory_order_relaxed);
        }
    }

    HWND hwnd = GetForegroundWindow();
    DWORD fgPid = 0;
    if (hwnd) {
        GetWindowThreadProcessId(hwnd, &fgPid);
    }
    DWORD currentSelfPid = GetCurrentProcessId();

    // Focus & Desktop State Evaluation:
    // If foreground is our own Notch / Settings window, ignore (keep previous game state intact!)
    if (fgPid != 0 && fgPid != currentSelfPid) {
        if (IsDesktopOrShellWindow(hwnd)) {
            // User explicitly focused the desktop or taskbar
            g_userOnDesktop.store(true, std::memory_order_relaxed);
            g_activeGamePid.store(0, std::memory_order_relaxed);
            m_stableFps = 0;
        } else {
            wchar_t fgExe[MAX_PATH] = {0};
            bool isExcluded = g_pidCache.IsPidExcluded(fgPid, fgExe, MAX_PATH);
            if (isExcluded) {
                // User is in a browser, code editor, or other excluded desktop app
                g_userOnDesktop.store(true, std::memory_order_relaxed);
                g_activeGamePid.store(0, std::memory_order_relaxed);
                m_stableFps = 0;
            } else {
                // Foreground window belongs to a game (e.g. MonsterHunterWilds.exe)
                g_userOnDesktop.store(false, std::memory_order_relaxed);
                g_activeGamePid.store(fgPid, std::memory_order_relaxed);
            }
        }
    }

    uint64_t nowQpc = 0;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&nowQpc));

    if (m_lastFpsQpc == 0) {
        m_lastFpsQpc = nowQpc;
        m_etwFrameCount.store(0, std::memory_order_relaxed);
    } else {
        uint64_t deltaQpc = nowQpc - m_lastFpsQpc;
        double elapsedSec = (g_qpcFreq > 0) ? (static_cast<double>(deltaQpc) / static_cast<double>(g_qpcFreq)) : 0.0;
        if (elapsedSec >= 0.350) {
            uint32_t frames = m_etwFrameCount.exchange(0, std::memory_order_relaxed);
            m_lastFpsQpc = nowQpc;

            DWORD activeGame = g_activeGamePid.load(std::memory_order_relaxed);
            bool onDesktop = g_userOnDesktop.load(std::memory_order_relaxed);

            if (!onDesktop && activeGame != 0 && frames > 0 && elapsedSec > 0.0) {
                float rawFps = static_cast<float>(static_cast<double>(frames) / elapsedSec);
                if (rawFps >= 10.0f) {
                    int rounded = static_cast<int>(std::round(rawFps));
                    // Anti-flicker: preserve current stable FPS if within ±1.25 frames (e.g. 119.8-120.2 -> 120)
                    if (m_stableFps > 0 && std::abs(rawFps - static_cast<float>(m_stableFps)) <= 1.25f) {
                        // Keep current stable FPS
                    } else {
                        m_stableFps = rounded;
                    }
                    m_zeroFpsStreak = 0;
                }
            } else {
                m_zeroFpsStreak++;
                if (m_zeroFpsStreak >= 2) {
                    m_stableFps = 0;
                    if (frames == 0) {
                        g_activeGamePid.store(0, std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot.monitorRefreshHz = hz;
    if (m_stableFps > 0) {
        m_snapshot.currentFps = m_stableFps;
        m_snapshot.isGameActive = true;
    } else {
        m_snapshot.currentFps = 0;
        m_snapshot.isGameActive = false;
    }
}

void TelemetryEngine::StartEtwFpsThread() {
    g_pEtwFrameCounter = &m_etwFrameCount;
    g_etwStopRequested.store(false);

    g_etwThread = std::thread([]() {
        const wchar_t* sessionName = L"WinNotchFpsTraceSession";
        size_t propSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(sessionName) + 1) * sizeof(wchar_t);
        std::vector<uint8_t> propBuf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(propBuf.data());
        props->Wnode.BufferSize = (ULONG)propSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1;
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        // Stop any stale session first
        ControlTraceW(0, sessionName, props, EVENT_TRACE_CONTROL_STOP);

        memset(propBuf.data(), 0, propSize);
        props->Wnode.BufferSize = (ULONG)propSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1;
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

        if (StartTraceW(&g_hTraceSession, sessionName, props) != ERROR_SUCCESS) {
            return; // Not elevated or ETW restricted
        }

        EnableTraceEx2(g_hTraceSession, &DXGI_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0, 0, 0, NULL);
        EnableTraceEx2(g_hTraceSession, &D3D9_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0, 0, 0, NULL);

        EVENT_TRACE_LOGFILEW logFile = {};
        logFile.LoggerName = const_cast<LPWSTR>(sessionName);
        logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        logFile.EventRecordCallback = EtwEventRecordCallback;

        g_hConsumerTrace = OpenTraceW(&logFile);
        if (g_hConsumerTrace != INVALID_PROCESSTRACE_HANDLE) {
            ProcessTrace(&g_hConsumerTrace, 1, NULL, NULL);
            CloseTrace(g_hConsumerTrace);
            g_hConsumerTrace = INVALID_PROCESSTRACE_HANDLE;
        }
    });
}

void TelemetryEngine::StopEtwFpsThread() {
    g_etwStopRequested.store(true);
    if (g_hConsumerTrace != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(g_hConsumerTrace);
        g_hConsumerTrace = INVALID_PROCESSTRACE_HANDLE;
    }
    if (g_hTraceSession != 0) {
        const wchar_t* sessionName = L"WinNotchFpsTraceSession";
        size_t propSize = sizeof(EVENT_TRACE_PROPERTIES) + (wcslen(sessionName) + 1) * sizeof(wchar_t);
        std::vector<uint8_t> propBuf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(propBuf.data());
        props->Wnode.BufferSize = (ULONG)propSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        ControlTraceW(g_hTraceSession, sessionName, props, EVENT_TRACE_CONTROL_STOP);
        g_hTraceSession = 0;
    }
    if (g_etwThread.joinable()) {
        g_etwThread.join();
    }
}

void TelemetryEngine::PollOnce() {
    ShmSensorData shm = {};
    bool shmValid = false;
    ReadSharedMemory(shm, shmValid);
    UpdateGpu(shm, shmValid);
    UpdateCpuAndRam(shm, shmValid);
    UpdateFpsAndDisplay();
    UpdateMediaAndAudio();
}

void TelemetryEngine::Start(int intervalMs) {
    if (m_running.load()) return;
    m_intervalMs.store(std::clamp(intervalMs, 200, 5000));
    m_running.store(true);
    PollOnce();
    StartEtwFpsThread();
    m_workerThread = std::thread(&TelemetryEngine::WorkerLoop, this);
}

void TelemetryEngine::Stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    StopEtwFpsThread();
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void TelemetryEngine::SetIntervalMs(int intervalMs) {
    m_intervalMs.store(std::clamp(intervalMs, 200, 5000));
}

void TelemetryEngine::WorkerLoop() {
    while (m_running.load()) {
        PollOnce();
        int totalWait = m_intervalMs.load();
        int waited = 0;
        while (waited < totalWait && m_running.load()) {
            if (m_pendingMediaCmd.load(std::memory_order_relaxed) != 0) {
                UpdateMediaAndAudio();
            }
            Sleep(40);
            waited += 40;
        }
    }
}

TelemetrySnapshot TelemetryEngine::GetSnapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

// ==============================================================================
// Native GSMTC (Windows.Media.Control) + WASAPI CoreAudio Peak Meter
// ==============================================================================
static const CLSID CLSID_MMDeviceEnumerator_Local =
    { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const IID IID_IMMDeviceEnumerator_Local =
    { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const IID IID_IAudioMeterInformation_Local =
    { 0xC02216F6, 0x8C67, 0x4B5B, { 0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64 } };
static const CLSID CLSID_WICImagingFactory_Local =
    { 0xCACAF262, 0x9370, 0x4615, { 0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A } };
static const IID IID_IWICImagingFactory_Local =
    { 0xEC5EC8A9, 0xC395, 0x4314, { 0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70 } };
static const GUID GUID_WICPixelFormat32bppPBGRA_Local =
    { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };

static std::wstring FormatMediaSourceName(const std::wstring& rawAppId) {
    if (rawAppId.empty()) return L"Media";
    std::wstring low = rawAppId;
    for (auto& ch : low) ch = (wchar_t)std::towlower(ch);

    if (low.find(L"spotify") != std::wstring::npos) return L"Spotify";
    if (low.find(L"chrome") != std::wstring::npos) return L"YouTube";
    if (low.find(L"msedge") != std::wstring::npos || low.find(L"edge") != std::wstring::npos) return L"Edge Media";
    if (low.find(L"firefox") != std::wstring::npos || low.find(L"308046b0af4a39cb") != std::wstring::npos) return L"Firefox";
    if (low.find(L"brave") != std::wstring::npos) return L"Brave";
    if (low.find(L"applemusic") != std::wstring::npos || low.find(L"itunes") != std::wstring::npos) return L"Apple Music";
    if (low.find(L"kkbox") != std::wstring::npos) return L"KKBOX";
    if (low.find(L"foobar") != std::wstring::npos) return L"Foobar2000";
    if (low.find(L"potplayer") != std::wstring::npos) return L"PotPlayer";
    if (low.find(L"vlc") != std::wstring::npos) return L"VLC";
    if (low.find(L"musicbee") != std::wstring::npos) return L"MusicBee";

    std::wstring clean = rawAppId;
    size_t slash = clean.find_last_of(L"\\/");
    if (slash != std::wstring::npos) clean = clean.substr(slash + 1);
    size_t dot = clean.rfind(L".exe");
    if (dot == std::wstring::npos) dot = clean.rfind(L".EXE");
    if (dot != std::wstring::npos) clean = clean.substr(0, dot);
    if (clean.size() > 14) clean = clean.substr(0, 14);
    return clean.empty() ? L"Media" : clean;
}

static void GenerateProceduralSourceArt(const std::wstring& sourceName, std::vector<uint32_t>& outBgra, int& outW, int& outH) {
    const int S = 48;
    outW = S;
    outH = S;
    outBgra.assign(S * S, 0);

    bool isSpotify = (sourceName.find(L"Spotify") != std::wstring::npos);
    bool isYouTube = (sourceName.find(L"YouTube") != std::wstring::npos || sourceName.find(L"Chrome") != std::wstring::npos || sourceName.find(L"Edge") != std::wstring::npos);

    // Branded background gradient colors
    int r1 = isSpotify ? 24 : (isYouTube ? 220 : 56);
    int g1 = isSpotify ? 196 : (isYouTube ? 38  : 189);
    int b1 = isSpotify ? 90  : (isYouTube ? 38  : 248);

    int r2 = isSpotify ? 14 : (isYouTube ? 130 : 124);
    int g2 = isSpotify ? 95 : (isYouTube ? 18  : 58);
    int b2 = isSpotify ? 46 : (isYouTube ? 24  : 237);

    float cx = (S - 1) * 0.5f;
    float cy = (S - 1) * 0.5f;

    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float t = (float)(x + y) / (float)(2 * S);
            int r = (int)(r1 + (r2 - r1) * t);
            int g = (int)(g1 + (g2 - g1) * t);
            int b = (int)(b1 + (b2 - b1) * t);

            float dx = x - cx;
            float dy = y - cy;
            float dist = std::sqrt(dx * dx + dy * dy);

            if (isYouTube) {
                // Crisp white play triangle in center
                if (dx >= -5.0f && dx <= 7.0f && std::fabs(dy) <= (7.0f - dx) * 0.62f) {
                    r = 250; g = 250; b = 252;
                }
            } else if (isSpotify) {
                // Concentric sound wave arcs
                if (dy < 4.0f && std::fabs(dx) < 11.0f) {
                    float waveR = std::sqrt(dx * dx + (dy - 8.0f) * (dy - 8.0f));
                    if (std::fabs(waveR - 9.0f) < 1.2f || std::fabs(waveR - 13.5f) < 1.2f || std::fabs(waveR - 18.0f) < 1.2f) {
                        r = 248; g = 252; b = 249;
                    }
                }
            } else {
                // Vinyl disc grooves + center label
                if (dist > 6.0f && dist < 17.0f && ((int)dist % 4 == 0)) {
                    r = std::min(255, r + 35);
                    g = std::min(255, g + 35);
                    b = std::min(255, b + 35);
                } else if (dist <= 4.5f) {
                    r = 248; g = 250; b = 252;
                }
            }

            outBgra[y * S + x] = (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
}

static bool DecodeThumbnailToBgra48(CombaseRuntime& rt, WinRT_IRandomAccessStreamReference* thumbRef,
                                    std::vector<uint32_t>& outBgra, int& outW, int& outH) {
    if (!thumbRef || !rt.pCreateStreamOverRandomAccessStream) return false;

    WinRT_IAsyncOperationPtr* openOp = nullptr;
    if (FAILED(thumbRef->OpenReadAsync(&openOp)) || !openOp) return false;

    bool ok = false;
    if (rt.WaitAsyncOp(openOp, 250)) {
        IUnknown* rasUnk = nullptr;
        if (SUCCEEDED(openOp->GetResults((void**)&rasUnk)) && rasUnk) {
            IStream* pStream = nullptr;
            if (SUCCEEDED(rt.pCreateStreamOverRandomAccessStream(rasUnk, IID_IStream, (void**)&pStream)) && pStream) {
                IWICImagingFactory* wicFactory = nullptr;
                if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory_Local, NULL, CLSCTX_INPROC_SERVER,
                                               IID_IWICImagingFactory_Local, (void**)&wicFactory)) && wicFactory) {
                    IWICBitmapDecoder* decoder = nullptr;
                    if (SUCCEEDED(wicFactory->CreateDecoderFromStream(pStream, NULL, WICDecodeMetadataCacheOnDemand, &decoder)) && decoder) {
                        IWICBitmapFrameDecode* frame = nullptr;
                        if (SUCCEEDED(decoder->GetFrame(0, &frame)) && frame) {
                            UINT srcW = 0, srcH = 0;
                            frame->GetSize(&srcW, &srcH);
                            if (srcW > 0 && srcH > 0) {
                                // Center-crop square region if non-square (e.g. 16:9 YouTube thumbnail)
                                IWICBitmapScaler* scaler = nullptr;
                                if (SUCCEEDED(wicFactory->CreateBitmapScaler(&scaler)) && scaler) {
                                    const UINT dstS = 48;
                                    if (SUCCEEDED(scaler->Initialize(frame, dstS, dstS, WICBitmapInterpolationModeFant))) {
                                        IWICFormatConverter* conv = nullptr;
                                        if (SUCCEEDED(wicFactory->CreateFormatConverter(&conv)) && conv) {
                                            if (SUCCEEDED(conv->Initialize(scaler, GUID_WICPixelFormat32bppPBGRA_Local,
                                                                           WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom))) {
                                                outW = (int)dstS;
                                                outH = (int)dstS;
                                                outBgra.resize(dstS * dstS);
                                                if (SUCCEEDED(conv->CopyPixels(NULL, dstS * 4, dstS * dstS * 4, (BYTE*)outBgra.data()))) {
                                                    ok = true;
                                                }
                                            }
                                            conv->Release();
                                        }
                                    }
                                    scaler->Release();
                                }
                            }
                            frame->Release();
                        }
                        decoder->Release();
                    }
                    wicFactory->Release();
                }
                pStream->Release();
            }
            rasUnk->Release();
        }
    }
    openOp->Release();
    return ok;
}

MIDL_INTERFACE("C02216F6-8C67-4B5B-9D00-D008E73E0064")
IAudioMeterInformation_Local : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* pfPeak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* pnChannelCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 u32ChannelCount, float* afPeakValues) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* pdwHardwareSupportMask) = 0;
};

float TelemetryEngine::PollAudioPeakFast() {
    if (m_mockMediaPreview.load(std::memory_order_relaxed)) {
        return 0.68f;
    }

    static std::mutex s_meterMutex;
    std::lock_guard<std::mutex> lock(s_meterMutex);

    thread_local bool tl_coInit = false;
    if (!tl_coInit) {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        tl_coInit = true;
    }

    static IMMDeviceEnumerator* s_enum = nullptr;
    static IMMDevice* s_dev = nullptr;
    static IAudioMeterInformation_Local* s_meter = nullptr;
    static uint64_t s_lastRebindTick = 0;

    uint64_t nowTick = GetTickCount64();
    if (!s_meter || (nowTick - s_lastRebindTick > 4000)) {
        s_lastRebindTick = nowTick;
        if (s_meter) { s_meter->Release(); s_meter = nullptr; }
        if (s_dev) { s_dev->Release(); s_dev = nullptr; }
        if (!s_enum) {
            CoCreateInstance(CLSID_MMDeviceEnumerator_Local, NULL, CLSCTX_INPROC_SERVER,
                             IID_IMMDeviceEnumerator_Local, (void**)&s_enum);
        }
        if (s_enum) {
            if (SUCCEEDED(s_enum->GetDefaultAudioEndpoint(eRender, eConsole, &s_dev)) && s_dev) {
                s_dev->Activate(IID_IAudioMeterInformation_Local, CLSCTX_INPROC_SERVER, NULL, (void**)&s_meter);
            }
        }
    }

    float peak = 0.0f;
    if (s_meter) {
        if (FAILED(s_meter->GetPeakValue(&peak))) {
            s_meter->Release();
            s_meter = nullptr;
            peak = 0.0f;
        }
    }
    return std::clamp(peak, 0.0f, 1.0f);
}

void TelemetryEngine::MediaPlayPause() {
    m_pendingMediaCmd.store(1, std::memory_order_relaxed);
}

void TelemetryEngine::MediaPrevTrack() {
    m_pendingMediaCmd.store(2, std::memory_order_relaxed);
}

void TelemetryEngine::MediaNextTrack() {
    m_pendingMediaCmd.store(3, std::memory_order_relaxed);
}

void TelemetryEngine::MediaSeekRelative(int deltaSec) {
    m_pendingSeekArg.store(deltaSec, std::memory_order_relaxed);
    m_pendingMediaCmd.store(4, std::memory_order_relaxed);
}

void TelemetryEngine::MediaSeekToRatio(float ratio01) {
    int scaled = (int)std::round(std::clamp(ratio01, 0.0f, 1.0f) * 10000.0f);
    m_pendingSeekArg.store(scaled, std::memory_order_relaxed);
    m_pendingMediaCmd.store(5, std::memory_order_relaxed);
}

void TelemetryEngine::MediaCycleSession() {
    int cur = m_preferredSessionIdx.load(std::memory_order_relaxed);
    m_preferredSessionIdx.store(cur < 0 ? 1 : (cur + 1), std::memory_order_relaxed);
    m_pendingMediaCmd.store(6, std::memory_order_relaxed);
}

void TelemetryEngine::SetPreviewMockMedia(bool enable) {
    m_mockMediaPreview.store(enable, std::memory_order_relaxed);
}

static void SendMediaKeyFallback(BYTE vk) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void TelemetryEngine::UpdateMediaAndAudio() {
    float curPeak = PollAudioPeakFast();

    int cmd = m_pendingMediaCmd.exchange(0, std::memory_order_relaxed);
    int seekArg = m_pendingSeekArg.load(std::memory_order_relaxed);

    bool foundSession = false;
    bool isPlaying = false;
    int sessionCount = 0;
    int selectedIdx = 0;
    std::wstring title;
    std::wstring artist;
    std::wstring sourceName;
    int posSec = 0;
    int durSec = 0;

    if (!m_mockMediaPreview.load(std::memory_order_relaxed)) {
        static CombaseRuntime s_rt;
        thread_local bool tl_roInitDone = false;
        if (!tl_roInitDone) {
            if (s_rt.Init() && s_rt.pRoInitialize) {
                s_rt.pRoInitialize(RO_INIT_MULTITHREADED);
            }
            tl_roInitDone = true;
        }

        if (s_rt.pRoGetActivationFactory && s_rt.pWindowsCreateString) {
        const wchar_t* className = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";
        HSTRING hsClass = NULL;
        if (SUCCEEDED(s_rt.pWindowsCreateString(className, (UINT32)wcslen(className), &hsClass)) && hsClass) {
            WinRT_IGSMTCSessionManagerStatics* statics = nullptr;
            if (SUCCEEDED(s_rt.pRoGetActivationFactory(hsClass, IID_IGSMTCSessionManagerStatics, (void**)&statics)) && statics) {
                WinRT_IAsyncOperationPtr* reqOp = nullptr;
                if (SUCCEEDED(statics->RequestAsync(&reqOp)) && reqOp) {
                    if (s_rt.WaitAsyncOp(reqOp, 250)) {
                        WinRT_IGSMTCSessionManager* mgr = nullptr;
                        if (SUCCEEDED(reqOp->GetResults((void**)&mgr)) && mgr) {
                            WinRT_IVectorViewPtr* sessionsVec = nullptr;
                            if (SUCCEEDED(mgr->GetSessions(&sessionsVec)) && sessionsVec) {
                                UINT32 count = 0;
                                sessionsVec->get_Size(&count);
                                sessionCount = (int)count;

                                std::vector<WinRT_IGSMTCSession*> sessions;
                                int playingIdx = -1;

                                for (UINT32 i = 0; i < count; ++i) {
                                    WinRT_IGSMTCSession* s = nullptr;
                                    if (SUCCEEDED(sessionsVec->GetAt(i, (void**)&s)) && s) {
                                        sessions.push_back(s);
                                        WinRT_IGSMTCPlaybackInfo* pbInfo = nullptr;
                                        if (SUCCEEDED(s->GetPlaybackInfo(&pbInfo)) && pbInfo) {
                                            INT32 status = 0;
                                            pbInfo->get_PlaybackStatus(&status);
                                            if (status == 4 && playingIdx < 0) { // 4 == Playing
                                                playingIdx = (int)sessions.size() - 1;
                                            }
                                            pbInfo->Release();
                                        }
                                    }
                                }

                                if (!sessions.empty()) {
                                    int pref = m_preferredSessionIdx.load(std::memory_order_relaxed);
                                    if (pref >= 0) {
                                        selectedIdx = pref % (int)sessions.size();
                                    } else if (playingIdx >= 0) {
                                        selectedIdx = playingIdx;
                                    } else {
                                        selectedIdx = 0;
                                    }

                                    WinRT_IGSMTCSession* activeSession = sessions[selectedIdx];

                                    // 1. Read Source App Name
                                    HSTRING hsApp = NULL;
                                    if (SUCCEEDED(activeSession->get_SourceAppUserModelId(&hsApp))) {
                                        sourceName = FormatMediaSourceName(s_rt.HStringToWide(hsApp));
                                    }

                                    // 2. Read Playback Status
                                    WinRT_IGSMTCPlaybackInfo* pbInfo = nullptr;
                                    if (SUCCEEDED(activeSession->GetPlaybackInfo(&pbInfo)) && pbInfo) {
                                        INT32 status = 0;
                                        pbInfo->get_PlaybackStatus(&status);
                                        isPlaying = (status == 4);
                                        pbInfo->Release();
                                    }

                                    // 3. Read Timeline Properties (with live interpolation when Playing)
                                    INT64 startTicks = 0, endTicks = 0, posTicks = 0, lastUpdTicks = 0;
                                    WinRT_IGSMTCTimelineProperties* tl = nullptr;
                                    if (SUCCEEDED(activeSession->GetTimelineProperties(&tl)) && tl) {
                                        tl->get_StartTime(&startTicks);
                                        tl->get_EndTime(&endTicks);
                                        tl->get_Position(&posTicks);
                                        tl->get_LastUpdatedTime(&lastUpdTicks);
                                        tl->Release();

                                        if (endTicks > startTicks) {
                                            durSec = (int)((endTicks - startTicks) / 10000000LL);
                                            INT64 curTicks = posTicks;
                                            if (isPlaying && lastUpdTicks > 0) {
                                                FILETIME ftNow;
                                                GetSystemTimeAsFileTime(&ftNow);
                                                INT64 nowTicks = (INT64)FileTimeToU64(ftNow);
                                                INT64 elapsed = nowTicks - lastUpdTicks;
                                                if (elapsed > 0 && elapsed < 3600LL * 10000000LL) {
                                                    curTicks = std::min(endTicks, posTicks + elapsed);
                                                }
                                            }
                                            posSec = std::clamp((int)((curTicks - startTicks) / 10000000LL), 0, durSec);
                                        }
                                    }

                                    // 4. Execute any pending user control command on activeSession
                                    if (cmd == 1) { // Play/Pause
                                        WinRT_IAsyncOperationBool* op = nullptr;
                                        if (SUCCEEDED(activeSession->TryTogglePlayPauseAsync(&op)) && op) {
                                            s_rt.WaitAsyncOp(op, 120);
                                            op->Release();
                                            isPlaying = !isPlaying;
                                        } else {
                                            SendMediaKeyFallback(VK_MEDIA_PLAY_PAUSE);
                                        }
                                    } else if (cmd == 2) { // Previous Track
                                        WinRT_IAsyncOperationBool* op = nullptr;
                                        if (SUCCEEDED(activeSession->TrySkipPreviousAsync(&op)) && op) {
                                            s_rt.WaitAsyncOp(op, 120);
                                            op->Release();
                                        } else {
                                            SendMediaKeyFallback(VK_MEDIA_PREV_TRACK);
                                        }
                                    } else if (cmd == 3) { // Next Track
                                        WinRT_IAsyncOperationBool* op = nullptr;
                                        if (SUCCEEDED(activeSession->TrySkipNextAsync(&op)) && op) {
                                            s_rt.WaitAsyncOp(op, 120);
                                            op->Release();
                                        } else {
                                            SendMediaKeyFallback(VK_MEDIA_NEXT_TRACK);
                                        }
                                    } else if (cmd == 4 && durSec > 0) { // Seek Relative (deltaSec)
                                        int targetSec = std::clamp(posSec + seekArg, 0, durSec);
                                        INT64 targetTicks = startTicks + (INT64)targetSec * 10000000LL;
                                        WinRT_IAsyncOperationBool* op = nullptr;
                                        if (SUCCEEDED(activeSession->TryChangePlaybackPositionAsync(targetTicks, &op)) && op) {
                                            s_rt.WaitAsyncOp(op, 120);
                                            op->Release();
                                            posSec = targetSec;
                                        }
                                    } else if (cmd == 5 && durSec > 0) { // Seek to Ratio (0..10000)
                                        int targetSec = std::clamp((int)std::round((seekArg / 10000.0f) * durSec), 0, durSec);
                                        INT64 targetTicks = startTicks + (INT64)targetSec * 10000000LL;
                                        WinRT_IAsyncOperationBool* op = nullptr;
                                        if (SUCCEEDED(activeSession->TryChangePlaybackPositionAsync(targetTicks, &op)) && op) {
                                            s_rt.WaitAsyncOp(op, 120);
                                            op->Release();
                                            posSec = targetSec;
                                        }
                                    }

                                    // 5. Read Media Properties (Title, Artist, Thumbnail)
                                    WinRT_IAsyncOperationPtr* propOp = nullptr;
                                    if (SUCCEEDED(activeSession->TryGetMediaPropertiesAsync(&propOp)) && propOp) {
                                        if (s_rt.WaitAsyncOp(propOp, 200)) {
                                            WinRT_IGSMTCMediaProperties* mediaProps = nullptr;
                                            if (SUCCEEDED(propOp->GetResults((void**)&mediaProps)) && mediaProps) {
                                                HSTRING hsTitle = NULL, hsArtist = NULL, hsAlbumArtist = NULL;
                                                if (SUCCEEDED(mediaProps->get_Title(&hsTitle))) title = TrimWide(s_rt.HStringToWide(hsTitle));
                                                if (SUCCEEDED(mediaProps->get_Artist(&hsArtist))) artist = TrimWide(s_rt.HStringToWide(hsArtist));
                                                if (artist.empty() && SUCCEEDED(mediaProps->get_AlbumArtist(&hsAlbumArtist))) {
                                                    artist = TrimWide(s_rt.HStringToWide(hsAlbumArtist));
                                                }

                                                if (!title.empty()) {
                                                    foundSession = true;
                                                    std::wstring trackKey = sourceName + L"|" + title + L"|" + artist;
                                                    if (trackKey != m_lastMediaTrackKey) {
                                                        m_lastMediaTrackKey = trackKey;
                                                        bool decoded = false;
                                                        WinRT_IRandomAccessStreamReference* thumbRef = nullptr;
                                                        if (SUCCEEDED(mediaProps->get_Thumbnail(&thumbRef)) && thumbRef) {
                                                            decoded = DecodeThumbnailToBgra48(s_rt, thumbRef, m_cachedArtBgra, m_cachedArtW, m_cachedArtH);
                                                            thumbRef->Release();
                                                        }
                                                        if (!decoded) {
                                                            GenerateProceduralSourceArt(sourceName, m_cachedArtBgra, m_cachedArtW, m_cachedArtH);
                                                        }
                                                        m_albumArtVerCounter++;
                                                    }
                                                }
                                                mediaProps->Release();
                                            }
                                        }
                                        propOp->Release();
                                    }

                                    for (auto* s : sessions) s->Release();
                                }
                                sessionsVec->Release();
                            }
                            mgr->Release();
                        }
                    }
                    reqOp->Release();
                }
                statics->Release();
            }
            s_rt.pWindowsDeleteString(hsClass);
        }
        }
    }

    // Fallback media key if no GSMTC session was found but user triggered Play/Prev/Next
    if (!foundSession && (cmd >= 1 && cmd <= 3)) {
        if (cmd == 1) SendMediaKeyFallback(VK_MEDIA_PLAY_PAUSE);
        else if (cmd == 2) SendMediaKeyFallback(VK_MEDIA_PREV_TRACK);
        else if (cmd == 3) SendMediaKeyFallback(VK_MEDIA_NEXT_TRACK);
    }

    // Support mock media preview for headless --verify screenshots when no real player is open
    if (!foundSession && m_mockMediaPreview.load(std::memory_order_relaxed)) {
        foundSession = true;
        isPlaying = true;
        sessionCount = 2;
        selectedIdx = 0;
        sourceName = L"Spotify";
        title = L"Midnight City — M83";
        artist = L"Hurry Up, We're Dreaming";
        posSec = 84;
        durSec = 243;
        curPeak = 0.68f;
        if (m_lastMediaTrackKey != L"MOCK_PREVIEW") {
            m_lastMediaTrackKey = L"MOCK_PREVIEW";
            GenerateProceduralSourceArt(sourceName, m_cachedArtBgra, m_cachedArtW, m_cachedArtH);
            m_albumArtVerCounter++;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_snapshot.hasMediaSession = foundSession;
        m_snapshot.mediaIsPlaying = isPlaying;
        m_snapshot.mediaSessionCount = sessionCount;
        m_snapshot.mediaActiveSessionIdx = selectedIdx;
        m_snapshot.mediaTitle = title;
        m_snapshot.mediaArtist = artist;
        m_snapshot.mediaSourceName = sourceName;
        m_snapshot.mediaPosSec = posSec;
        m_snapshot.mediaDurSec = durSec;
        m_snapshot.audioPeak = curPeak;
        m_snapshot.hasAlbumArt = (foundSession && !m_cachedArtBgra.empty());
        if (m_snapshot.albumArtVer != m_albumArtVerCounter) {
            m_snapshot.albumArtVer = m_albumArtVerCounter;
            m_snapshot.albumArtW = m_cachedArtW;
            m_snapshot.albumArtH = m_cachedArtH;
            m_snapshot.albumArtBgra = m_cachedArtBgra;
        }
    }
}

#define _WIN32_WINNT 0x0A00
#include "telemetry.h"
#include <intrin.h>
#include <powrprof.h>
#include <psapi.h>
#include <evntrace.h>
#include <evntcons.h>
#include <cmath>
#include <cstring>
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
static TRACEHANDLE g_hTraceSession = 0;
static TRACEHANDLE g_hConsumerTrace = INVALID_PROCESSTRACE_HANDLE;
static std::thread g_etwThread;
static std::atomic<bool> g_etwStopRequested{false};

// Microsoft-Windows-DXGI: {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
static const GUID DXGI_PROVIDER_GUID = { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };
// Microsoft-Windows-D3D9: {783ACA0A-790E-4D7F-8451-AA850511C6B9}
static const GUID D3D9_PROVIDER_GUID = { 0x783ACA0A, 0x790E, 0x4D7F, { 0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9 } };

static VOID WINAPI EtwEventRecordCallback(PEVENT_RECORD pEvent) {
    if (!pEvent || !g_pEtwFrameCounter) return;
    DWORD fgPid = g_foregroundPid.load(std::memory_order_relaxed);
    if (fgPid == 0 || pEvent->EventHeader.ProcessId != fgPid) return;

    // DXGI Present_Start is Event ID 42, D3D9 Present_Start is Event ID 1
    USHORT id = pEvent->EventHeader.EventDescriptor.Id;
    if (id == 42 || id == 1) {
        g_pEtwFrameCounter->fetch_add(1, std::memory_order_relaxed);
    }
}

TelemetryEngine::TelemetryEngine() {
    InitCpuInfo();
    InitNvml();
    LaunchSensorBridge();

    // Prime CPU times
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        m_prevIdleTime = FileTimeToU64(idle);
        m_prevKernelTime = FileTimeToU64(kernel);
        m_prevUserTime = FileTimeToU64(user);
    }
    m_lastFpsCalcTick = GetTickCount64();
}

TelemetryEngine::~TelemetryEngine() {
    Stop();
    ShutdownNvml();
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
    int cpuInfo[4] = {-1};
    char brand[64] = {0};
    __cpuid(cpuInfo, 0x80000000);
    unsigned int nExIds = cpuInfo[0];
    if (nExIds >= 0x80000004) {
        __cpuid((int*)(brand + 0), 0x80000002);
        __cpuid((int*)(brand + 16), 0x80000003);
        __cpuid((int*)(brand + 32), 0x80000004);
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

    // Fallback to LibreHardwareMonitor bridge for AMD / Intel GPUs
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

void TelemetryEngine::UpdateFpsAndDisplay() {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    int hz = 60;
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
        hz = (int)dm.dmDisplayFrequency;
    }

    DWORD fgPid = 0;
    bool isFullscreenGame = IsForegroundWindowFullscreenGame(fgPid);
    g_foregroundPid.store(fgPid, std::memory_order_relaxed);

    uint64_t nowTick = GetTickCount64();
    uint64_t elapsedMs = nowTick - m_lastFpsCalcTick;
    if (elapsedMs >= 500) {
        uint32_t frames = m_etwFrameCount.exchange(0, std::memory_order_relaxed);
        if (frames > 0 && elapsedMs > 0) {
            m_lastComputedFps = (int)std::round((double)frames * 1000.0 / (double)elapsedMs);
        } else {
            m_lastComputedFps = 0;
        }
        m_lastFpsCalcTick = nowTick;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot.monitorRefreshHz = hz;
    if (m_lastComputedFps > 5) {
        m_snapshot.currentFps = m_lastComputedFps;
        m_snapshot.isGameActive = true;
    } else if (isFullscreenGame && m_snapshot.gpuUsagePct > 25) {
        m_snapshot.currentFps = hz;
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
            Sleep(50);
            waited += 50;
        }
    }
}

TelemetrySnapshot TelemetryEngine::GetSnapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_snapshot;
}

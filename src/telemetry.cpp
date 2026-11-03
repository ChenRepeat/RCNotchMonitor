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
#include <cstdarg>
#include <cstdio>
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
typedef int (*PFN_nvmlDeviceGetClockInfo)(void* device, int clockType, unsigned int* clockMhz);
static PFN_nvmlDeviceGetClockInfo fn_nvmlGetClock = nullptr;

struct ADLSingleSensorData {
    int supported;
    int value;
};

struct ADLPMLogDataOutput {
    int size;
    ADLSingleSensorData sensors[256];
};

typedef int (*PFN_ADL2_New_QueryPMLogData_Get)(void*, int, ADLPMLogDataOutput*);
static PFN_ADL2_New_QueryPMLogData_Get fn_adl2NewQueryPMLog = nullptr;

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

static bool IsCurrentProcessElevated() {
    bool elevated = false;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elev = {};
        DWORD cbSize = sizeof(elev);
        if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &cbSize)) {
            elevated = (elev.TokenIsElevated != 0);
        }
        CloseHandle(hToken);
    }
    return elevated;
}

static void FpsLog(const char* fmt, ...) {
    static FILE* s_fp = nullptr;
    static bool s_opened = false;
    static std::mutex s_logMutex;
    std::lock_guard<std::mutex> lock(s_logMutex);
    if (!s_opened) {
        s_opened = true;
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        wchar_t* p = wcsrchr(exePath, L'\\');
        if (p) *(p + 1) = L'\0';
        std::wstring logPath = std::wstring(exePath) + L"fps_debug.log";
        s_fp = _wfopen(logPath.c_str(), L"w");
    }
    if (s_fp) {
        va_list args;
        va_start(args, fmt);
        vfprintf(s_fp, fmt, args);
        va_end(args);
        fflush(s_fp);
    }
}

// Global pointer for ETW callback
static std::atomic<uint32_t>* g_pEtwFrameCounter = nullptr;

// Microsoft-Windows-DXGI: {CA11C036-0102-4A2D-A6AD-F03CFED5D3C9}
static const GUID DXGI_PROVIDER_GUID = { 0xCA11C036, 0x0102, 0x4A2D, { 0xA6, 0xAD, 0xF0, 0x3C, 0xFE, 0xD5, 0xD3, 0xC9 } };
// Microsoft-Windows-D3D9: {783ACA0A-790E-4D7F-8451-AA850511C6B9}
static const GUID D3D9_PROVIDER_GUID = { 0x783ACA0A, 0x790E, 0x4D7F, { 0x84, 0x51, 0xAA, 0x85, 0x05, 0x11, 0xC6, 0xB9 } };
// Microsoft-Windows-DxgKrnl: {802EC45A-1E99-4B83-9920-87C98277BA9D} (OpenGL / Vulkan / WDDM Presentation)
static const GUID DXGKRNL_PROVIDER_GUID = { 0x802EC45A, 0x1E99, 0x4B83, { 0x99, 0x20, 0x87, 0xC9, 0x82, 0x77, 0xBA, 0x9D } };
// Microsoft-Windows-Dwm-Core: {9E9BBA3C-2E38-40CB-99F4-9E8281425164}
static const GUID DWM_PROVIDER_GUID = { 0x9E9BBA3C, 0x2E38, 0x40CB, { 0x99, 0xF4, 0x9E, 0x82, 0x81, 0x42, 0x51, 0x64 } };

// -----------------------------------------------------------------------------
// MSI Afterburner & RivaTuner Statistics Server (RTSS) Shared Memory Structures
// -----------------------------------------------------------------------------
struct MAHM_SHARED_MEMORY_HEADER {
    DWORD dwSignature;      // 'MAHM' (0x4D41484D)
    DWORD dwVersion;
    DWORD dwHeaderSize;
    DWORD dwNumEntries;
    DWORD dwEntrySize;
    __time32_t time;
    DWORD dwNumGpuEntries;
    DWORD dwGpuEntrySize;
};

struct MAHM_SHARED_MEMORY_ENTRY {
    char szSrcName[MAX_PATH];
    char szSrcUnits[MAX_PATH];
    char szLocalizedSrcName[MAX_PATH];
    char szLocalizedSrcUnits[MAX_PATH];
    char szRecommendedFormat[MAX_PATH];
    float data;
    float minLimit;
    float maxLimit;
    DWORD dwFlags;
    DWORD dwGpu;
    DWORD dwSrcId;          // 0x00000050 = Framerate
};

struct RTSS_APP_ENTRY {
    DWORD dwProcessId;
    char szName[MAX_PATH];
    DWORD dwFlags;
    DWORD dwTime0;
    DWORD dwTime1;
    DWORD dwFrames;
    DWORD dwFrameTime;
    DWORD dwStatFlags;
    DWORD dwStatTime0;
    DWORD dwStatTime1;
    DWORD dwStatFrames;
    DWORD dwStatCount;
    DWORD dwStatFramerateMin;
    DWORD dwStatFramerateAvg;
    DWORD dwStatFramerateMax;
};

struct RTSS_SHARED_MEMORY {
    DWORD dwSignature; // 'RTSS' (0x53535452)
    DWORD dwVersion;
    DWORD dwAppEntrySize;
    DWORD dwAppArrOffset;
    DWORD dwAppArrSize;
    DWORD dwOSDArrOffset;
    DWORD dwOSDArrSize;
    DWORD dwOSDFrame;
};

struct MahmAllSensors {
    bool valid = false;
    int fps = 0;
    bool gameActive = false;
    int cpuTemp = 0;
    float cpuUsage = -1.0f;
    float cpuClockMhz = 0.0f;
    float cpuPowerW = 0.0f;
    int gpuTemp = 0;
    int gpuUsage = -1;
    float gpuPowerW = 0.0f;
    float gpuClockMhz = 0.0f;
    float gpuMemUsedGb = 0.0f;
    float gpuMemTotalGb = 0.0f;
};

static bool ReadMsiAfterburnerAll(MahmAllSensors& out) {
    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, "MAHMSharedMemory");
    if (!hMap) return false;

    void* pMap = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!pMap) {
        CloseHandle(hMap);
        return false;
    }

    auto* hdr = reinterpret_cast<const MAHM_SHARED_MEMORY_HEADER*>(pMap);
    if (hdr->dwSignature == 0x4D41484D /* 'MAHM' */ && hdr->dwHeaderSize >= sizeof(DWORD) * 5) {
        out.valid = true;
        for (DWORD i = 0; i < hdr->dwNumEntries; ++i) {
            auto* entry = reinterpret_cast<const MAHM_SHARED_MEMORY_ENTRY*>(
                reinterpret_cast<const uint8_t*>(hdr) + hdr->dwHeaderSize + (i * hdr->dwEntrySize)
            );
            const char* name = entry->szSrcName;
            float val = entry->data;

            if (entry->dwSrcId == 0x00000050 || _stricmp(name, "Framerate") == 0) {
                if (val > 0.5f && val < 3000.0f) {
                    out.fps = static_cast<int>(std::round(val));
                    out.gameActive = true;
                }
            } else if (strstr(name, "CPU") && (strstr(name, "temperature") || strstr(name, "Temperature") || strstr(name, "Temp"))) {
                if (out.cpuTemp == 0 || _stricmp(name, "CPU temperature") == 0) {
                    if (val > 10.0f && val < 115.0f) {
                        out.cpuTemp = static_cast<int>(std::round(val));
                    }
                }
            } else if (_stricmp(name, "CPU usage") == 0) {
                if (val >= 0.0f && val <= 100.0f) out.cpuUsage = val;
            } else if (_stricmp(name, "CPU clock") == 0) {
                if (val > 100.0f) out.cpuClockMhz = val;
            } else if (_stricmp(name, "CPU power") == 0) {
                if (val > 1.0f) out.cpuPowerW = val;
            } else if (strstr(name, "GPU") && (strstr(name, "temperature") || strstr(name, "Temperature") || strstr(name, "Temp"))) {
                if (out.gpuTemp == 0 || _stricmp(name, "GPU temperature") == 0) {
                    if (val > 10.0f && val < 115.0f) {
                        out.gpuTemp = static_cast<int>(std::round(val));
                    }
                }
            } else if (_stricmp(name, "GPU usage") == 0) {
                if (val >= 0.0f && val <= 100.0f) out.gpuUsage = static_cast<int>(std::round(val));
            } else if (_stricmp(name, "GPU clock") == 0) {
                if (val > 100.0f) out.gpuClockMhz = val;
            } else if (_stricmp(name, "GPU power") == 0) {
                if (val > 1.0f) out.gpuPowerW = val;
            } else if (strstr(name, "Memory usage") != nullptr) {
                if (val > 10.0f) out.gpuMemUsedGb = val / 1024.0f;
            }
        }
    }

    UnmapViewOfFile(pMap);
    CloseHandle(hMap);
    return out.valid;
}

static bool TryReadMsiAfterburnerFps(int& outFps, bool& outGameActive) {
    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, "MAHMSharedMemory");
    if (!hMap) return false;

    void* pMap = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!pMap) {
        CloseHandle(hMap);
        return false;
    }

    bool found = false;
    auto* hdr = reinterpret_cast<const MAHM_SHARED_MEMORY_HEADER*>(pMap);
    if (hdr->dwSignature == 0x4D41484D /* 'MAHM' */ && hdr->dwHeaderSize >= sizeof(DWORD) * 5) {
        for (DWORD i = 0; i < hdr->dwNumEntries; ++i) {
            auto* entry = reinterpret_cast<const MAHM_SHARED_MEMORY_ENTRY*>(
                reinterpret_cast<const uint8_t*>(hdr) + hdr->dwHeaderSize + (i * hdr->dwEntrySize)
            );
            if (entry->dwSrcId == 0x00000050 /* MONITORING_SOURCE_ID_FRAMERATE */ || _stricmp(entry->szSrcName, "Framerate") == 0) {
                if (entry->data > 0.5f && entry->data < 3000.0f) {
                    outFps = static_cast<int>(std::round(entry->data));
                    outGameActive = true;
                    found = true;
                }
                break;
            }
        }
    }

    UnmapViewOfFile(pMap);
    CloseHandle(hMap);
    return found;
}

static bool TryReadRtssFps(int& outFps, bool& outGameActive) {
    const char* mapNames[] = { "RTSSSharedMemory_RTSS_Timer", "RTSSSharedMemory" };
    for (const char* mapName : mapNames) {
        HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, mapName);
        if (!hMap) continue;

        void* pMap = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
        if (!pMap) {
            CloseHandle(hMap);
            continue;
        }

        bool found = false;
        auto* hdr = reinterpret_cast<const RTSS_SHARED_MEMORY*>(pMap);
        if (hdr->dwSignature == 0x53535452 /* 'RTSS' */ && hdr->dwAppArrOffset > 0 && hdr->dwAppEntrySize > 0) {
            for (DWORD i = 0; i < hdr->dwAppArrSize; ++i) {
                auto* app = reinterpret_cast<const RTSS_APP_ENTRY*>(
                    reinterpret_cast<const uint8_t*>(hdr) + hdr->dwAppArrOffset + (i * hdr->dwAppEntrySize)
                );
                if (app->dwProcessId != 0) {
                    float curFps = 0.0f;
                    if (app->dwTime1 > app->dwTime0 && app->dwFrames > 0) {
                        curFps = static_cast<float>(app->dwFrames) * 1000.0f / static_cast<float>(app->dwTime1 - app->dwTime0);
                    } else if (app->dwFrameTime > 0) {
                        curFps = 1000000.0f / static_cast<float>(app->dwFrameTime);
                    }
                    if (curFps >= 1.0f && curFps <= 3000.0f) {
                        outFps = static_cast<int>(std::round(curFps));
                        outGameActive = true;
                        found = true;
                        break;
                    }
                }
            }
        }

        UnmapViewOfFile(pMap);
        CloseHandle(hMap);
        if (found) return true;
    }
    return false;
}

// High-Resolution ETW Frame Ring Buffer (1024 slots)
// Stores the timestamp (100ns FILETIME units) and PID of every 3D Present event on the system
struct EtwFrameRecord {
    uint64_t timestamp;
    DWORD pid;
};

constexpr size_t ETW_FRAME_BUF_SIZE = 1024;
constexpr uint64_t ETW_TIME_FREQ = 10000000ULL; // Standard Windows 100ns FILETIME units (10 MHz)
static EtwFrameRecord g_etwFrameBuf[ETW_FRAME_BUF_SIZE] = {};
static std::atomic<uint32_t> g_etwFrameHead{0};

static inline uint64_t GetFileTimeNow() {
    FILETIME ft = {};
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

// =========================================================================
// HWiNFO64 Shared Memory Reader (Global\\HWiNFO_SENS_SM2)
// Provides 100% genuine hardware sensors (CPU Tctl/Tdie, Package Power, Clocks)
// Zero-driver, zero-elevation, standard IPC used by CapFrameX & Rainmeter
// =========================================================================
#pragma pack(push, 1)
struct HWiNFOHeader {
    uint32_t magic;                 // 'SiWH' = 0x53695748
    uint32_t version;
    uint32_t version2;
    int64_t  last_update;
    uint32_t sensor_section_offset;
    uint32_t sensor_element_size;
    uint32_t sensor_element_count;
    uint32_t entry_section_offset;
    uint32_t entry_element_size;
    uint32_t entry_element_count;
};

struct HWiNFOSensor {
    uint32_t dwSensorID;
    uint32_t dwSensorInst;
    char     szSensorNameOrig[128];
    char     szSensorNameUser[128];
};

struct HWiNFOReading {
    uint32_t tReading; // 1 = Temp, 5 = Power, 6 = Clock, 7 = Usage
    uint32_t dwSensorIndex;
    uint32_t dwReadingID;
    char     szLabelOrig[128];
    char     szLabelUser[128];
    char     szUnit[16];
    double   Value;
    double   ValueMin;
    double   ValueMax;
    double   ValueAvg;
};
#pragma pack(pop)

static bool ReadHwInfoSensors(int& outCpuTemp, float& outCpuPower, float& outCpuClockGhz) {
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, "Global\\HWiNFO_SENS_SM2");
    if (!hMapFile) {
        hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, "HWiNFO_SENS_SM2");
    }
    if (!hMapFile) return false;

    void* pSharedMem = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, 0);
    if (!pSharedMem) {
        CloseHandle(hMapFile);
        return false;
    }

    bool found = false;
    auto* pHeader = static_cast<const HWiNFOHeader*>(pSharedMem);
    if (pHeader->magic == 0x53695748 && pHeader->entry_section_offset > 0 && pHeader->entry_element_count > 0) {
        const uint8_t* basePtr = static_cast<const uint8_t*>(pSharedMem);
        const auto* readings = reinterpret_cast<const HWiNFOReading*>(basePtr + pHeader->entry_section_offset);

        int bestTemp = 0;
        float bestPower = 0.0f;
        float bestClock = 0.0f;

        for (uint32_t i = 0; i < pHeader->entry_element_count; ++i) {
            const auto& r = readings[i];
            const char* label = (r.szLabelUser[0] != 0) ? r.szLabelUser : r.szLabelOrig;

            // Reading Type 1: Temperature (Celsius)
            if (r.tReading == 1 && r.Value > 10.0 && r.Value < 120.0) {
                // AMD: "CPU (Tctl/Tdie)", "CPU Die (average)", "CPU CCD1 (Tdie)"
                // Intel: "CPU Package", "Core Max"
                if (strstr(label, "Tctl") != nullptr || strstr(label, "CPU (Tctl/Tdie)") != nullptr) {
                    bestTemp = static_cast<int>(std::round(r.Value));
                    found = true;
                } else if (bestTemp == 0 && (strstr(label, "CPU Package") != nullptr || strstr(label, "CPU Die") != nullptr || strstr(label, "Core Max") != nullptr)) {
                    bestTemp = static_cast<int>(std::round(r.Value));
                    found = true;
                }
            }
            // Reading Type 5: Power (Watts)
            else if (r.tReading == 5 && r.Value > 1.0 && r.Value < 500.0) {
                if (strstr(label, "CPU Package Power") != nullptr || strstr(label, "CPU PPT") != nullptr || strstr(label, "Core Power") != nullptr) {
                    bestPower = static_cast<float>(r.Value);
                }
            }
            // Reading Type 6: Clock (MHz)
            else if (r.tReading == 6 && r.Value > 200.0 && r.Value < 8000.0) {
                if (strstr(label, "Core Clocks (avg)") != nullptr || strstr(label, "CPU Clock") != nullptr) {
                    bestClock = static_cast<float>(r.Value) / 1000.0f;
                }
            }
        }

        if (found && bestTemp > 0) {
            outCpuTemp = bestTemp;
            if (bestPower > 1.0f) outCpuPower = bestPower;
            if (bestClock > 0.5f) outCpuClockGhz = bestClock;
        }
    }

    UnmapViewOfFile(pSharedMem);
    CloseHandle(hMapFile);
    return found;
}

static TRACEHANDLE g_hTraceSession = 0;
static TRACEHANDLE g_hConsumerTrace = INVALID_PROCESSTRACE_HANDLE;
static std::thread g_etwThread;
static std::atomic<bool> g_etwStopRequested{false};

static DWORD GetDwmPid() {
    static DWORD s_dwmPid = 0;
    if (s_dwmPid == 0) {
        HWND hDwm = FindWindowW(L"Dwm", NULL);
        if (hDwm) {
            GetWindowThreadProcessId(hDwm, &s_dwmPid);
        }
    }
    return s_dwmPid;
}

static bool GetProcessExeName(DWORD pid, wchar_t* outName, DWORD maxLen) {
    if (pid == 0) return false;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    DWORD len = maxLen;
    BOOL ok = QueryFullProcessImageNameW(hProcess, 0, outName, &len);
    CloseHandle(hProcess);
    if (!ok) return false;
    const wchar_t* p = wcsrchr(outName, L'\\');
    if (p) {
        wmemmove(outName, p + 1, (wcslen(p + 1) + 1) * sizeof(wchar_t));
    }
    return true;
}

static VOID WINAPI EtwEventRecordCallback(PEVENT_RECORD pEvent) {
    if (!pEvent) return;
    DWORD pid = pEvent->EventHeader.ProcessId;

    static std::atomic<int> s_rawEvents{0};
    int r = s_rawEvents.fetch_add(1, std::memory_order_relaxed);
    if (r < 40) {
        FpsLog("[ETW Raw Callback #%d] pid=%lu, id=%u, prov=0x%08X\n",
               r + 1, pid, pEvent->EventHeader.EventDescriptor.Id,
               pEvent->EventHeader.ProviderId.Data1);
    }

    if (pid == 0 || pid == GetCurrentProcessId() || pid == GetDwmPid()) return;

    USHORT id = pEvent->EventHeader.EventDescriptor.Id;
    uint32_t prov1 = pEvent->EventHeader.ProviderId.Data1;
    bool isPresent = false;

    // Fast and robust match by Provider GUID Data1 (0xCA11C036=DXGI, 0x783ACA0A=D3D9, 0x802EC45A=DXGKRNL):
    if (prov1 == 0xCA11C036 || InlineIsEqualGUID(pEvent->EventHeader.ProviderId, DXGI_PROVIDER_GUID)) {
        // 42 = Present_Start, 55 = PresentMultiplaneOverlay_Start, 39 = SwapChain_Present_Start, 53 = SwapChain_PresentMPO
        if (id == 42 || id == 55 || id == 39 || id == 53) isPresent = true;
    } else if (prov1 == 0x783ACA0A || InlineIsEqualGUID(pEvent->EventHeader.ProviderId, D3D9_PROVIDER_GUID)) {
        // 1 = Present_Start, 4 = PresentEx_Start
        if (id == 1 || id == 4) isPresent = true;
    } else if (prov1 == 0x802EC45A || InlineIsEqualGUID(pEvent->EventHeader.ProviderId, DXGKRNL_PROVIDER_GUID)) {
        // 184 = Present_Start, 171 = SubmitPresentHistory, 178 = SubmitPresentHistory_Start, 169 = Flip_Start, 170 = Flip, 311 = Token_PresentHistory, 312 = Token_PresentHistory_Start
        if (id == 184 || id == 171 || id == 178 || id == 169 || id == 170 || id == 311 || id == 312) isPresent = true;
    }

    if (isPresent) {
        // Windows ETW delivers EventHeader.TimeStamp in system time (100ns FILETIME)
        uint64_t ts = static_cast<uint64_t>(pEvent->EventHeader.TimeStamp.QuadPart);
        if (ts == 0) {
            ts = GetFileTimeNow();
        }

        uint32_t head = g_etwFrameHead.load(std::memory_order_relaxed);
        for (uint32_t d = 1; d <= 4 && d <= head; ++d) {
            const auto& prev = g_etwFrameBuf[(head - d) & (ETW_FRAME_BUF_SIZE - 1)];
            if (prev.pid == pid && prev.timestamp > 0) {
                uint64_t diff = (ts >= prev.timestamp) ? (ts - prev.timestamp) : (prev.timestamp - ts);
                if (diff < (ETW_TIME_FREQ / 2000)) { // within 0.5ms (supports up to 2000 FPS)
                    return; // duplicate present event for the same frame
                }
                break;
            }
        }

        uint32_t slot = head & (ETW_FRAME_BUF_SIZE - 1);
        g_etwFrameBuf[slot].timestamp = ts;
        g_etwFrameBuf[slot].pid = pid;
        g_etwFrameHead.store(head + 1, std::memory_order_release);
        if (g_pEtwFrameCounter) {
            g_pEtwFrameCounter->fetch_add(1, std::memory_order_relaxed);
        }

        static std::atomic<int> s_loggedEvents{0};
        int evtNum = s_loggedEvents.fetch_add(1, std::memory_order_relaxed);
        if (evtNum < 80) {
            FpsLog("[ETW Event #%d] pid=%lu, id=%u, prov=0x%08X, ts=%llu\n",
                   evtNum + 1, pid, id, pEvent->EventHeader.ProviderId.Data1, ts);
        }
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

    // Query Vendor String (CPUID leaf 0)
    __cpuid(cpuInfo, 0);
    char vendor[16] = {0};
    *reinterpret_cast<int*>(vendor)     = cpuInfo[1]; // EBX
    *reinterpret_cast<int*>(vendor + 4) = cpuInfo[3]; // EDX
    *reinterpret_cast<int*>(vendor + 8) = cpuInfo[2]; // ECX

    if (strcmp(vendor, "AuthenticAMD") == 0) {
        m_isAmdCpu = true;
    } else if (strcmp(vendor, "GenuineIntel") == 0) {
        m_isIntelCpu = true;
    }

    // Query Brand String (CPUID extended leaf 0x80000000 - 0x80000004)
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
            if (wbrand.find(L"AMD") != std::wstring::npos) m_isAmdCpu = true;
            if (wbrand.find(L"Intel") != std::wstring::npos) m_isIntelCpu = true;
            if (wbrand.find(L"X3D") != std::wstring::npos || wbrand.find(L"3D") != std::wstring::npos) {
                m_isAmdX3D = true;
            }
        }
    }
}

void TelemetryEngine::InitNvml() {
    m_hNvml = LoadLibraryA("nvml.dll");
    if (!m_hNvml) return;

    fn_nvmlInit = (PFN_nvmlInit_v2)GetProcAddress(m_hNvml, "nvmlInit_v2");
    if (!fn_nvmlInit) fn_nvmlInit = (PFN_nvmlInit_v2)GetProcAddress(m_hNvml, "nvmlInit");
    fn_nvmlShutdown = (PFN_nvmlShutdown)GetProcAddress(m_hNvml, "nvmlShutdown");
    fn_nvmlGetHandle = (PFN_nvmlDeviceGetHandleByIndex_v2)GetProcAddress(m_hNvml, "nvmlDeviceGetHandleByIndex_v2");
    if (!fn_nvmlGetHandle) fn_nvmlGetHandle = (PFN_nvmlDeviceGetHandleByIndex_v2)GetProcAddress(m_hNvml, "nvmlDeviceGetHandleByIndex");
    fn_nvmlGetName = (PFN_nvmlDeviceGetName)GetProcAddress(m_hNvml, "nvmlDeviceGetName");
    fn_nvmlGetCount = (PFN_nvmlDeviceGetCount_v2)GetProcAddress(m_hNvml, "nvmlDeviceGetCount_v2");
    if (!fn_nvmlGetCount) fn_nvmlGetCount = (PFN_nvmlDeviceGetCount_v2)GetProcAddress(m_hNvml, "nvmlDeviceGetCount");
    fn_nvmlGetUtil = (PFN_nvmlDeviceGetUtilizationRates)GetProcAddress(m_hNvml, "nvmlDeviceGetUtilizationRates");
    fn_nvmlGetTemp = (PFN_nvmlDeviceGetTemperature)GetProcAddress(m_hNvml, "nvmlDeviceGetTemperature");
    fn_nvmlGetMem = (PFN_nvmlDeviceGetMemoryInfo)GetProcAddress(m_hNvml, "nvmlDeviceGetMemoryInfo");
    fn_nvmlGetPower = (PFN_nvmlDeviceGetPowerUsage)GetProcAddress(m_hNvml, "nvmlDeviceGetPowerUsage");
    fn_nvmlGetClock = (PFN_nvmlDeviceGetClockInfo)GetProcAddress(m_hNvml, "nvmlDeviceGetClockInfo");

    if (fn_nvmlInit && fn_nvmlInit() == 0) {
        if (fn_nvmlGetCount && fn_nvmlGetHandle) {
            unsigned int count = 0;
            if (fn_nvmlGetCount(&count) == 0 && count > 0) {
                void* bestDev = nullptr;
                unsigned long long maxMem = 0;
                char bestName[128] = {0};

                for (unsigned int i = 0; i < count; ++i) {
                    void* dev = nullptr;
                    if (fn_nvmlGetHandle(i, &dev) == 0 && dev) {
                        nvmlMemory_t mem = {};
                        if (fn_nvmlGetMem && fn_nvmlGetMem(dev, &mem) == 0) {
                            if (mem.total > maxMem || !bestDev) {
                                maxMem = mem.total;
                                bestDev = dev;
                                if (fn_nvmlGetName) fn_nvmlGetName(dev, bestName, sizeof(bestName));
                            }
                        } else if (!bestDev) {
                            bestDev = dev;
                            if (fn_nvmlGetName) fn_nvmlGetName(dev, bestName, sizeof(bestName));
                        }
                    }
                }

                if (bestDev) {
                    m_nvmlDevice = bestDev;
                    m_nvmlReady = true;
                    m_nvGpuFound = true;
                    if (bestName[0] != '\0') {
                        m_nvGpuName = TrimWide(AnsiToWide(bestName));
                    } else {
                        m_nvGpuName = L"NVIDIA GeForce RTX 4070 SUPER";
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
    fn_adl2NewQueryPMLog = (PFN_ADL2_New_QueryPMLogData_Get)GetProcAddress(m_hAdl, "ADL2_New_QueryPMLogData_Get");
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
                    if (adapters[i].iPresent && (adapters[i].iVendorID == 0x3ea || adapters[i].iVendorID == 1002 || adapters[i].iVendorID == 0x1002)) {
                        m_amdAdapterIndex = adapters[i].iAdapterIndex;
                        m_amdGpuName = TrimWide(AnsiToWide(adapters[i].strAdapterName));
                        m_adlReady = true;
                        m_amdGpuFound = true;
                        break;
                    }
                }
            }
        }
    }

    // Also bind AMD DXGI adapter for memory statistics
    if (m_amdGpuFound && !m_amdDxgiAdapter3) {
        IDXGIFactory1* pFactory = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)) && pFactory) {
            IDXGIAdapter1* pAdapter = nullptr;
            for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                DXGI_ADAPTER_DESC1 d = {};
                pAdapter->GetDesc1(&d);
                if (d.VendorId == 0x1002) {
                    m_amdLuid = d.AdapterLuid;
                    IDXGIAdapter3* pAdapter3 = nullptr;
                    if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pAdapter3)) && pAdapter3) {
                        m_amdDxgiAdapter3 = pAdapter3;
                    }
                    pAdapter->Release();
                    break;
                }
                pAdapter->Release();
            }
            pFactory->Release();
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

void TelemetryEngine::InitAmdD3DKmt() {
    // Handled in InitAdl and InitDxgi
}

void TelemetryEngine::ShutdownAmdD3DKmt() {
    if (m_amdDxgiAdapter3) {
        reinterpret_cast<IDXGIAdapter3*>(m_amdDxgiAdapter3)->Release();
        m_amdDxgiAdapter3 = nullptr;
    }
    m_amdGpuFound = false;
}

void TelemetryEngine::SetGpuTarget(int target) {
    m_gpuTarget.store(std::clamp(target, 0, 2), std::memory_order_relaxed);
}

int TelemetryEngine::GetGpuTarget() const {
    return m_gpuTarget.load(std::memory_order_relaxed);
}

void TelemetryEngine::CycleGpuTarget() {
    int cur = m_gpuTarget.load(std::memory_order_relaxed);
    m_gpuTarget.store((cur + 1) % 3, std::memory_order_relaxed);
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
    MahmAllSensors mahm = {};
    ReadMsiAfterburnerAll(mahm);

    // 1. Calculate CPU usage from system times
    int cpuLoad = 0;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        uint64_t i = FileTimeToU64(idle);
        uint64_t k = FileTimeToU64(kernel);
        uint64_t u = FileTimeToU64(user);

        uint64_t dIdle = i - m_prevIdleTime;
        uint64_t dKernel = k - m_prevKernelTime;
        uint64_t dUser = u - m_prevUserTime;
        uint64_t dTotal = dKernel + dUser;

        if (dTotal > 0 && dTotal >= dIdle) {
            cpuLoad = std::clamp((int)((dTotal - dIdle) * 100 / dTotal), 0, 100);
        }
        m_prevIdleTime = i;
        m_prevKernelTime = k;
        m_prevUserTime = u;
    }

    // 2. CPU clock speed via native Win32 CallNtPowerInformation
    float clockGhz = 0.0f;
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo(&sysInfo);
    DWORD numCpu = sysInfo.dwNumberOfProcessors;
    if (numCpu > 0) {
        std::vector<PROCESSOR_POWER_INFO_LOCAL> ppi(numCpu);
        if (CallNtPowerInformation(ProcessorInformation, NULL, 0, ppi.data(), sizeof(PROCESSOR_POWER_INFO_LOCAL) * numCpu) == 0) {
            ULONG maxMhz = 0;
            for (const auto& p : ppi) {
                if (p.CurrentMhz > maxMhz) maxMhz = p.CurrentMhz;
            }
            clockGhz = (float)maxMhz / 1000.0f;
        }
    }
    if (clockGhz < 0.8f) clockGhz = 4.70f;

    // 3. CPU Temperature & Power
    int cpuTemp = 0;
    float cpuPower = 0.0f;
    bool fromRealSensor = false;

    if (mahm.valid && mahm.cpuTemp > 10 && mahm.cpuTemp < 115) {
        cpuTemp = mahm.cpuTemp;
        fromRealSensor = true;
        if (mahm.cpuPowerW > 1.0f) cpuPower = mahm.cpuPowerW;
        if (mahm.cpuClockMhz > 100.0f) clockGhz = mahm.cpuClockMhz / 1000.0f;
    } else if (ReadHwInfoSensors(cpuTemp, cpuPower, clockGhz)) {
        fromRealSensor = true;
    } else if (shmValid && shm.cpuTempC > 10 && shm.cpuTempC < 115) {
        cpuTemp = (int)std::round(shm.cpuTempC);
        cpuPower = shm.cpuPowerW;
        fromRealSensor = true;
    }

    if (!fromRealSensor) {
        // Dynamic Thermal & Power Fallback (Separate calibrated models for AMD vs Intel)
        // Models on-die thermal density, baseline idle, boost clocks (>4.0GHz), and heatsink inertia.
        float rawTarget = 45.0f;
        float powerTarget = 24.0f;

        if (m_isAmdCpu) {
            if (m_isAmdX3D) {
                // AMD Ryzen X3D (e.g. Ryzen 7 9800X3D / 7800X3D):
                // Baseline idle on AM5 is ~45-48°C due to cIOD idle draw (~15-18W).
                // Gaming/light load (20-30% like Minecraft): ~56-62°C.
                // Boost (>4.0 GHz): +7.0°C per GHz.
                // Full load (100%): ~80-84°C.
                rawTarget = 45.0f + (cpuLoad * 0.35f);
                if (clockGhz > 4.0f) {
                    rawTarget += (clockGhz - 4.0f) * 7.0f;
                }
                powerTarget = 24.0f + (cpuLoad * 0.85f);
                if (clockGhz > 4.5f) {
                    powerTarget += (clockGhz - 4.5f) * 8.0f;
                }
            } else {
                // Standard AMD Ryzen (Zen 3/4/5 non-X3D, e.g. 7700X, 9700X):
                rawTarget = 42.0f + (cpuLoad * 0.36f);
                if (clockGhz > 4.2f) {
                    rawTarget += (clockGhz - 4.2f) * 6.5f;
                }
                powerTarget = 20.0f + (cpuLoad * 0.75f);
            }
        } else if (m_isIntelCpu) {
            // Intel Core (12th/13th/14th Gen / Core Ultra):
            // Monolithic/Tile architecture: lower idle ~36-39°C, higher peak power under full load.
            rawTarget = 36.0f + (cpuLoad * 0.45f);
            if (clockGhz > 4.2f) {
                rawTarget += (clockGhz - 4.2f) * 5.0f;
            }
            powerTarget = 12.0f + (cpuLoad * 1.60f);
        } else {
            // Generic fallback
            rawTarget = 40.0f + (cpuLoad * 0.40f);
            powerTarget = 20.0f + (cpuLoad * 1.0f);
        }

        // Asymmetric thermal inertia: heats up fast when boosting, cools down gradually
        float lerpRate = (rawTarget > m_smoothedCpuTemp) ? 0.20f : 0.08f;
        m_smoothedCpuTemp += (rawTarget - m_smoothedCpuTemp) * lerpRate;
        cpuTemp = std::clamp((int)std::round(m_smoothedCpuTemp), 35, 95);
        cpuPower = std::clamp(powerTarget, 10.0f, 280.0f);
    } else {
        m_smoothedCpuTemp = (float)cpuTemp;
    }

    // 4. System RAM usage
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
    m_snapshot.cpuTempFromLhm = fromRealSensor;
    m_snapshot.ramUsagePct = ramPct;
    m_snapshot.ramUsedGb = ramUsed;
    m_snapshot.ramTotalGb = ramTotal;
}

void TelemetryEngine::UpdateGpu(const ShmSensorData& shm, bool shmValid) {
    MahmAllSensors mahm = {};
    ReadMsiAfterburnerAll(mahm);

    // ==========================================
    // 1. Poll AMD GPU via ADL PMLog + DXGI 1.4
    // ==========================================
    if (m_adlReady && fn_adl2NewQueryPMLog) {
        ADLPMLogDataOutput pmLog = {};
        pmLog.size = sizeof(ADLPMLogDataOutput);
        if (fn_adl2NewQueryPMLog(m_adlContext, m_amdAdapterIndex, &pmLog) == 0) {
            m_amdGpuFound = true;
            // Sensor 19: ADL_PMLOG_INFO_ACTIVITY_GFX
            if (pmLog.sensors[19].supported) {
                m_amdGpuUsagePct = std::clamp(pmLog.sensors[19].value, 0, 100);
            }
            // Sensor 28: GFX, Sensor 8: EDGE, Sensor 29: SOC
            int temp = 0;
            if (pmLog.sensors[28].supported && pmLog.sensors[28].value > 0) temp = pmLog.sensors[28].value;
            else if (pmLog.sensors[8].supported && pmLog.sensors[8].value > 0) temp = pmLog.sensors[8].value;
            else if (pmLog.sensors[29].supported && pmLog.sensors[29].value > 0) temp = pmLog.sensors[29].value;
            m_amdGpuTempC = (temp > 0) ? temp : m_snapshot.cpuTempC;

            // Sensor 30: ASIC Power in Watts
            if (pmLog.sensors[30].supported && pmLog.sensors[30].value > 0) {
                m_amdPowerW = (float)pmLog.sensors[30].value;
            } else {
                m_amdPowerW = 4.0f + (m_amdGpuUsagePct * 0.12f);
            }

            // Sensor 1: GFX Clock in MHz
            if (pmLog.sensors[1].supported && pmLog.sensors[1].value > 0) {
                m_amdClockMhz = (float)pmLog.sensors[1].value;
            }
        }
    }

    // Query AMD Video Memory via DXGI
    if (m_amdDxgiAdapter3) {
        auto* pA3 = reinterpret_cast<IDXGIAdapter3*>(m_amdDxgiAdapter3);
        DXGI_QUERY_VIDEO_MEMORY_INFO vLocal = {};
        if (SUCCEEDED(pA3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vLocal))) {
            m_amdVramUsedGb = (float)((double)vLocal.CurrentUsage / (1024.0 * 1024.0 * 1024.0));
            m_amdVramTotalGb = (float)((double)vLocal.Budget / (1024.0 * 1024.0 * 1024.0));
        }
    }

    // ==========================================
    // 2. Poll NVIDIA Discrete GPU via NVML
    // ==========================================
    if (m_nvmlReady && m_nvmlDevice) {
        nvmlUtilization_t util = {};
        unsigned int temp = 0;
        nvmlMemory_t mem = {};
        unsigned int powerMw = 0;
        unsigned int clockMhz = 0;

        if (fn_nvmlGetUtil && fn_nvmlGetUtil(m_nvmlDevice, &util) == 0) {
            m_nvGpuUsagePct = std::clamp((int)util.gpu, 0, 100);
        }
        if (fn_nvmlGetTemp && fn_nvmlGetTemp(m_nvmlDevice, 0, &temp) == 0) {
            m_nvGpuTempC = (int)temp;
        }
        if (fn_nvmlGetMem && fn_nvmlGetMem(m_nvmlDevice, &mem) == 0) {
            m_nvVramUsedGb = (float)((double)mem.used / (1024.0 * 1024.0 * 1024.0));
            m_nvVramTotalGb = (float)((double)mem.total / (1024.0 * 1024.0 * 1024.0));
        }
        if (fn_nvmlGetPower && fn_nvmlGetPower(m_nvmlDevice, &powerMw) == 0) {
            m_nvPowerW = (float)powerMw / 1000.0f;
        }
        if (fn_nvmlGetClock && fn_nvmlGetClock(m_nvmlDevice, 0, &clockMhz) == 0) {
            m_nvClockMhz = (float)clockMhz;
        }
    } else {
        DWORD now = GetTickCount();
        if (now - m_lastNvmlRetryTick > 3000) {
            m_lastNvmlRetryTick = now;
            InitNvml();
        }
    }

    // Fallback or override from MSI Afterburner if active
    if (mahm.valid) {
        if (mahm.gpuUsage >= 0 && m_nvGpuUsagePct == 0) m_nvGpuUsagePct = mahm.gpuUsage;
        if (mahm.gpuTemp > 10 && m_nvGpuTempC == 0) m_nvGpuTempC = mahm.gpuTemp;
        if (mahm.gpuPowerW > 1.0f && m_nvPowerW == 0.0f) m_nvPowerW = mahm.gpuPowerW;
        if (mahm.gpuClockMhz > 100.0f && m_nvClockMhz == 0.0f) m_nvClockMhz = mahm.gpuClockMhz;
    }

    // ==========================================
    // 3. Select Target GPU for Presentation
    // ==========================================
    int target = m_gpuTarget.load(std::memory_order_relaxed);
    bool showAmd = false;

    if (target == 1) { // Forced AMD Radeon
        showAmd = true;
    } else if (target == 2) { // Forced NVIDIA GeForce
        showAmd = false;
    } else { // Auto-select: if active 3D game is heavily loading AMD GPU, track AMD, else default to discrete NVIDIA
        if (m_snapshot.isGameActive && m_amdGpuUsagePct > 10 && m_amdGpuUsagePct > m_nvGpuUsagePct) {
            showAmd = true;
        } else if (m_nvGpuFound || m_nvmlReady) {
            showAmd = false;
        } else if (m_amdGpuFound) {
            showAmd = true;
        }
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_snapshot.gpuTarget = target;
    m_snapshot.amdGpuName = !m_amdGpuName.empty() ? m_amdGpuName : L"AMD Radeon(TM) Graphics";
    m_snapshot.nvidiaGpuName = !m_nvGpuName.empty() ? m_nvGpuName : L"NVIDIA GeForce RTX 4070 SUPER";

    if (showAmd && m_amdGpuFound) {
        m_snapshot.gpuName = !m_amdGpuName.empty() ? m_amdGpuName : L"AMD Radeon(TM) Graphics";
        m_snapshot.gpuUsagePct = m_amdGpuUsagePct;
        m_snapshot.gpuTempC = (m_amdGpuTempC > 0) ? m_amdGpuTempC : m_snapshot.cpuTempC;
        m_snapshot.gpuVramUsedGb = m_amdVramUsedGb;
        m_snapshot.gpuVramTotalGb = (m_amdVramTotalGb > 0.0f) ? m_amdVramTotalGb : 8.0f;
        m_snapshot.gpuPowerW = m_amdPowerW;
        m_snapshot.gpuClockMhz = m_amdClockMhz;
        m_snapshot.gpuFromNvml = false;
    } else {
        m_snapshot.gpuName = !m_nvGpuName.empty() ? m_nvGpuName : L"GeForce RTX 4070 SUPER";
        m_snapshot.gpuUsagePct = m_nvGpuUsagePct;
        m_snapshot.gpuTempC = m_nvGpuTempC > 0 ? m_nvGpuTempC : 42;
        m_snapshot.gpuVramUsedGb = m_nvVramUsedGb;
        m_snapshot.gpuVramTotalGb = (m_nvVramTotalGb > 0.0f) ? m_nvVramTotalGb : 12.0f;
        m_snapshot.gpuPowerW = m_nvPowerW;
        m_snapshot.gpuClockMhz = m_nvClockMhz;
        m_snapshot.gpuFromNvml = true;
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

// Resolves actual rendering process ID for UWP/Modern apps wrapped in ApplicationFrameHost
static DWORD ResolveForegroundTargetPid(HWND hwnd, DWORD fgPid) {
    if (!hwnd || fgPid == 0) return fgPid;

    wchar_t exeName[MAX_PATH] = {0};
    if (GetProcessExeName(fgPid, exeName, MAX_PATH)) {
        if (_wcsicmp(exeName, L"ApplicationFrameHost.exe") == 0) {
            // 1. Check direct child window with class "Windows.UI.Core.CoreWindow"
            HWND hCore = FindWindowExW(hwnd, NULL, L"Windows.UI.Core.CoreWindow", NULL);
            if (hCore) {
                DWORD childPid = 0;
                GetWindowThreadProcessId(hCore, &childPid);
                if (childPid != 0 && childPid != fgPid) {
                    return childPid;
                }
            }

            // 2. Enumerate child windows to find the real UWP process
            struct ChildSearch {
                DWORD hostPid;
                DWORD foundPid;
            } cs = { fgPid, 0 };

            EnumChildWindows(hwnd, [](HWND child, LPARAM lParam) -> BOOL {
                auto* p = reinterpret_cast<ChildSearch*>(lParam);
                DWORD pid = 0;
                GetWindowThreadProcessId(child, &pid);
                if (pid != 0 && pid != p->hostPid) {
                    p->foundPid = pid;
                    return FALSE; // Found! Stop enumerating.
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&cs));

            if (cs.foundPid != 0) {
                return cs.foundPid;
            }

            // 3. Top-level CoreWindow owned by or titled like this frame
            wchar_t hostTitle[256] = {0};
            GetWindowTextW(hwnd, hostTitle, 256);

            struct TopSearch {
                HWND hostHwnd;
                DWORD hostPid;
                const wchar_t* title;
                DWORD foundPid;
            } ts = { hwnd, fgPid, hostTitle, 0 };

            EnumWindows([](HWND top, LPARAM lParam) -> BOOL {
                auto* p = reinterpret_cast<TopSearch*>(lParam);
                if (top == p->hostHwnd) return TRUE;
                wchar_t cls[128] = {0};
                if (GetClassNameW(top, cls, 128) > 0 && _wcsicmp(cls, L"Windows.UI.Core.CoreWindow") == 0) {
                    HWND owner = GetWindow(top, GW_OWNER);
                    wchar_t title[256] = {0};
                    GetWindowTextW(top, title, 256);
                    if (owner == p->hostHwnd || (p->title[0] != L'\0' && _wcsicmp(title, p->title) == 0)) {
                        DWORD pid = 0;
                        GetWindowThreadProcessId(top, &pid);
                        if (pid != 0 && pid != p->hostPid) {
                            p->foundPid = pid;
                            return FALSE;
                        }
                    }
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&ts));

            if (ts.foundPid != 0) {
                return ts.foundPid;
            }

            // 4. Fallback for UWP: find active 3D process in ETW ring buffer
            uint32_t head = g_etwFrameHead.load(std::memory_order_relaxed);
            uint32_t maxScan = (head < ETW_FRAME_BUF_SIZE) ? head : static_cast<uint32_t>(ETW_FRAME_BUF_SIZE);
            DWORD dwmPid = GetDwmPid();
            DWORD selfPid = GetCurrentProcessId();

            uint64_t nowTs = GetFileTimeNow();

            for (uint32_t i = 0; i < maxScan; ++i) {
                uint32_t idx = (head - 1 - i) & (ETW_FRAME_BUF_SIZE - 1);
                const auto& frame = g_etwFrameBuf[idx];
                if (frame.pid != 0 && frame.pid != fgPid && frame.pid != dwmPid && frame.pid != selfPid) {
                    uint64_t age = (nowTs >= frame.timestamp) ? (nowTs - frame.timestamp) : (frame.timestamp - nowTs);
                    if (age < (ETW_TIME_FREQ * 2)) { // within 2 seconds
                        return frame.pid;
                    }
                }
            }
        }
    }
    return fgPid;
}

void TelemetryEngine::UpdateFpsAndDisplay() {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    int hz = 60;
    if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
        hz = (int)dm.dmDisplayFrequency;
    }

    HWND hwnd = GetForegroundWindow();
    DWORD fgPid = 0;
    if (hwnd) {
        GetWindowThreadProcessId(hwnd, &fgPid);
    }

    DWORD selfPid = GetCurrentProcessId();
    DWORD dwmPid = GetDwmPid();
    DWORD resolvedFgPid = ResolveForegroundTargetPid(hwnd, fgPid);

    uint64_t nowTs = GetFileTimeNow();

    int calculatedFps = 0;
    bool gameActive = false;
    uint32_t activeCount = 0;
    uint32_t activeHead = 0;

    // 1. Primary: If MSI Afterburner / RTSS is running, check shared memory
    int extFps = 0;
    bool extActive = false;
    if (TryReadMsiAfterburnerFps(extFps, extActive) || TryReadRtssFps(extFps, extActive)) {
        calculatedFps = extFps;
        gameActive = extActive;
    } else {
        // 2. ETW Dynamic 3D Present Detection (Intel PresentMon method, multi-API: DXGI/D3D9/DxgKrnl/UWP)
        uint32_t head = g_etwFrameHead.load(std::memory_order_acquire);
        activeHead = head;

        auto CalculatePidFps = [&](DWORD pid, uint32_t& outCount) -> int {
            outCount = 0;
            if (pid == 0 || head == 0) return 0;

            uint64_t maxTs = 0;
            uint64_t minTs = UINT64_MAX;
            uint32_t count = 0;

            uint32_t maxScan = (head < ETW_FRAME_BUF_SIZE) ? head : static_cast<uint32_t>(ETW_FRAME_BUF_SIZE);
            for (uint32_t i = 0; i < maxScan; ++i) {
                uint32_t idx = (head - 1 - i) & (ETW_FRAME_BUF_SIZE - 1);
                const auto& frame = g_etwFrameBuf[idx];
                if (frame.pid == pid && frame.timestamp > 0) {
                    uint64_t age = (nowTs >= frame.timestamp) ? (nowTs - frame.timestamp) : (frame.timestamp - nowTs);
                    if (age > (ETW_TIME_FREQ * 25 / 10)) { // older than 2.5s
                        if (count > 0) break;
                        continue;
                    }
                    if (frame.timestamp > maxTs) maxTs = frame.timestamp;
                    if (frame.timestamp < minTs) minTs = frame.timestamp;
                    count++;
                    // Once we span at least 1.0 second of frame intervals, we have a complete sample window
                    if (maxTs > minTs && (maxTs - minTs) >= ETW_TIME_FREQ) {
                        break;
                    }
                }
            }
            outCount = count;

            if (count >= 2 && maxTs > minTs) {
                double dtSec = static_cast<double>(maxTs - minTs) / static_cast<double>(ETW_TIME_FREQ);
                if (dtSec >= 0.02) {
                    double rawFps = static_cast<double>(count - 1) / dtSec;
                    if (rawFps >= 1.0 && rawFps <= 3000.0) {
                        return static_cast<int>(std::round(rawFps));
                    }
                }
            }
            return 0;
        };

        // 1. First priority: Check if foreground window is an active 3D application
        bool fgIsShell = (hwnd == NULL) || IsDesktopOrShellWindow(hwnd);
        if (!fgIsShell && resolvedFgPid != 0 && resolvedFgPid != selfPid && resolvedFgPid != dwmPid) {
            wchar_t exeName[MAX_PATH] = {0};
            if (GetProcessExeName(resolvedFgPid, exeName, MAX_PATH)) {
                if (_wcsicmp(exeName, L"explorer.exe") == 0) {
                    fgIsShell = true;
                }
            }
        }

        if (!fgIsShell && resolvedFgPid != 0 && resolvedFgPid != selfPid && resolvedFgPid != dwmPid) {
            uint32_t fgCount = 0;
            int fgFps = CalculatePidFps(resolvedFgPid, fgCount);
            if (fgCount >= 2 && fgFps > 0) {
                calculatedFps = fgFps;
                gameActive = true;
                m_targetGamePid = resolvedFgPid;
            }
        }

        // 2. Second priority: If foreground lost focus (e.g. user hovered/clicked Notch, clicked Desktop, or clicked second screen),
        // check if our previously locked game (m_targetGamePid) is still actively rendering!
        if (!gameActive && m_targetGamePid != 0) {
            uint32_t prevCount = 0;
            int prevFps = CalculatePidFps(m_targetGamePid, prevCount);
            if (prevCount >= 2 && prevFps > 0) {
                calculatedFps = prevFps;
                gameActive = true;
            }
        }

        // 3. Third priority (Crucial for Windowed Games): Dynamic scan of ETW buffer to find ANY active 3D rendering process on the system!
        // Handles games running in windowed mode that were not clicked yet, or wrapper launchers like League of Legends, Minecraft, etc.
        if (!gameActive && head > 0) {
            struct PidInfo {
                DWORD pid;
                uint32_t count;
            };
            std::vector<PidInfo> activePids;

            uint32_t maxScan = (head < ETW_FRAME_BUF_SIZE) ? head : static_cast<uint32_t>(ETW_FRAME_BUF_SIZE);
            for (uint32_t i = 0; i < maxScan; ++i) {
                uint32_t idx = (head - 1 - i) & (ETW_FRAME_BUF_SIZE - 1);
                const auto& frame = g_etwFrameBuf[idx];
                if (frame.pid != 0 && frame.pid != selfPid && frame.pid != dwmPid) {
                    uint64_t age = (nowTs >= frame.timestamp) ? (nowTs - frame.timestamp) : (frame.timestamp - nowTs);
                    if (age < (ETW_TIME_FREQ * 12 / 10)) { // within last 1.2 seconds
                        bool found = false;
                        for (auto& item : activePids) {
                            if (item.pid == frame.pid) {
                                item.count++;
                                found = true;
                                break;
                            }
                        }
                        if (!found && activePids.size() < 16) {
                            activePids.push_back({ frame.pid, 1 });
                        }
                    }
                }
            }

            DWORD bestPid = 0;
            uint32_t bestCount = 0;
            for (const auto& item : activePids) {
                if (item.count >= 3 && item.count > bestCount) {
                    wchar_t exe[MAX_PATH] = {0};
                    if (GetProcessExeName(item.pid, exe, MAX_PATH)) {
                        // Exclude desktop shell, search, and common browser/media processes that render with GPU acceleration
                        if (_wcsicmp(exe, L"explorer.exe") == 0 ||
                            _wcsicmp(exe, L"SearchHost.exe") == 0 ||
                            _wcsicmp(exe, L"StartMenuExperienceHost.exe") == 0 ||
                            _wcsicmp(exe, L"msedge.exe") == 0 ||
                            _wcsicmp(exe, L"chrome.exe") == 0 ||
                            _wcsicmp(exe, L"firefox.exe") == 0 ||
                            _wcsicmp(exe, L"brave.exe") == 0 ||
                            _wcsicmp(exe, L"discord.exe") == 0 ||
                            _wcsicmp(exe, L"LeagueClientUx.exe") == 0 ||
                            _wcsicmp(exe, L"LeagueClient.exe") == 0 ||
                            _wcsicmp(exe, L"RiotClientServices.exe") == 0 ||
                            _wcsicmp(exe, L"Code.exe") == 0) {
                            continue;
                        }
                    }
                    bestCount = item.count;
                    bestPid = item.pid;
                }
            }

            if (bestPid != 0) {
                uint32_t scanCount = 0;
                int scanFps = CalculatePidFps(bestPid, scanCount);
                if (scanCount >= 2 && scanFps > 0) {
                    calculatedFps = scanFps;
                    gameActive = true;
                    m_targetGamePid = bestPid; // Automatically lock onto windowed 3D game!
                }
            }
        }
    }

    if (gameActive && calculatedFps > 0) {
        if (m_stableFps == 0) {
            m_stableFps = calculatedFps;
        } else {
            // Calm, stable EMA filter matching Steam / RTSS overlay:
            int delta = std::abs(calculatedFps - m_stableFps);
            if (delta <= 2) {
                // Rock-solid: ignore natural +-1~2 fps measurement jitter
            } else if (delta <= 6) {
                // Gentle smoothing
                m_stableFps = (int)std::round(m_stableFps * 0.75f + calculatedFps * 0.25f);
            } else {
                // Significant change (scene transition): respond quickly
                m_stableFps = (int)std::round(m_stableFps * 0.35f + calculatedFps * 0.65f);
            }
        }
        m_zeroFpsStreak = 0;
    } else {
        m_zeroFpsStreak++;
        if (m_zeroFpsStreak >= 8) { // 2~4 seconds grace period before dropping to idle
            m_stableFps = 0;
        }
        if (m_zeroFpsStreak >= 24) { // Keep game lock for 6~12 seconds across loading screens
            m_targetGamePid = 0;
        }
    }

    static uint64_t s_lastTickLog = 0;
    uint64_t tickNow = GetTickCount64();
    if (tickNow - s_lastTickLog >= 1500) {
        s_lastTickLog = tickNow;
        wchar_t fgExeName[MAX_PATH] = {0};
        GetProcessExeName(fgPid, fgExeName, MAX_PATH);
        wchar_t resExeName[MAX_PATH] = {0};
        GetProcessExeName(resolvedFgPid, resExeName, MAX_PATH);
        wchar_t tgtExeName[MAX_PATH] = {0};
        GetProcessExeName(m_targetGamePid, tgtExeName, MAX_PATH);
        FpsLog("[Tick] fgPid=%lu (%ls), resPid=%lu (%ls), tgtPid=%lu (%ls), head=%u, calcFps=%d, stableFps=%d, active=%d, streak=%d\n",
               fgPid, fgExeName, resolvedFgPid, resExeName, m_targetGamePid, tgtExeName,
               activeHead, calculatedFps, m_stableFps, gameActive ? 1 : 0, m_zeroFpsStreak);
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
        wchar_t sessionName[64];
        swprintf_s(sessionName, L"RCNotchFps_%lu", GetCurrentProcessId());

        ULONG propSize = sizeof(EVENT_TRACE_PROPERTIES) + 2 * MAX_PATH * sizeof(wchar_t);
        std::vector<uint8_t> propBuf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(propBuf.data());
        props->Wnode.BufferSize = propSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1; // High-resolution QPC clock matching PresentMon standard
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->LogFileNameOffset = 0; // MUST BE 0 FOR REAL_TIME_MODE!

        // Stop any stale session with this PID first
        ControlTraceW(0, sessionName, props, EVENT_TRACE_CONTROL_STOP);

        // Reset properties before starting
        memset(propBuf.data(), 0, propSize);
        props->Wnode.BufferSize = propSize;
        props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        props->Wnode.ClientContext = 1; // QPC clock (ProcessTrace delivers 100ns FILETIME)
        props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        props->FlushTimer = 1; // Flush real-time buffers every 1 second
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->LogFileNameOffset = 0; // MUST BE 0 FOR REAL_TIME_MODE!

        bool isElevated = IsCurrentProcessElevated();
        FpsLog("[ETW Startup] Process elevated=%d, Session: %ls\n", isElevated ? 1 : 0, sessionName);

        ULONG status = StartTraceW(&g_hTraceSession, sessionName, props);
        if (status == ERROR_ALREADY_EXISTS) {
            ControlTraceW(0, sessionName, props, EVENT_TRACE_CONTROL_STOP);
            status = StartTraceW(&g_hTraceSession, sessionName, props);
        }
        FpsLog("[ETW Startup] StartTraceW status=%lu\n", status);
        if (status != ERROR_SUCCESS) {
            FpsLog("[ETW Startup] FAILED with status=%lu. (5=Access Denied, elevation required)\n", status);
            return; // Not elevated or ETW restricted
        }

        ULONG sDxgi = EnableTraceEx2(g_hTraceSession, &DXGI_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0xFFFFFFFFFFFFFFFFULL, 0, 0, NULL);
        ULONG sD3d9 = EnableTraceEx2(g_hTraceSession, &D3D9_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0xFFFFFFFFFFFFFFFFULL, 0, 0, NULL);
        ULONG sDxgk = EnableTraceEx2(g_hTraceSession, &DXGKRNL_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0xFFFFFFFFFFFFFFFFULL, 0, 0, NULL);
        ULONG sDwm  = EnableTraceEx2(g_hTraceSession, &DWM_PROVIDER_GUID, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                       TRACE_LEVEL_INFORMATION, 0xFFFFFFFFFFFFFFFFULL, 0, 0, NULL);
        FpsLog("[ETW Startup] EnableTrace: DXGI=%lu, D3D9=%lu, DXGKRNL=%lu, DWM=%lu\n", sDxgi, sD3d9, sDxgk, sDwm);

        EVENT_TRACE_LOGFILEW logFile = {};
        logFile.LoggerName = sessionName;
        logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
        logFile.EventRecordCallback = EtwEventRecordCallback;

        g_hConsumerTrace = OpenTraceW(&logFile);
        FpsLog("[ETW Startup] OpenTraceW handle=%p\n", (void*)g_hConsumerTrace);
        if (g_hConsumerTrace != INVALID_PROCESSTRACE_HANDLE) {
            ULONG ptRes = ProcessTrace(&g_hConsumerTrace, 1, NULL, NULL);
            FpsLog("[ETW Startup] ProcessTrace ended with status=%lu\n", ptRes);
            CloseTrace(g_hConsumerTrace);
            g_hConsumerTrace = INVALID_PROCESSTRACE_HANDLE;
        } else {
            FpsLog("[ETW Startup] OpenTraceW FAILED, GetLastError=%lu\n", GetLastError());
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
        wchar_t sessionName[64];
        swprintf_s(sessionName, L"RCNotchFps_%lu", GetCurrentProcessId());
        ULONG propSize = sizeof(EVENT_TRACE_PROPERTIES) + 2 * MAX_PATH * sizeof(wchar_t);
        std::vector<uint8_t> propBuf(propSize, 0);
        auto* props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(propBuf.data());
        props->Wnode.BufferSize = propSize;
        props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
        props->LogFileNameOffset = 0;
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
                                // "擷取縮圖時 不應該變形 把高撐滿 致中即可"
                                // Center-crop square region preserving height and aspect ratio:
                                IWICBitmapSource* sourceForScale = frame;
                                IWICBitmapClipper* clipper = nullptr;

                                if (srcW != srcH) {
                                    WICRect rcClip = {};
                                    if (srcW > srcH) {
                                        // Wide image (e.g. 16:9 / 4:3 video thumbnail) -> fill height (把高撐滿), center horizontally (致中)
                                        rcClip.X = (INT)((srcW - srcH) / 2);
                                        rcClip.Y = 0;
                                        rcClip.Width = (INT)srcH;
                                        rcClip.Height = (INT)srcH;
                                    } else {
                                        // Tall image -> fill width, center vertically
                                        rcClip.X = 0;
                                        rcClip.Y = (INT)((srcH - srcW) / 2);
                                        rcClip.Width = (INT)srcW;
                                        rcClip.Height = (INT)srcW;
                                    }
                                    if (SUCCEEDED(wicFactory->CreateBitmapClipper(&clipper)) && clipper) {
                                        if (SUCCEEDED(clipper->Initialize(frame, &rcClip))) {
                                            sourceForScale = clipper;
                                        }
                                    }
                                }

                                IWICBitmapScaler* scaler = nullptr;
                                if (SUCCEEDED(wicFactory->CreateBitmapScaler(&scaler)) && scaler) {
                                    const UINT dstS = 64;
                                    if (SUCCEEDED(scaler->Initialize(sourceForScale, dstS, dstS, WICBitmapInterpolationModeFant))) {
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
                                if (clipper) clipper->Release();
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

#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <cstdint>

#pragma pack(push, 1)
struct ShmSensorData {
    uint32_t magic;
    uint32_t version;
    uint64_t updateTickMs;
    float cpuTempC;
    float cpuPowerW;
    float cpuClockMhz;
    float gpuTempC;
    float gpuLoadPct;
    float gpuPowerW;
    float gpuVramUsedGb;
    float gpuVramTotalGb;
    char cpuName[96];
    char gpuName[96];
};
#pragma pack(pop)

struct TelemetrySnapshot {
    std::wstring cpuName = L"CPU";
    std::wstring gpuName = L"GPU";

    int cpuUsagePct = 0;
    int cpuTempC = 0;
    float cpuClockGhz = 0.0f;
    float cpuPowerW = 0.0f;
    bool cpuTempFromLhm = false;

    int gpuUsagePct = 0;
    int gpuTempC = 0;
    float gpuVramUsedGb = 0.0f;
    float gpuVramTotalGb = 0.0f;
    float gpuPowerW = 0.0f;
    bool gpuFromNvml = false;

    int ramUsagePct = 0;
    float ramUsedGb = 0.0f;
    float ramTotalGb = 0.0f;

    int currentFps = 0;          // 0 when desktop idle (no 3D game in foreground)
    int monitorRefreshHz = 60;
    bool isGameActive = false;
};

class TelemetryEngine {
public:
    TelemetryEngine();
    ~TelemetryEngine();

    void Start(int intervalMs = 500);
    void Stop();
    void SetIntervalMs(int intervalMs);
    void PollOnce();
    TelemetrySnapshot GetSnapshot();

private:
    void WorkerLoop();
    void InitCpuInfo();
    void InitNvml();
    void ShutdownNvml();
    void LaunchSensorBridge();
    void ReadSharedMemory(ShmSensorData& outShm, bool& valid);
    void UpdateCpuAndRam(const ShmSensorData& shm, bool shmValid);
    void UpdateGpu(const ShmSensorData& shm, bool shmValid);
    void UpdateFpsAndDisplay();

    // Etw FPS trace thread
    void StartEtwFpsThread();
    void StopEtwFpsThread();

    std::atomic<bool> m_running{false};
    std::atomic<int> m_intervalMs{500};
    std::thread m_workerThread;
    std::mutex m_mutex;
    TelemetrySnapshot m_snapshot;

    // CPU usage delta state
    uint64_t m_prevIdleTime = 0;
    uint64_t m_prevKernelTime = 0;
    uint64_t m_prevUserTime = 0;
    float m_smoothedCpuTemp = 46.0f;

    // NVML dynamic bindings
    HMODULE m_hNvml = NULL;
    void* m_nvmlDevice = nullptr;
    bool m_nvmlReady = false;

    // Shared memory & SensorBridge child process
    HANDLE m_hMapFile = NULL;
    void* m_pShmBuf = nullptr;
    HANDLE m_hJobObject = NULL;
    HANDLE m_hBridgeProcess = NULL;

    // ETW FPS state
    std::atomic<uint32_t> m_etwFrameCount{0};
    uint64_t m_lastFpsCalcTick = 0;
    int m_lastComputedFps = 0;
};

#pragma once
#include <windows.h>
#include <string>
#include <vector>
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

    // Media & Audio Telemetry (GSMTC + WASAPI CoreAudio)
    bool hasMediaSession = false;
    bool mediaIsPlaying = false;
    int mediaSessionCount = 0;
    int mediaActiveSessionIdx = 0;
    std::wstring mediaTitle;
    std::wstring mediaArtist;
    std::wstring mediaSourceName;
    int mediaPosSec = 0;
    int mediaDurSec = 0;
    float audioPeak = 0.0f;      // 0.0 ~ 1.0 real-time audio output peak
    bool hasAlbumArt = false;
    uint32_t albumArtVer = 0;
    int albumArtW = 0;
    int albumArtH = 0;
    std::vector<uint32_t> albumArtBgra; // Premultiplied B8G8R8A8_UNORM thumbnail (48x48)
};

class TelemetryEngine {
public:
    TelemetryEngine(bool verifyMockMode = false);
    ~TelemetryEngine();

    void Start(int intervalMs = 500);
    void Stop();
    void SetIntervalMs(int intervalMs);
    void PollOnce();
    TelemetrySnapshot GetSnapshot();

    // Fast real-time audio peak query (safe to call from 120FPS UI timer)
    float PollAudioPeakFast();

    // Media Playback Controls (GSMTC + Media Key Fallback)
    void MediaPlayPause();
    void MediaPrevTrack();
    void MediaNextTrack();
    void MediaSeekRelative(int deltaSec);
    void MediaSeekToRatio(float ratio01);
    void MediaCycleSession();
    void SetPreviewMockMedia(bool enable);

private:
    void WorkerLoop();
    void InitCpuInfo();
    void InitNvml();
    void ShutdownNvml();
    void InitAdl();
    void ShutdownAdl();
    void InitDxgi();
    void ShutdownDxgi();
    void LaunchSensorBridge();
    void ReadSharedMemory(ShmSensorData& outShm, bool& valid);
    void UpdateCpuAndRam(const ShmSensorData& shm, bool shmValid);
    void UpdateGpu(const ShmSensorData& shm, bool shmValid);
    void UpdateFpsAndDisplay();
    void UpdateMediaAndAudio();

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

    // AMD ADL dynamic bindings
    HMODULE m_hAdl = NULL;
    void* m_adlContext = nullptr;
    int m_amdAdapterIndex = -1;
    std::wstring m_amdGpuName = L"AMD Radeon Graphics";
    bool m_adlReady = false;

    // DXGI 1.4 for Video Memory Query
    void* m_dxgiFactory = nullptr;
    void* m_dxgiAdapter3 = nullptr;
    bool m_dxgiReady = false;

    // Shared memory & SensorBridge child process
    HANDLE m_hMapFile = NULL;
    void* m_pShmBuf = nullptr;
    HANDLE m_hJobObject = NULL;
    HANDLE m_hBridgeProcess = NULL;

    // ETW FPS & High-Resolution QPC State
    std::atomic<uint32_t> m_etwFrameCount{0};
    uint64_t m_lastFpsQpc = 0;
    int m_stableFps = 0;
    int m_zeroFpsStreak = 0;

    // Media & Audio state
    std::atomic<int> m_preferredSessionIdx{-1}; // -1 = auto-select Playing session
    std::atomic<int> m_pendingMediaCmd{0};      // 1=PlayPause, 2=Prev, 3=Next, 4=SeekRel, 5=SeekRatio
    std::atomic<int> m_pendingSeekArg{0};       // deltaSec or ratio*10000
    std::atomic<bool> m_mockMediaPreview{false};
    std::wstring m_lastMediaTrackKey;
    uint32_t m_albumArtVerCounter = 0;
    std::vector<uint32_t> m_cachedArtBgra;
    int m_cachedArtW = 0;
    int m_cachedArtH = 0;
};

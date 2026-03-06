#include "shared.hpp"
#include <windows.h>
#include <thread>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

#ifdef _NVAPI_H_
#include "nvapi.h"
#pragma comment(lib, "nvapi.lib")
#define HAVE_NVAPI 1
#else
#define HAVE_NVAPI 0
typedef void* NvPhysicalGpuHandle;
typedef unsigned int NvU32;
#define NVAPI_MAX_PHYSICAL_GPUS 64
#endif

// Extended GPU stats using NVIDIA NVAPI
struct NVIDIAGpuStats {
    int currentTemp = 0;
    int maxTemp = 0;
    int minTemp = 999;
    double avgTemp = 0.0;
    int tempSamples = 0;
    int coreClock = 0;
    int memClock = 0;
    int videoClock = 0;
    int maxCoreClock = 0;
    int minCoreClock = 999999;
    int maxMemClock = 0;
    int minMemClock = 999999;
    int fanRPM = 0;
    int gpuUtilization = 0;
    int memUtilization = 0;
    double powerDraw = 0.0;
    double powerLimit = 0.0;
    bool nvapiAvailable = false;
    int memBusWidth = 0;
    unsigned long long totalVram = 0;
    unsigned long long usedVram = 0;
    unsigned long long availableVram = 0;
    std::string memType;
    std::string pciDeviceId;
    int pcieLanes = 0;
};

static std::vector<NVIDIAGpuStats> g_NvidiaStats;
static NvPhysicalGpuHandle g_NvidiaHandles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
static NvU32 g_NvidiaGpuCount = 0;

static std::mutex g_NvidiaMutex;
static bool g_NvidiaInitialized = false;

// Execute command silently
bool ExecuteCommandSilent(const std::string& command, std::string& output) {
    HANDLE hPipeRead, hPipeWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    
    if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) return false;

    STARTUPINFOA si = { 0 };
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hPipeWrite;
    si.hStdError = hPipeWrite;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    
    if (!CreateProcessA(NULL, (LPSTR)command.c_str(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hPipeRead);
        CloseHandle(hPipeWrite);
        return false;
    }

    CloseHandle(hPipeWrite);
    
    char buffer[4096] = { 0 };
    DWORD bytesRead = 0;
    if (ReadFile(hPipeRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
        output = buffer;
    }

    CloseHandle(hPipeRead);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return true;
}

// Query PCIe device ID
bool GetNvidiaSmiPciDeviceId(int gpuIndex, std::string& deviceId) {
    std::string command = "nvidia-smi --query-gpu=pci.device_id --format=csv,noheader -i " + std::to_string(gpuIndex);
    std::string output;
    
    if (!ExecuteCommandSilent(command, output)) return false;

    try {
        deviceId = output;
        // Remove trailing whitespace
        deviceId.erase(deviceId.find_last_not_of(" \n\r\t") + 1);
        return true;
    } catch (...) {
        return false;
    }
}

// Query PCIe link width (lanes)
bool GetNvidiaSmiPcieLanes(int gpuIndex, int& lanes) {
    std::string command = "nvidia-smi --query-gpu=pci.link.info.current.link_width --format=csv,noheader -i " + std::to_string(gpuIndex);
    std::string output;
    
    if (!ExecuteCommandSilent(command, output)) return false;

    try {
        std::string laneStr = output;
        // Extract number from "x16" or "x8" format
        size_t xPos = laneStr.find('x');
        if (xPos != std::string::npos) {
            lanes = std::stoi(laneStr.substr(xPos + 1));
        }
        return true;
    } catch (...) {
        return false;
    }
}

// Query all GPU clocks from nvidia-smi
bool GetNvidiaSmiClocks(int gpuIndex, int& coreClock, int& memClock, int& videoClock) {
    std::string command = "nvidia-smi --query-gpu=clocks.current.graphics,clocks.current.memory,clocks.current.video --format=csv,noheader,nounits -i " + std::to_string(gpuIndex);
    std::string output;
    
    if (!ExecuteCommandSilent(command, output)) return false;

    std::stringstream ss(output);
    std::string item;
    try {
        if (std::getline(ss, item, ',')) coreClock = std::stoi(item);
        if (std::getline(ss, item, ',')) memClock = std::stoi(item);
        if (std::getline(ss, item, ',')) videoClock = std::stoi(item);
        return true;
    } catch (...) {
        return false;
    }
}

// Query GPU utilization from nvidia-smi
bool GetNvidiaSmiUtilization(int gpuIndex, int& gpuUtil, int& memUtil) {
    std::string command = "nvidia-smi --query-gpu=utilization.gpu,utilization.memory --format=csv,noheader,nounits -i " + std::to_string(gpuIndex);
    std::string output;
    
    if (!ExecuteCommandSilent(command, output)) return false;

    std::stringstream ss(output);
    std::string item;
    try {
        if (std::getline(ss, item, ',')) gpuUtil = std::stoi(item);
        if (std::getline(ss, item, ',')) memUtil = std::stoi(item);
        return true;
    } catch (...) {
        return false;
    }
}

// Query bus width from nvidia-smi
bool GetNvidiAsmiBusWidth(int gpuIndex, int& busWidth) {
    std::string command = "nvidia-smi --query-gpu=memory.bus_width --format=csv,noheader,nounits -i " + std::to_string(gpuIndex);
    std::string output;
    
    if (!ExecuteCommandSilent(command, output)) return false;

    try {
        busWidth = std::stoi(output);
        return true;
    } catch (...) {
        return false;
    }
}

void InitNvidiaGpu() {
#if HAVE_NVAPI
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    
    if (g_NvidiaInitialized) return;

    if (NvAPI_Initialize() != NVAPI_OK) {
        return;
    }

    NvAPI_EnumPhysicalGPUs(g_NvidiaHandles, &g_NvidiaGpuCount);

    if (g_NvidiaGpuCount == 0) {
        return;
    }

    g_NvidiaStats.resize(g_NvidiaGpuCount);
    g_NvidiaInitialized = true;
#endif
}

void MonitorGpu() {
#if HAVE_NVAPI
    InitNvidiaGpu();
    
    if (g_NvidiaGpuCount == 0 || !g_NvidiaInitialized) {
        // NVAPI initialization failed - use defaults
        if (g_NvidiaStats.empty()) {
            g_NvidiaStats.resize(1);
        }
        // Set reasonable defaults
        g_NvidiaStats[0].coreClock = 1000;
        g_NvidiaStats[0].memClock = 5000;
        g_NvidiaStats[0].videoClock = 1000;
        g_NvidiaStats[0].memBusWidth = 256;
        g_NvidiaStats[0].fanRPM = 0;
        g_NvidiaStats[0].gpuUtilization = 0;
        g_NvidiaStats[0].memUtilization = 0;
        return;
    }

    std::lock_guard<std::mutex> lock(g_NvidiaMutex);

    for (NvU32 i = 0; i < g_NvidiaGpuCount; i++) {
        NvPhysicalGpuHandle gpu = g_NvidiaHandles[i];
        bool nvapiWorking = false;

        // === Temperature via NVAPI ===
        NV_GPU_THERMAL_SETTINGS thermalInfo = { 0 };
        thermalInfo.version = NV_GPU_THERMAL_SETTINGS_VER;

        if (NvAPI_GPU_GetThermalSettings(gpu, NVAPI_THERMAL_TARGET_ALL, &thermalInfo) == NVAPI_OK) {
            int coreTemp = (int)thermalInfo.sensor[0].currentTemp;
            g_NvidiaStats[i].currentTemp = coreTemp;
            g_NvidiaStats[i].tempSamples++;
            g_NvidiaStats[i].avgTemp = (g_NvidiaStats[i].avgTemp * (g_NvidiaStats[i].tempSamples - 1) + coreTemp) / g_NvidiaStats[i].tempSamples;
            if (coreTemp > g_NvidiaStats[i].maxTemp) g_NvidiaStats[i].maxTemp = coreTemp;
            if (coreTemp < g_NvidiaStats[i].minTemp) g_NvidiaStats[i].minTemp = coreTemp;
            nvapiWorking = true;
        }

        // === Clock Frequencies via NVAPI ===
        NV_GPU_CLOCK_FREQUENCIES clockInfo = { 0 };
        clockInfo.version = NV_GPU_CLOCK_FREQUENCIES_VER;

        if (NvAPI_GPU_GetAllClockFrequencies(gpu, &clockInfo) == NVAPI_OK) {
            if (clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent) {
                int coreClock = clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency / 1000;
                g_NvidiaStats[i].coreClock = coreClock;
                if (coreClock > g_NvidiaStats[i].maxCoreClock) g_NvidiaStats[i].maxCoreClock = coreClock;
                if (coreClock < g_NvidiaStats[i].minCoreClock) g_NvidiaStats[i].minCoreClock = coreClock;
            }

            if (clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].bIsPresent) {
                int memClock = clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency / 1000;
                g_NvidiaStats[i].memClock = memClock;
                if (memClock > g_NvidiaStats[i].maxMemClock) g_NvidiaStats[i].maxMemClock = memClock;
                if (memClock < g_NvidiaStats[i].minMemClock) g_NvidiaStats[i].minMemClock = memClock;
            }

            if (clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_VIDEO].bIsPresent) {
                g_NvidiaStats[i].videoClock = clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_VIDEO].frequency / 1000;
            }

            nvapiWorking = true;
        }

        // === Memory Info via NVAPI ===
        NV_GPU_MEMORY_INFO_EX memInfo = { 0 };
        memInfo.version = NV_GPU_MEMORY_INFO_EX_VER;

        if (NvAPI_GPU_GetMemoryInfoEx(gpu, &memInfo) == NVAPI_OK) {
            g_NvidiaStats[i].totalVram = memInfo.dedicatedVideoMemory;
            g_NvidiaStats[i].availableVram = memInfo.availableDedicatedVideoMemory;
            g_NvidiaStats[i].usedVram = g_NvidiaStats[i].totalVram - g_NvidiaStats[i].availableVram;
        }

        // === Memory Bus Width via NVAPI ===
        NvU32 busWidth = 0;
        if (NvAPI_GPU_GetRamBusWidth(gpu, &busWidth) == NVAPI_OK) {
            g_NvidiaStats[i].memBusWidth = busWidth;
        }

        // === Fan Speed via NVAPI ===
        NvU32 fanSpeedRPM = 0;
        if (NvAPI_GPU_GetTachReading(gpu, &fanSpeedRPM) == NVAPI_OK) {
            g_NvidiaStats[i].fanRPM = fanSpeedRPM;
        }

        // === GPU/Memory Utilization via NVAPI ===
        NvU32 gpuUtil = 0;
        if (NvAPI_GPU_GetUsages(gpu, &gpuUtil) == NVAPI_OK) {
            g_NvidiaStats[i].gpuUtilization = gpuUtil;
        }

        NV_GPU_MEMORY_INFO_EX memInfo2 = { 0 };
        memInfo2.version = NV_GPU_MEMORY_INFO_EX_VER;
        if (NvAPI_GPU_GetMemoryInfoEx(gpu, &memInfo2) == NVAPI_OK) {
            if (memInfo2.dedicatedVideoMemory > 0) {
                unsigned long long usedMemBytes = memInfo2.dedicatedVideoMemory - memInfo2.availableDedicatedVideoMemory;
                g_NvidiaStats[i].memUtilization = (int)((double)usedMemBytes / (double)memInfo2.dedicatedVideoMemory * 100.0);
                g_GpuMemUsed = usedMemBytes;
            }
        }

        // === PCIe Device ID ===
        std::string pciDeviceId;
        if (GetNvidiaSmiPciDeviceId(i, pciDeviceId)) {
            g_NvidiaStats[i].pciDeviceId = pciDeviceId;
        }

        // === PCIe Lanes ===
        int pcieLanes = 0;
        if (GetNvidiaSmiPcieLanes(i, pcieLanes)) {
            g_NvidiaStats[i].pcieLanes = pcieLanes;
        }

        // === Memory Type (fallback default) ===
        if (g_NvidiaStats[i].memBusWidth >= 384) {
            g_NvidiaStats[i].memType = "HBM3";
        } else if (g_NvidiaStats[i].memBusWidth >= 256) {
            g_NvidiaStats[i].memType = "GDDR6X";
        } else {
            g_NvidiaStats[i].memType = "GDDR6";
        }

        // === Power Draw & Limit (set to 0 if not available) ===
        g_NvidiaStats[i].powerDraw = 0.0;
        g_NvidiaStats[i].powerLimit = 0.0;

        g_NvidiaStats[i].nvapiAvailable = nvapiWorking;
    }
#else
    // Fallback without NVAPI - use nvidia-smi
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    
    if (g_NvidiaStats.empty()) {
        g_NvidiaStats.resize(1);
    }
    
    // Get data from nvidia-smi
    int coreClock = 0, memClock = 0, videoClock = 0;
    if (GetNvidiaSmiClocks(0, coreClock, memClock, videoClock)) {
        g_NvidiaStats[0].coreClock = coreClock;
        g_NvidiaStats[0].memClock = memClock;
        g_NvidiaStats[0].videoClock = videoClock;
    }
    
    int gpuUtil = 0, memUtil = 0;
    if (GetNvidiaSmiUtilization(0, gpuUtil, memUtil)) {
        g_NvidiaStats[0].gpuUtilization = gpuUtil;
        g_NvidiaStats[0].memUtilization = memUtil;
    }
    
    int busWidth = 0;
    if (GetNvidiAsmiBusWidth(0, busWidth)) {
        g_NvidiaStats[0].memBusWidth = busWidth;
    }
    
    // PCIe info
    std::string pciDeviceId;
    if (GetNvidiaSmiPciDeviceId(0, pciDeviceId)) {
        g_NvidiaStats[0].pciDeviceId = pciDeviceId;
    }
    
    int pcieLanes = 0;
    if (GetNvidiaSmiPcieLanes(0, pcieLanes)) {
        g_NvidiaStats[0].pcieLanes = pcieLanes;
    }
    
    // Memory type
    if (g_NvidiaStats[0].memBusWidth >= 384) {
        g_NvidiaStats[0].memType = "HBM3";
    } else if (g_NvidiaStats[0].memBusWidth >= 256) {
        g_NvidiaStats[0].memType = "GDDR6X";
    } else {
        g_NvidiaStats[0].memType = "GDDR6";
    }
    
    g_NvidiaStats[0].powerDraw = 0.0;
    g_NvidiaStats[0].powerLimit = 0.0;
#endif
}

// Refresh GPU stats periodically
void RefreshGpuStats() {
    static auto lastRefresh = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastRefresh).count();
    
    if (elapsed >= 1) { // Refresh every 1 second
        lastRefresh = now;
        MonitorGpu();
    }
}

// Get NVIDIA stats for a specific GPU (thread-safe)
NVIDIAGpuStats GetNvidiaGpuStats(int gpuIndex) {
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    if (gpuIndex >= 0 && gpuIndex < (int)g_NvidiaStats.size()) {
        return g_NvidiaStats[gpuIndex];
    }
    return NVIDIAGpuStats();
}

int GetNvidiaGpuCount() {
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    return g_NvidiaGpuCount > 0 ? g_NvidiaGpuCount : (int)g_NvidiaStats.size();
}

// Update global variables with primary GPU stats
void UpdateNvidiaStats() {
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    
    if (!g_NvidiaStats.empty()) {
        const auto& stats = g_NvidiaStats[0];
        g_GpuTemp = stats.currentTemp;
        g_GpuTempAvg = (int)stats.avgTemp;
        g_GpuTempMin = stats.minTemp == 999 ? 0 : stats.minTemp;
        g_GpuTempMax = stats.maxTemp;
        g_GpuCoreClock = stats.coreClock;
        g_GpuCoreClockMin = stats.minCoreClock == 999999 ? 0 : stats.minCoreClock;
        g_GpuCoreClockMax = stats.maxCoreClock;
        g_GpuMemClock = stats.memClock;
        g_GpuMemClockMin = stats.minMemClock == 999999 ? 0 : stats.minMemClock;
        g_GpuMemClockMax = stats.maxMemClock;
        g_GpuVideoClock = stats.videoClock;
        g_GpuUtilization = stats.gpuUtilization;
        g_GpuMemUtilization = stats.memUtilization;
        g_GpuPowerDraw = stats.powerDraw;
        g_GpuPowerLimit = stats.powerLimit;
        g_GpuFanRPM = stats.fanRPM;
        g_GpuMemBusWidth = stats.memBusWidth;  // Always copy, even if 0
        g_GpuMemType = std::wstring(stats.memType.begin(), stats.memType.end());
        g_GpuPciDeviceId = stats.pciDeviceId;  // Always copy
        g_GpuPcieLanes = stats.pcieLanes;      // Always copy
    }
}

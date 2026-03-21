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

// Legacy Nvidia functions removed. All logic is now in gpu.cpp. This file is now a stub for compatibility only.

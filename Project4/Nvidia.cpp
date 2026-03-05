#include "shared.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

// ============================================================
//  NVAPI Dynamic Load (no .lib needed, works on any NVIDIA system)
// ============================================================
#include <windows.h>

typedef void* (*NvAPI_QueryInterface_t)(unsigned int);
typedef int   (*NvAPI_Initialize_t)();
typedef int   (*NvAPI_EnumPhysicalGPUs_t)(int handles[64], int* count);
typedef int   (*NvAPI_GPU_GetFullName_t)(int handle, char name[64]);
typedef int   (*NvAPI_GPU_GetThermalSettings_t)(int handle, int sensorIndex, void* pThermalSettings);
typedef int   (*NvAPI_GPU_GetTachReading_t)(int handle, unsigned int* pValue);
typedef int   (*NvAPI_GPU_GetAllClockFrequencies_t)(int handle, void* pClkFreqs);
typedef int   (*NvAPI_GPU_GetDynamicPstatesInfoEx_t)(int handle, void* pDynPstatesInfo);
typedef int   (*NvAPI_GPU_GetMemoryInfoEx_t)(int handle, void* pMemoryInfo);
typedef int   (*NvAPI_GPU_GetCurrentPstate_t)(int handle, int* pCurrentPstate);
typedef int   (*NvAPI_GPU_GetVoltageDomainsStatus_t)(int handle, void* pVoltStatus);
typedef int   (*NvAPI_GPU_GetPCIIdentifiers_t)(int handle, unsigned int* pDevId, unsigned int* pSSId, unsigned int* pRevId, unsigned int* pExtDevId);
typedef int   (*NvAPI_SYS_GetDriverAndBranchVersion_t)(unsigned int* pDriverVersion, char branch[64]);
typedef int   (*NvAPI_GPU_GetPowerUsage_t)(int handle, void* pPowerInfo);
typedef int   (*NvAPI_GPU_GetCurrentPCIEDownstreamWidth_t)(int handle, unsigned int* pWidth);
typedef int   (*NvAPI_GPU_GetPCIEInfo_t)(int handle, void* pInfo);
typedef int   (*NvAPI_GPU_GetGpuCoreCount_t)(int handle, unsigned int* pCount);
typedef int   (*NvAPI_GPU_GetVbiosVersionString_t)(int handle, char vbios[64]);
typedef int   (*NvAPI_GPU_GetPhysicalFrameBufferSize_t)(int handle, unsigned int* pSize);
typedef int   (*NvAPI_GPU_GetBoostClockFreq_t)(int handle, unsigned int clockType, unsigned int* pBoostClk);

static HMODULE           g_NvapiDll = nullptr;
static NvAPI_QueryInterface_t NvAPI_QueryInterface = nullptr;

// NVAPI function IDs
#define NVAPI_ID_EnumPhysicalGPUs              0xE5AC921F
#define NVAPI_ID_GPU_GetFullName               0xCEEE8E9F
#define NVAPI_ID_GPU_GetThermalSettings        0xE3640A56
#define NVAPI_ID_GPU_GetTachReading            0x5F608315
#define NVAPI_ID_GPU_GetAllClockFrequencies    0xDCB616C3
#define NVAPI_ID_GPU_GetDynamicPstatesInfoEx   0x60DED2ED
#define NVAPI_ID_GPU_GetMemoryInfoEx           0xC0599498
#define NVAPI_ID_GPU_GetCurrentPstate          0x927DA4F6
#define NVAPI_ID_GPU_GetVoltageDomainsStatus   0xDE536C58
#define NVAPI_ID_GPU_GetPCIIdentifiers         0x2DDFB66E
#define NVAPI_ID_SYS_GetDriverAndBranchVersion 0x2926AAAD
#define NVAPI_ID_GPU_GetPowerUsage             0x70916171
#define NVAPI_ID_GPU_GetCurrentPCIEDownstreamWidth 0x52216C61
#define NVAPI_ID_GPU_GetGpuCoreCount           0xC7026A87
#define NVAPI_ID_GPU_GetVbiosVersionString     0xA561FD7D
#define NVAPI_ID_GPU_GetPhysicalFrameBufferSize 0x46FBEB03
#define NVAPI_ID_Initialize                    0x0150E828

// NVAPI Structs (version-stamped manually to avoid SDK dependency)
#pragma pack(push, 8)
struct NV_THERMAL_SETTINGS_V2 {
    unsigned int version;           // NV_THERMAL_SETTINGS_VER2
    unsigned int count;
    struct {
        int controller;
        int defaultMinTemp;
        int defaultMaxTemp;
        int currentTemp;
        int target;
    } sensor[3];
};

struct NV_CLOCK_FREQUENCIES_V2 {
    unsigned int version;
    unsigned int ClockType : 4;
    unsigned int reserved : 28;
    struct {
        unsigned int bIsPresent : 1;
        unsigned int reserved : 31;
        unsigned int frequency;         // kHz
    } domain[32];
};

struct NV_GPU_DYNAMIC_PSTATES_INFO_EX_V1 {
    unsigned int version;
    unsigned int flags;
    struct {
        unsigned int bIsPresent : 1;
        unsigned int reserved : 31;
        unsigned int percentage;
        unsigned int percentageDecimalPart;
    } utilization[8];
};

struct NV_DISPLAY_DRIVER_MEMORY_INFO_V3 {
    unsigned int version;
    unsigned int dedicatedVideoMemory;          // KB
    unsigned int availableDedicatedVideoMemory; // KB
    unsigned int systemVideoMemory;             // KB
    unsigned int sharedSystemMemory;            // KB
    unsigned int curAvailableDedicatedVideoMemory; // KB
    unsigned int dedicatedVideoMemoryEvictionsSize;
    unsigned int dedicatedVideoMemoryEvictionCount;
};

struct NV_VOLT_STATUS_V1 {
    unsigned int version;
    unsigned int flags;
    unsigned int count;
    struct {
        unsigned int domainId;
        unsigned int flags;
        unsigned int mvolt; // millivolts
    } voltDomain[16];
};

struct NV_GPU_POWER_INFO_V1 {
    unsigned int version;
    unsigned int count;
    struct {
        unsigned int unknown;
        unsigned int percentOfTDP; // x 1000
    } entries[4];
};

#pragma pack(pop)

// Version stamps
#define MAKE_VER(st, v) (unsigned int)(sizeof(st) | (v) << 16)
#define NV_THERMAL_SETTINGS_VER2            MAKE_VER(NV_THERMAL_SETTINGS_V2, 2)
#define NV_CLOCK_FREQUENCIES_VER2           MAKE_VER(NV_CLOCK_FREQUENCIES_V2, 2)
#define NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER1 MAKE_VER(NV_GPU_DYNAMIC_PSTATES_INFO_EX_V1, 1)
#define NV_DISPLAY_DRIVER_MEMORY_INFO_VER3  MAKE_VER(NV_DISPLAY_DRIVER_MEMORY_INFO_V3, 3)
#define NV_VOLT_STATUS_VER1                 MAKE_VER(NV_VOLT_STATUS_V1, 1)
#define NV_GPU_POWER_INFO_VER1              MAKE_VER(NV_GPU_POWER_INFO_V1, 1)

// Domain indices for clock frequencies
#define NVAPI_CLOCK_DOMAIN_GRAPHICS 0
#define NVAPI_CLOCK_DOMAIN_MEMORY   4
#define NVAPI_CLOCK_DOMAIN_SHADER   7
#define NVAPI_CLOCK_DOMAIN_VIDEO    8

// Utilization domain indices
#define NVAPI_UTIL_GPU    0
#define NVAPI_UTIL_FB     1  // framebuffer (memory controller)
#define NVAPI_UTIL_VID    2  // video engine
#define NVAPI_UTIL_BUS    3  // bus interface

// ============================================================
//  nvidia-smi helper (fallback for fields NVAPI lacks)
// ============================================================
static std::string RunNvidiaSmi(const std::string& query, int gpuIdx = 0) {
    std::string cmd = "nvidia-smi --query-gpu=" + query +
        " --format=csv,noheader,nounits -i " + std::to_string(gpuIdx);
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[512]; std::string result;
    if (fgets(buf, sizeof(buf), pipe)) {
        result = buf;
        result.erase(0, result.find_first_not_of(" \n\r\t"));
        result.erase(result.find_last_not_of(" \n\r\t") + 1);
    }
    _pclose(pipe);
    return result;
}

// ============================================================
//  GPU Stats Structure — everything we can get
// ============================================================
struct NvidiaGpuStats {
    // Identity
    std::string name;
    std::string vbiosVersion;
    unsigned int deviceId = 0;
    unsigned int subSystemId = 0;
    unsigned int revisionId = 0;
    unsigned int coreCount = 0;

    // Driver
    std::string driverVersion;
    std::string driverBranch;

    // Performance state
    int pState = -1;            // P0 = max performance

    // Thermals
    int  temperature = 0;
    int  maxTemp = 0;
    int  minTemp = 999;
    double avgTemp = 0.0;
    int  tempSamples = 0;
    int  tempTarget = 0;    // GPU's own thermal target

    // Clocks (MHz)
    int coreClock = 0;
    int memClock = 0;
    int shaderClock = 0;
    int videoClock = 0;
    int maxCoreClock = 0;
    int minCoreClock = INT_MAX;
    int maxMemClock = 0;
    int minMemClock = INT_MAX;
    int maxShaderClock = 0;
    int maxVideoClock = 0;
    // nvidia-smi boost clocks
    int boostCoreClock = 0;    // from smi: clocks.max.graphics
    int boostMemClock = 0;    // from smi: clocks.max.memory

    // Utilization (%)
    int gpuUtil = 0;
    int memCtrlUtil = 0;    // framebuffer / memory controller
    int videoUtil = 0;    // video engine
    int busUtil = 0;    // bus interface

    // Fan
    unsigned int fanRPM = 0;    // actual RPM via NVAPI tach
    int          fanPct = 0;    // % from nvidia-smi (fallback)

    // Power (watts)
    double powerDraw = 0.0;
    double powerLimit = 0.0;
    double defaultPowerLimit = 0.0;
    double minPowerLimit = 0.0;
    double maxPowerLimit = 0.0;

    // Voltage (millivolts from NVAPI, then converted to V)
    double voltageCore = 0.0;  // V
    bool   voltageAvail = false;

    // Memory (bytes)
    unsigned long long memTotal = 0;
    unsigned long long memUsed = 0;
    unsigned long long memFree = 0;
    unsigned long long memReserved = 0;   // reserved by driver

    // PCIe
    unsigned int pcieLanes = 0;    // current negotiated lanes
    std::string  pcieLinkInfo;          // e.g. "Gen4 x16"

    // nvidia-smi extras (not always in NVAPI)
    std::string  computeMode;
    int          encUtil = 0;
    int          decUtil = 0;
    double       gpuTempSlow = 0.0;     // GPU throttle slow-down temp from smi
    double       gpuTempShut = 0.0;     // GPU shutdown temp from smi
};

// ============================================================
//  Globals
// ============================================================
static std::vector<NvidiaGpuStats> g_NvidiaStats;
static std::mutex                  g_NvidiaMutex;
static int                         g_NvapiHandles[64] = {};
static int                         g_NvapiGpuCount = 0;
static bool                        g_NvapiReady = false;

// ============================================================
//  NVAPI Init
// ============================================================
static bool LoadNvapi() {
#ifdef _WIN64
    g_NvapiDll = LoadLibraryA("nvapi64.dll");
#else
    g_NvapiDll = LoadLibraryA("nvapi.dll");
#endif
    if (!g_NvapiDll) return false;

    NvAPI_QueryInterface = (NvAPI_QueryInterface_t)GetProcAddress(g_NvapiDll, "nvapi_QueryInterface");
    if (!NvAPI_QueryInterface) return false;

    auto init = (NvAPI_Initialize_t)NvAPI_QueryInterface(NVAPI_ID_Initialize);
    if (!init || init() != 0) return false;

    return true;
}

void InitNvidiaGpu() {
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);

    g_NvapiReady = LoadNvapi();

    if (g_NvapiReady) {
        auto enumGPUs = (NvAPI_EnumPhysicalGPUs_t)NvAPI_QueryInterface(NVAPI_ID_EnumPhysicalGPUs);
        if (enumGPUs && enumGPUs(g_NvapiHandles, &g_NvapiGpuCount) == 0 && g_NvapiGpuCount > 0) {
            g_NvidiaStats.resize(g_NvapiGpuCount);

            // --- Static one-time queries ---
            auto getFullName = (NvAPI_GPU_GetFullName_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetFullName);
            auto getVbios = (NvAPI_GPU_GetVbiosVersionString_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetVbiosVersionString);
            auto getPCIIds = (NvAPI_GPU_GetPCIIdentifiers_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetPCIIdentifiers);
            auto getCoreCount = (NvAPI_GPU_GetGpuCoreCount_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetGpuCoreCount);
            auto getDriver = (NvAPI_SYS_GetDriverAndBranchVersion_t)NvAPI_QueryInterface(NVAPI_ID_SYS_GetDriverAndBranchVersion);

            for (int i = 0; i < g_NvapiGpuCount; i++) {
                int h = g_NvapiHandles[i];

                if (getFullName) {
                    char name[64] = {};
                    if (getFullName(h, name) == 0) g_NvidiaStats[i].name = name;
                }
                if (getVbios) {
                    char vbios[64] = {};
                    if (getVbios(h, vbios) == 0) g_NvidiaStats[i].vbiosVersion = vbios;
                }
                if (getPCIIds) {
                    getPCIIds(h,
                        &g_NvidiaStats[i].deviceId,
                        &g_NvidiaStats[i].subSystemId,
                        &g_NvidiaStats[i].revisionId,
                        nullptr);
                }
                if (getCoreCount) {
                    getCoreCount(h, &g_NvidiaStats[i].coreCount);
                }
                if (getDriver && i == 0) {
                    unsigned int driverVer = 0; char branch[64] = {};
                    if (getDriver(&driverVer, branch) == 0) {
                        // Version is like 53104 → "531.04"
                        char verStr[16];
                        sprintf_s(verStr, "%u.%02u", driverVer / 100, driverVer % 100);
                        g_NvidiaStats[i].driverVersion = verStr;
                        g_NvidiaStats[i].driverBranch = branch;
                    }
                }

                // Boost/max clocks via smi (static)
                std::string boostCore = RunNvidiaSmi("clocks.max.graphics", i);
                std::string boostMem = RunNvidiaSmi("clocks.max.memory", i);
                try { if (!boostCore.empty()) g_NvidiaStats[i].boostCoreClock = std::stoi(boostCore); }
                catch (...) {}
                try { if (!boostMem.empty())  g_NvidiaStats[i].boostMemClock = std::stoi(boostMem); }
                catch (...) {}

                // Power limits via smi (static)
                auto parseSmiDouble = [&](const std::string& q) -> double {
                    std::string s = RunNvidiaSmi(q, i);
                    try { return s.empty() ? 0.0 : std::stod(s); }
                    catch (...) { return 0.0; }
                    };
                g_NvidiaStats[i].defaultPowerLimit = parseSmiDouble("power.default_limit");
                g_NvidiaStats[i].minPowerLimit = parseSmiDouble("power.min_limit");
                g_NvidiaStats[i].maxPowerLimit = parseSmiDouble("power.max_limit");

                // Compute mode (static)
                g_NvidiaStats[i].computeMode = RunNvidiaSmi("compute_mode", i);

                // Shutdown / slowdown temps via smi (static thresholds)
                g_NvidiaStats[i].gpuTempSlow = parseSmiDouble("temperature.gpu.tlimit");
                g_NvidiaStats[i].gpuTempShut = parseSmiDouble("temperature.gpu.shutdown");
            }
        }
        else {
            // NVAPI found but no GPUs — fall back to smi-only
            g_NvapiReady = false;
            g_NvapiGpuCount = 0;
            goto smi_fallback;
        }
    }
    else {
    smi_fallback:
        // Pure nvidia-smi path — count GPUs
        std::string countStr = RunNvidiaSmi("count");
        if (countStr.empty()) return;
        try {
            int count = std::stoi(countStr);
            g_NvidiaStats.resize(count);
            for (int i = 0; i < count; i++) {
                g_NvidiaStats[i].name = RunNvidiaSmi("name", i);
            }
        }
        catch (...) {}
    }
}

// ============================================================
//  Poll — called periodically from MonitorGpu thread
// ============================================================
void MonitorGpu() {
    if (!g_AppRunning) return;
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    if (g_NvidiaStats.empty()) return;

    for (int i = 0; i < (int)g_NvidiaStats.size() && g_AppRunning; i++) {
        NvidiaGpuStats& s = g_NvidiaStats[i];

        // ---- NVAPI dynamic polling ----
        if (g_NvapiReady && i < g_NvapiGpuCount) {
            int h = g_NvapiHandles[i];

            // Temperature
            auto getThermal = (NvAPI_GPU_GetThermalSettings_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetThermalSettings);
            if (getThermal) {
                NV_THERMAL_SETTINGS_V2 ts = {};
                ts.version = NV_THERMAL_SETTINGS_VER2;
                if (getThermal(h, 0, &ts) == 0 && ts.count > 0) {
                    int temp = ts.sensor[0].currentTemp;
                    s.temperature = temp;
                    s.tempTarget = ts.sensor[0].defaultMaxTemp;
                    s.tempSamples++;
                    s.avgTemp = (s.avgTemp * (s.tempSamples - 1) + temp) / s.tempSamples;
                    if (temp > s.maxTemp) s.maxTemp = temp;
                    if (temp < s.minTemp) s.minTemp = temp;
                }
            }

            // Fan RPM (actual tach reading)
            auto getTach = (NvAPI_GPU_GetTachReading_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetTachReading);
            if (getTach) {
                unsigned int rpm = 0;
                if (getTach(h, &rpm) == 0) s.fanRPM = rpm;
            }

            // Clocks
            auto getClocks = (NvAPI_GPU_GetAllClockFrequencies_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetAllClockFrequencies);
            if (getClocks) {
                NV_CLOCK_FREQUENCIES_V2 cf = {};
                cf.version = NV_CLOCK_FREQUENCIES_VER2;
                cf.ClockType = 0; // current clocks
                if (getClocks(h, &cf) == 0) {
                    auto mhz = [&](int d) -> int {
                        return cf.domain[d].bIsPresent ? (int)(cf.domain[d].frequency / 1000) : 0;
                        };
                    int gc = mhz(NVAPI_CLOCK_DOMAIN_GRAPHICS);
                    int mc = mhz(NVAPI_CLOCK_DOMAIN_MEMORY);
                    int sc = mhz(NVAPI_CLOCK_DOMAIN_SHADER);
                    int vc = mhz(NVAPI_CLOCK_DOMAIN_VIDEO);
                    s.coreClock = gc; s.memClock = mc;
                    s.shaderClock = sc; s.videoClock = vc;
                    if (gc > s.maxCoreClock)   s.maxCoreClock = gc;
                    if (gc < s.minCoreClock && gc > 0) s.minCoreClock = gc;
                    if (mc > s.maxMemClock)    s.maxMemClock = mc;
                    if (mc < s.minMemClock && mc > 0) s.minMemClock = mc;
                    if (sc > s.maxShaderClock) s.maxShaderClock = sc;
                    if (vc > s.maxVideoClock)  s.maxVideoClock = vc;
                }
            }

            // Utilization
            auto getUtil = (NvAPI_GPU_GetDynamicPstatesInfoEx_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetDynamicPstatesInfoEx);
            if (getUtil) {
                NV_GPU_DYNAMIC_PSTATES_INFO_EX_V1 pi = {};
                pi.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER1;
                if (getUtil(h, &pi) == 0) {
                    s.gpuUtil = pi.utilization[NVAPI_UTIL_GPU].bIsPresent ? (int)pi.utilization[NVAPI_UTIL_GPU].percentage : 0;
                    s.memCtrlUtil = pi.utilization[NVAPI_UTIL_FB].bIsPresent ? (int)pi.utilization[NVAPI_UTIL_FB].percentage : 0;
                    s.videoUtil = pi.utilization[NVAPI_UTIL_VID].bIsPresent ? (int)pi.utilization[NVAPI_UTIL_VID].percentage : 0;
                    s.busUtil = pi.utilization[NVAPI_UTIL_BUS].bIsPresent ? (int)pi.utilization[NVAPI_UTIL_BUS].percentage : 0;
                }
            }

            // Memory (via NVAPI — gives reserved memory too)
            auto getMemEx = (NvAPI_GPU_GetMemoryInfoEx_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetMemoryInfoEx);
            if (getMemEx) {
                NV_DISPLAY_DRIVER_MEMORY_INFO_V3 mi = {};
                mi.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER3;
                if (getMemEx(h, &mi) == 0) {
                    unsigned long long totalKB = mi.dedicatedVideoMemory;
                    unsigned long long availKB = mi.curAvailableDedicatedVideoMemory;
                    unsigned long long reservedKB = totalKB > (availKB + (totalKB - mi.dedicatedVideoMemory)) ?
                        totalKB - mi.availableDedicatedVideoMemory - availKB : 0;
                    s.memTotal = totalKB * 1024ULL;
                    s.memFree = availKB * 1024ULL;
                    s.memUsed = (totalKB - availKB) * 1024ULL;
                    s.memReserved = (mi.dedicatedVideoMemory - mi.availableDedicatedVideoMemory) * 1024ULL;
                    (void)reservedKB;
                }
            }

            // Voltage
            auto getVolt = (NvAPI_GPU_GetVoltageDomainsStatus_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetVoltageDomainsStatus);
            if (getVolt) {
                NV_VOLT_STATUS_V1 vs = {};
                vs.version = NV_VOLT_STATUS_VER1;
                if (getVolt(h, &vs) == 0 && vs.count > 0) {
                    s.voltageCore = vs.voltDomain[0].mvolt / 1000.0;
                    s.voltageAvail = true;
                }
            }

            // Performance state
            auto getPstate = (NvAPI_GPU_GetCurrentPstate_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetCurrentPstate);
            if (getPstate) {
                int pstate = -1;
                if (getPstate(h, &pstate) == 0) s.pState = pstate;
            }

            // PCIe lanes
            auto getPcieLanes = (NvAPI_GPU_GetCurrentPCIEDownstreamWidth_t)NvAPI_QueryInterface(NVAPI_ID_GPU_GetCurrentPCIEDownstreamWidth);
            if (getPcieLanes) {
                unsigned int lanes = 0;
                if (getPcieLanes(h, &lanes) == 0) s.pcieLanes = lanes;
            }
        }

        // ---- nvidia-smi supplement (fields NVAPI doesn't cleanly expose) ----
        {
            // Power draw + current limit
            auto parseDbl = [&](const std::string& q) -> double {
                std::string r = RunNvidiaSmi(q, i);
                try { return r.empty() ? 0.0 : std::stod(r); }
                catch (...) { return 0.0; }
                };
            auto parseInt = [&](const std::string& q) -> int {
                std::string r = RunNvidiaSmi(q, i);
                try { return r.empty() ? 0 : std::stoi(r); }
                catch (...) { return 0; }
                };

            s.powerDraw = parseDbl("power.draw");
            s.powerLimit = parseDbl("power.limit");

            // Encoder / decoder utilization
            s.encUtil = parseInt("utilization.encoder");
            s.decUtil = parseInt("utilization.decoder");

            // PCIe generation (smi gives current gen cleanly)
            std::string pciGen = RunNvidiaSmi("pcie.link.gen.current", i);
            std::string pciWidth = RunNvidiaSmi("pcie.link.width.current", i);
            if (!pciGen.empty() && !pciWidth.empty()) {
                s.pcieLinkInfo = "Gen" + pciGen + " x" + pciWidth;
                if (s.pcieLanes == 0) {
                    try { s.pcieLanes = (unsigned int)std::stoi(pciWidth); }
                    catch (...) {}
                }
            }

            // Fan % (fallback if NVAPI tach not available)
            if (s.fanRPM == 0) {
                s.fanPct = parseInt("fan.speed");
                s.fanRPM = (unsigned int)(s.fanPct * 3000 / 100); // rough estimate
            }
            else {
                // Also grab fan % for display alongside RPM
                s.fanPct = parseInt("fan.speed");
            }

            // If NVAPI memory failed, use smi
            if (s.memTotal == 0) {
                unsigned long long usedMB = (unsigned long long)parseInt("memory.used");
                unsigned long long totalMB = (unsigned long long)parseInt("memory.total");
                s.memUsed = usedMB * 1024ULL * 1024ULL;
                s.memTotal = totalMB * 1024ULL * 1024ULL;
                s.memFree = (totalMB > usedMB) ? (totalMB - usedMB) * 1024ULL * 1024ULL : 0;
            }

            // If NVAPI util failed, use smi
            if (s.gpuUtil == 0) {
                s.gpuUtil = parseInt("utilization.gpu");
                s.memCtrlUtil = parseInt("utilization.memory");
            }

            // Temperature fallback
            if (s.temperature == 0) {
                s.temperature = parseInt("temperature.gpu");
            }
        }
    }
}

// ============================================================
//  Thread-safe accessors
// ============================================================
NvidiaGpuStats GetNvidiaGpuStats(int idx) {
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    if (idx >= 0 && idx < (int)g_NvidiaStats.size()) return g_NvidiaStats[idx];
    return {};
}

int GetNvidiaGpuCount() {
    std::lock_guard<std::mutex> lock(g_NvidiaMutex);
    return (int)g_NvidiaStats.size();
}
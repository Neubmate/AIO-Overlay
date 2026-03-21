#pragma once
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <wbemidl.h>
#include <comdef.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "wbemuuid.lib") 

// App State
extern std::atomic<bool> g_AppRunning;
extern std::mutex g_StatsMutex;
extern std::mutex g_IoMutex;

// Stress & Bench
extern std::atomic<bool> g_CpuStress;
extern std::atomic<bool> g_GpuStress;
extern std::atomic<bool> g_RamStress;
extern std::atomic<int> g_BenchScore;
extern std::atomic<bool> g_BenchRunning;
extern std::atomic<int> g_BenchProgress;
extern std::wstring g_BenchMode;
extern std::atomic<int> g_GpuScore;
extern std::atomic<bool> g_GpuBenchRunning;

// Hardware Stats
extern int g_CpuUsage;
extern std::vector<int> g_CoreLoad;
extern int g_CpuTemp;
extern std::wstring g_CpuName;

extern int g_RamLoad;
extern std::wstring g_RamText;

extern std::wstring g_GpuName;
extern unsigned long long g_GpuVramUsed;
extern unsigned long long g_GpuVramTotal;

// GPU Stats (NVAPI)
extern int g_GpuTemp;
extern int g_GpuTempAvg;
extern int g_GpuTempMin;
extern int g_GpuTempMax;
extern int g_GpuCoreClock;
extern int g_GpuCoreClockMin;
extern int g_GpuCoreClockMax;
extern int g_GpuMemClock;
extern int g_GpuMemClockMin;
extern int g_GpuMemClockMax;
extern int g_GpuVideoClock;
extern int g_GpuUtilization;
extern int g_GpuMemUtilization;
extern double g_GpuPowerDraw;
extern double g_GpuPowerLimit;
extern int g_GpuFanRPM;

// GPU Hardware Info
extern int g_GpuMemBusWidth;
extern unsigned long long g_GpuMemUsed;
extern unsigned long long g_GpuMemReserved;
extern std::wstring g_GpuMemType;
extern std::wstring g_GpuDriverVersion;
extern std::string g_GpuPciDeviceId;
extern int g_GpuPcieLanes;

extern std::wstring g_MoboName;
extern std::wstring g_BiosWmi;
extern std::wstring g_UpgradePath;

// CPU Core Clock Customization
extern int g_CpuClock;
extern int g_CpuTdp;
extern std::wstring g_RamTimings;
extern int g_SelectedCoreIndex;
extern int g_TargetCoreClock;

// Function declarations
bool SetCpuCoreClock(int core, int mhz);
void UpdateCpuAndRamInfo();

// ZenStates-Core CPU Info exports
extern "C" {
    const char* GetCpuNameNative();
    unsigned int GetCpuFamilyNative();
    unsigned int GetCpuCodeNameNative();
    unsigned int GetPackageTypeNative();
    unsigned int GetCcdsNative();
    unsigned int GetCcxsNative();
    unsigned int GetCoresPerCcxNative();
    unsigned int GetLogicalCoresNative();
    unsigned int GetPhysicalCoresNative();
    unsigned int GetPatchLevelNative();
    unsigned int GetSvi2CoreAddressNative();
    unsigned int GetSvi2SocAddressNative();
}

extern std::wstring g_AgesaVersion;

extern int g_GlobalThreads;
extern int g_ContextSwitches;

extern std::vector<std::wstring> g_DriveInfo;

// Motherboard Sensors
extern int g_DetectedChipID;
extern int g_DebugID;
extern float g_Volt12V;
extern float g_Volt5V;
extern float g_VoltVCore;
extern float g_VoltDram;
extern float g_VoltSoC;
extern int g_TempVRM;
extern int g_TempPCH;
extern int g_TempSocket;
extern int g_TempSystem;

// Fan
extern int g_FanSpeedPct;
extern int g_FanRPM;
extern bool g_FanControlActive;

// Battery
extern bool g_HasBattery;
extern int g_BatteryPct;
extern std::wstring g_BatteryTime;

// Config
extern bool g_LoggingEnabled;
extern std::wstring g_LogPath;

// Functions
void StartBenchmark(bool multiCore);
void StartGpuBenchmark();
void StartCpuStress();
void StartGpuStress();
void StartRamStress();
void MonitorCpu();
void MonitorGpu();
void RefreshGpuStats();
void MonitorSystem();
void InitGpuInfo();
void InitNvidiaGpu();
void UpdateGpuVram();
void UpdateNvidiaStats();
void GetDetailedRamInfo();
void InitDiskPdh();
void UpdateDiskIo();
void CheckStorage();
void UpdateBattery();

// SIO
bool InitFanControl();
void SetFanSpeed(int pct);

// SmuBridge
extern std::wstring g_SmuCpuName;
extern std::wstring g_SmuCpuId;
extern std::wstring g_SmuModel;
extern std::wstring g_SmuPackage;
extern std::wstring g_SmuConfig;
extern std::wstring g_SmuMobo;
extern std::wstring g_SmuBios;
extern std::wstring g_SmuVersion;
extern std::wstring g_SmuPatch;
extern std::wstring g_SmuTemp;
extern std::wstring g_SmuAcfResult;
extern bool         g_SmuReady;
extern bool         g_SmuProchot;
bool InitSmuBridge();
bool SmuSetAllCoreFrequency(int mhz);
std::string SmuGetRawOutput();

class WmiQuery {
public:
    WmiQuery();
    ~WmiQuery();
    bool Init(const std::wstring& namesSpace = L"ROOT\\CIMV2");
    IEnumWbemClassObject* Exec(const std::wstring& query);
private:
    IWbemLocator* pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
};
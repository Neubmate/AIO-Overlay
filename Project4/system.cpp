#include "shared.hpp"
#include "ChipDefs.hpp"
#include <fstream>
#include <chrono>
#include <thread>
#include <iomanip>

// Global Definitions
std::wstring g_MoboName = L"Detecting...";
std::wstring g_BiosWmi = L"...";
int g_GlobalThreads = 0;
int g_ContextSwitches = 0;

std::wstring g_UpgradePath = L"";
std::wstring g_BiosAnalysis = L"";
std::wstring g_AgesaVersion = L"";

bool g_HasBattery = false;
int g_BatteryPct = 0;
std::wstring g_BatteryTime = L"";

// Detailed Sensors
int g_DetectedChipID = 0;
int g_DebugID = 0;
float g_Volt12V = 0.0f;
float g_Volt5V = 0.0f;
float g_VoltVCore = 0.0f;
float g_VoltDram = 0.0f;
float g_VoltSoC = 0.0f;
int g_TempVRM = 0;
int g_TempPCH = 0;
int g_TempSocket = 0;
int g_TempSystem = 0;
int g_FanRPM = 0;

// Logging
bool g_LoggingEnabled = false;
std::wstring g_LogPath = L"stats_log.csv";
std::thread g_LogThread;

std::mutex g_IoMutex;

// ---------------------------------------------------------
//  MOTHERBOARD/BIOS MONITORING (WMI only)
// ---------------------------------------------------------

void UpdateBattery() {
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        std::lock_guard<std::mutex> l(g_StatsMutex);
        g_HasBattery = (sps.BatteryFlag != 128 && sps.BatteryFlag != 255);
        if (g_HasBattery) {
            g_BatteryPct = sps.BatteryLifePercent;
            if (sps.BatteryLifeTime != -1 && sps.ACLineStatus == 0) {
                wchar_t buf[32]; swprintf_s(buf, L"%dh %02dm", sps.BatteryLifeTime / 3600, (sps.BatteryLifeTime % 3600) / 60);
                g_BatteryTime = buf;
            }
            else {
                g_BatteryTime = (sps.ACLineStatus == 1) ? L"Charging" : L"...";
            }
        }
    }
}

WmiQuery::WmiQuery() { CoInitializeEx(0, COINIT_MULTITHREADED); }
WmiQuery::~WmiQuery() { if (pSvc) pSvc->Release(); if (pLoc) pLoc->Release(); CoUninitialize(); }
bool WmiQuery::Init(const std::wstring& namesSpace) {
    HRESULT hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) return false;
    hres = pLoc->ConnectServer(_bstr_t(namesSpace.c_str()), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    if (FAILED(hres)) return false;
    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    return SUCCEEDED(hres);
}
IEnumWbemClassObject* WmiQuery::Exec(const std::wstring& query) {
    if (!pSvc) return nullptr;
    IEnumWbemClassObject* pEnum = nullptr;
    pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query.c_str()), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
    return pEnum;
}

static std::wstring GetVariantString(IWbemClassObject* pObj, const std::wstring& prop) {
    VARIANT v; pObj->Get(prop.c_str(), 0, &v, 0, 0);
    std::wstring res = (v.vt == VT_BSTR) ? v.bstrVal : L"Unknown";
    VariantClear(&v); return res;
}
static int GetVariantInt(IWbemClassObject* pObj, const std::wstring& prop) {
    VARIANT v; pObj->Get(prop.c_str(), 0, &v, 0, 0);
    int res = (v.vt == VT_I4) ? v.intVal : (v.vt == VT_UI4) ? (int)v.uintVal : 0;
    VariantClear(&v); return res;
}

void MonitorSystem() {
    WmiQuery wmi; wmi.Init();

    IEnumWbemClassObject* pEnum = wmi.Exec(L"SELECT Product FROM Win32_BaseBoard");
    if (pEnum) {
        IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
        pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
        if (uRet) {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_MoboName = GetVariantString(pObj, L"Product");
            pObj->Release();
        }
        pEnum->Release();
    }

    while (g_AppRunning) {
        pEnum = wmi.Exec(L"SELECT Threads, ContextSwitchesPerSec FROM Win32_PerfFormattedData_PerfOS_System");
        if (pEnum) {
            IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
            pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
            if (uRet) {
                int t = GetVariantInt(pObj, L"Threads");
                int c = GetVariantInt(pObj, L"ContextSwitchesPerSec");
                { std::lock_guard<std::mutex> l(g_StatsMutex); g_GlobalThreads = t; g_ContextSwitches = c; }
                pObj->Release();
            }
            pEnum->Release();
        }
        Sleep(1000);
    }
}

void LogWorker() {
    std::ofstream file(g_LogPath, std::ios::app);
    if (!file.is_open()) return;
    file << "Timestamp,CPU_Usage,CPU_Temp\n";
    while (g_LoggingEnabled && g_AppRunning) {
        {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            auto now = std::chrono::system_clock::now();
            auto timeT = std::chrono::system_clock::to_time_t(now);
            file << timeT << "," << g_CpuUsage << "," << g_CpuTemp << "\n";
        }
        file.flush(); Sleep(1000);
    }
}
void StartLogging() { if (g_LoggingEnabled) return; g_LoggingEnabled = true; g_LogThread = std::thread(LogWorker); g_LogThread.detach(); }
void StopLogging() { g_LoggingEnabled = false; }
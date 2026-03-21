#include "shared.hpp"
#include <pdh.h>
#include <pdhmsg.h>
#include <immintrin.h>
#include <intrin.h>
#include <fstream>
#include <filesystem>
#include <comdef.h>
#include <Wbemidl.h>
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

namespace fs = std::filesystem;

// Definitions
int g_CpuUsage = 0;
std::vector<int> g_CoreLoad;
int g_CpuTemp = 0;
std::wstring g_CpuName = L"CPU";
int g_CpuClock = 0;
int g_CpuTdp = 0;
std::wstring g_RamTimings = L"";
int g_SelectedCoreIndex = 0;
int g_TargetCoreClock = 0;

std::atomic<int> g_BenchScore = 0;
std::atomic<bool> g_BenchRunning = false;
std::atomic<int> g_BenchProgress = 0;
std::wstring g_BenchMode = L"";

const int B_WIDTH = 4096;
const int B_HEIGHT = 4096;
const int MAX_ITER = 1000;

void BenchmarkWorkerScalar(int startRow, int endRow, std::atomic<long long>* totalIter) {
    long long localIter = 0;
    for (int y = startRow; y < endRow; y++) {
        double ci = (y * (2.0 / B_HEIGHT)) - 1.0;
        for (int x = 0; x < B_WIDTH; x++) {
            double cr = (x * (3.5 / B_WIDTH)) - 2.5;
            double zr = 0.0, zi = 0.0;
            int iter = 0;
            while ((zr * zr + zi * zi) <= 4.0 && iter < MAX_ITER) {
                double temp = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = temp;
                iter++;
            }
            localIter += iter;
        }
        if (y % 10 == 0) g_BenchProgress = (int)((float)y / B_HEIGHT * 100.0f);
    }
    *totalIter += localIter;
}

void BenchmarkWorkerAVX2(int startRow, int endRow, std::atomic<long long>* totalIter) {
    __m256 ymm_const_2 = _mm256_set1_ps(2.0f);
    __m256 ymm_const_4 = _mm256_set1_ps(4.0f);
    __m256 dx = _mm256_set1_ps(3.5f / B_WIDTH);
    __m256 x_offsets = _mm256_set_ps(7.0f, 6.0f, 5.0f, 4.0f, 3.0f, 2.0f, 1.0f, 0.0f);

    long long localIter = 0;
    for (int y = startRow; y < endRow; y++) {
        float fy = (float)y * (2.0f / B_HEIGHT) - 1.0f;
        __m256 y0 = _mm256_set1_ps(fy);
        for (int x = 0; x < B_WIDTH; x += 8) {
            __m256 x0 = _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps((float)x), dx), _mm256_mul_ps(x_offsets, dx));
            x0 = _mm256_sub_ps(x0, _mm256_set1_ps(2.5f));
            __m256 zr = _mm256_setzero_ps();
            __m256 zi = _mm256_setzero_ps();
            __m256 iter_counts = _mm256_setzero_ps();
            __m256 mask_active = _mm256_castsi256_ps(_mm256_set1_epi32(-1));

            for (int i = 0; i < MAX_ITER; i++) {
                __m256 zr2 = _mm256_mul_ps(zr, zr);
                __m256 zi2 = _mm256_mul_ps(zi, zi);
                __m256 dist = _mm256_add_ps(zr2, zi2);
                __m256 mask_div = _mm256_cmp_ps(dist, ymm_const_4, _CMP_LE_OQ);
                if (_mm256_movemask_ps(mask_div) == 0) break;
                mask_active = _mm256_and_ps(mask_active, mask_div);
                iter_counts = _mm256_add_ps(iter_counts, _mm256_and_ps(mask_active, _mm256_set1_ps(1.0f)));
                __m256 temp = _mm256_mul_ps(zr, zi);
                zi = _mm256_add_ps(_mm256_mul_ps(temp, ymm_const_2), y0);
                zr = _mm256_add_ps(_mm256_sub_ps(zr2, zi2), x0);
            }
            float* counts = (float*)&iter_counts;
            for (int k = 0; k < 8; k++) localIter += (long long)counts[k];
        }
        if (y % 10 == 0) g_BenchProgress = (int)((float)y / B_HEIGHT * 100.0f);
    }
    *totalIter += localIter;
}

bool CpuSupportsAVX2() {
    int cpuInfo[4];
    __cpuid(cpuInfo, 0); if (cpuInfo[0] < 7) return false;
    __cpuid(cpuInfo, 7); return (cpuInfo[1] & (1 << 5)) != 0;
}

void StartBenchmark(bool multiCore) {
    if (g_BenchRunning || g_GpuBenchRunning) return;

    std::thread([multiCore]() {
        g_BenchRunning = true;
        g_BenchScore = 0;
        g_BenchProgress = 0;

        bool useAVX = CpuSupportsAVX2();
        g_BenchMode = multiCore ? (useAVX ? L"Multi (AVX)" : L"Multi (Std)")
            : (useAVX ? L"Single (AVX)" : L"Single (Std)");

        std::atomic<long long> totalIterations = 0;
        auto startTime = std::chrono::high_resolution_clock::now();

        int threads = multiCore ? std::thread::hardware_concurrency() : 1;
        int rowsPerThread = B_HEIGHT / threads;

        std::vector<std::thread> pool;
        for (int i = 0; i < threads; i++) {
            int start = i * rowsPerThread;
            int end = (i == threads - 1) ? B_HEIGHT : start + rowsPerThread;
            if (useAVX) pool.emplace_back(BenchmarkWorkerAVX2, start, end, &totalIterations);
            else pool.emplace_back(BenchmarkWorkerScalar, start, end, &totalIterations);
        }

        for (auto& t : pool) t.join();

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;
        double score = (totalIterations / elapsed.count()) / 100000.0;

        g_BenchScore = (int)score;
        g_BenchProgress = 100;
        g_BenchRunning = false;
        }).detach();
}

void CpuStressTask(int) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    float a = 1.1f, b = 2.2f;
    while (g_CpuStress) { for (int i = 0; i < 1000; i++) a = a * b + 0.001f; }
}

void StartCpuStress() {
    g_CpuStress = true;
    for (int i = 0; i < std::thread::hardware_concurrency(); i++) std::thread(CpuStressTask, i).detach();
}

int GetWmiTemp(IWbemServices* pSvc) {
    if (!pSvc) return 0;
    IEnumWbemClassObject* pEnum = NULL;
    HRESULT hres = pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
    if (FAILED(hres) || !pEnum) return 0;

    IWbemClassObject* pObj = NULL; ULONG uRet = 0;
    pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet);
    int kelvin = 0;
    if (uRet) {
        VARIANT v; pObj->Get(L"CurrentTemperature", 0, &v, 0, 0);
        if (v.vt == VT_I4 || v.vt == VT_UI4) kelvin = v.intVal;
        VariantClear(&v); pObj->Release();
    }
    pEnum->Release();

    if (kelvin > 2732) return (kelvin - 2732) / 10;
    return 0;
}

void MonitorCpu() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == 0) {
        wchar_t buf[256]; DWORD sz = sizeof(buf);
        RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)buf, &sz);
        g_CpuName = buf;
        RegCloseKey(hKey);
    }

    // Simulate WMI Init manually or assume it's available via helper
    // For standalone simplicity, we assume GetWmiTemp is called with a local service if needed

    PDH_HQUERY q; PdhOpenQueryW(NULL, 0, &q);
    PDH_HCOUNTER cTotal;
    PdhAddEnglishCounterW(q, L"\\Processor(_Total)\\% Processor Time", 0, &cTotal);

    SYSTEM_INFO sys; GetSystemInfo(&sys);
    int coreCount = sys.dwNumberOfProcessors;
    std::vector<PDH_HCOUNTER> cCores(coreCount);

    for (int i = 0; i < coreCount; i++) {
        std::wstring path = L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time";
        PdhAddEnglishCounterW(q, path.c_str(), 0, &cCores[i]);
    }
    PdhCollectQueryData(q);
    while (g_AppRunning) {
        PdhCollectQueryData(q);
        PDH_FMT_COUNTERVALUE cv;
        PdhGetFormattedCounterValue(cTotal, PDH_FMT_LONG, NULL, &cv);
        int total = (int)cv.longValue;
        std::vector<int> loads;
        for (int i = 0; i < coreCount; i++) {
            PdhGetFormattedCounterValue(cCores[i], PDH_FMT_LONG, NULL, &cv);
            loads.push_back((int)cv.longValue);
        }
        // int temp = GetWmiTemp(...); // Optional fallback
        {
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_CpuUsage = total;
            g_CoreLoad = loads;
            // g_CpuTemp is updated by system.cpp via hardware poll
        }
        Sleep(500);
    }
}

// --- CPU INFO (WMI/PDH) ---
void UpdateCpuInfo() {
    IWbemLocator* pLoc = nullptr;
    IWbemServices* pSvc = nullptr;
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);

    if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
        if (SUCCEEDED(hr)) {
            hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
            if (SUCCEEDED(hr)) {
                // Get CPU Info: Name, Clock Speed, Cores
                IEnumWbemClassObject* pEnum = nullptr;
                hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t(L"SELECT Name, MaxClockSpeed, NumberOfCores, NumberOfLogicalProcessors FROM Win32_Processor"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
                if (SUCCEEDED(hr) && pEnum) {
                    IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
                    if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == S_OK && uRet) {
                        VARIANT vName, vClock, vCores, vThreads;
                        pObj->Get(L"Name", 0, &vName, 0, 0);
                        pObj->Get(L"MaxClockSpeed", 0, &vClock, 0, 0);
                        pObj->Get(L"NumberOfCores", 0, &vCores, 0, 0);
                        pObj->Get(L"NumberOfLogicalProcessors", 0, &vThreads, 0, 0);

                        if (vName.vt == VT_BSTR) g_CpuName = vName.bstrVal;
                        if (vClock.vt == VT_I4) g_CpuClock = vClock.intVal;

                        VariantClear(&vName); VariantClear(&vClock); VariantClear(&vCores); VariantClear(&vThreads);
                        pObj->Release();
                    }
                    pEnum->Release();
                }

                // Get CPU Temp via WMI (may not be available on all systems)
                pEnum = nullptr;
                hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t(L"SELECT CurrentTemperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);
                if (SUCCEEDED(hr) && pEnum) {
                    IWbemClassObject* pObj = nullptr; ULONG uRet = 0;
                    if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uRet) == S_OK && uRet) {
                        VARIANT vTemp;
                        pObj->Get(L"CurrentTemperature", 0, &vTemp, 0, 0);
                        if (vTemp.vt == VT_I4) g_CpuTemp = (vTemp.intVal - 2732) / 10;
                        VariantClear(&vTemp);
                        pObj->Release();
                    }
                    pEnum->Release();
                }
            }
            pSvc->Release();
        }
        pLoc->Release();
        CoUninitialize();
    }
}

// --- RAM TIMINGS (ZenStates.dll) ---
typedef const char* (__stdcall* ZENSTATES_GETRAMTIMINGS)();
void UpdateRamTimings() {
    HMODULE hZen = LoadLibraryW(L"ZenStates.dll");
    if (hZen) {
        ZENSTATES_GETRAMTIMINGS getTimings = (ZENSTATES_GETRAMTIMINGS)GetProcAddress(hZen, "GetRamTimings");
        if (getTimings) {
            const char* timings = getTimings();
            if (timings) {
                int len = MultiByteToWideChar(CP_UTF8, 0, timings, -1, NULL, 0);
                if (len > 0) {
                    wchar_t* wstr = new wchar_t[len];
                    MultiByteToWideChar(CP_UTF8, 0, timings, -1, wstr, len);
                    g_RamTimings = wstr;
                    delete[] wstr;
                }
            }
        }
        FreeLibrary(hZen);
    } else {
        g_RamTimings = L"N/A";
    }
}

// --- CORE CLOCK (ZenStates-core.dll wrapper) ---

// Wrapper functions for ZenStates-core.dll
namespace ZenStatesInterop {
    static HMODULE g_hZenStates = NULL;

    bool LoadZenStatesDll() {
        if (g_hZenStates != NULL)
            return true;

        const wchar_t* searchPaths[] = {
            L"ZenStates-core.dll",
            L".\\ZenStates-core.dll",
            L"..\\ZenStates-core.dll",
        };

        for (const wchar_t* path : searchPaths) {
            g_hZenStates = LoadLibraryW(path);
            if (g_hZenStates != NULL)
                return true;
        }
        return false;
    }
}

bool SetCoreClockNative(int core, int mhz) {
    if (!ZenStatesInterop::LoadZenStatesDll()) {
        OutputDebugStringW(L"✗ Failed to load ZenStates-core.dll");
        return false;
    }

    // Try multiple calling conventions and function names
    typedef int(__stdcall* SETCORECLOCK_STDCALL)(int, int);
    typedef int(__cdecl* SETCORECLOCK_CDECL)(int, int);

    SETCORECLOCK_STDCALL setClockFunc_stdcall = NULL;
    SETCORECLOCK_CDECL setClockFunc_cdecl = NULL;

    const char* functionNames[] = {
        "SetCoreClock",
        "SetP0Clock",
        "ApplyCoreClock",
    };

    // List what functions we find
    OutputDebugStringW(L"\n✓ DLL loaded, searching for SetCoreClock function...");

    for (const char* funcName : functionNames) {
        void* func = GetProcAddress(ZenStatesInterop::g_hZenStates, funcName);
        if (func) {
            wchar_t msg[256];
            swprintf_s(msg, L"✓ Found function: %S", funcName);
            OutputDebugStringW(msg);

            // Try __stdcall first
            setClockFunc_stdcall = (SETCORECLOCK_STDCALL)func;
            try {
                int result = setClockFunc_stdcall(core, mhz);
                wchar_t msg2[256];
                swprintf_s(msg2, L"✓ SetCoreClock returned: %d", result);
                OutputDebugStringW(msg2);
                return (result == 0);
            }
            catch (...) {
                OutputDebugStringW(L"✗ Exception with __stdcall, trying __cdecl...");
            }

            // Try __cdecl
            setClockFunc_cdecl = (SETCORECLOCK_CDECL)func;
            try {
                int result = setClockFunc_cdecl(core, mhz);
                wchar_t msg2[256];
                swprintf_s(msg2, L"✓ SetCoreClock (cdecl) returned: %d", result);
                OutputDebugStringW(msg2);
                return (result == 0);
            }
            catch (...) {
                OutputDebugStringW(L"✗ Exception with __cdecl");
            }
        } else {
            wchar_t msg[256];
            swprintf_s(msg, L"✗ Function not found: %S", funcName);
            OutputDebugStringW(msg);
        }
    }

    OutputDebugStringW(L"✗ No valid function found");
    return false;
}

// Set CPU core clock using ZenStates-Core.dll
bool SetCpuCoreClock(int core, int mhz) {
    if (core < 0 || core > 15) {
        MessageBoxW(NULL, L"Invalid core index (0-15)", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }
    if (mhz < 400 || mhz > 5000) {
        MessageBoxW(NULL, L"Clock speed must be 400-5000 MHz", L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    bool result = SetCoreClockNative(core, mhz);

    if (result) {
        wchar_t msg[256];
        swprintf_s(msg, L"Core %d set to %d MHz", core, mhz);
        MessageBoxW(NULL, msg, L"Success", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(NULL, L"Failed to set core clock. Make sure ZenStates-core.dll is available.", L"Error", MB_OK | MB_ICONERROR);
    }

    return result;
}

// Call these periodically to update CPU and RAM info
void UpdateCpuAndRamInfo() {
    UpdateCpuInfo();
    UpdateRamTimings();
}
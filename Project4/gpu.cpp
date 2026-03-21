#include "shared.hpp"
#include <windows.h>
#include <gl/GL.h>
#include <chrono>
#include <dxgi.h>
#include <mutex>
#ifdef _NVAPI_H_
#include "nvapi.h"
#pragma comment(lib, "nvapi.lib")
#define HAVE_NVAPI 1
#else
#define HAVE_NVAPI 0
#endif
#ifdef _ADLX_H_
#include "ADLX.h"
#pragma comment(lib, "adlx.lib")
#define HAVE_ADLX 1
#else
#define HAVE_ADLX 0
#endif

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "dxgi.lib")

// --- DEFINITIONS ---
std::wstring g_GpuName = L"GPU";
std::atomic<int> g_GpuScore = 0;
std::atomic<bool> g_GpuBenchRunning = false;

unsigned long long g_GpuVramUsed = 0;
unsigned long long g_GpuVramTotal = 0;

// GPU Stats (NVAPI/ADLX)
int g_GpuTemp = 0;
int g_GpuTempAvg = 0;
int g_GpuTempMin = 999;
int g_GpuTempMax = 0;
int g_GpuCoreClock = 0;
int g_GpuCoreClockMin = 999999;
int g_GpuCoreClockMax = 0;
int g_GpuMemClock = 0;
int g_GpuMemClockMin = 999999;
int g_GpuMemClockMax = 0;
int g_GpuVideoClock = 0;
int g_GpuUtilization = 0;
int g_GpuMemUtilization = 0;
double g_GpuPowerDraw = 0.0;
double g_GpuPowerLimit = 0.0;
int g_GpuFanRPM = 0;

// GPU Hardware Info
int g_GpuMemBusWidth = 0;
unsigned long long g_GpuMemUsed = 0;
unsigned long long g_GpuMemReserved = 0;
std::wstring g_GpuMemType = L"Unknown";
std::wstring g_GpuDriverVersion = L"Unknown";
std::string g_GpuPciDeviceId = "N/A";
int g_GpuPcieLanes = 0;

std::mutex g_GpuMutex;

void InitGpuInfo() {
    std::lock_guard<std::mutex> lock(g_GpuMutex);
#if HAVE_NVAPI
    if (NvAPI_Initialize() == NVAPI_OK) {
        NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
        NvU32 count = 0;
        if (NvAPI_EnumPhysicalGPUs(handles, &count) == NVAPI_OK && count > 0) {
            NvPhysicalGpuHandle gpu = handles[0];
            NvAPI_ShortString name = { 0 };
            if (NvAPI_GPU_GetFullName(gpu, name) == NVAPI_OK) {
                g_GpuName = std::wstring(name, name + strlen(name));
            }
            NV_DISPLAY_DRIVER_MEMORY_INFO memInfo = { 0 };
            memInfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;
            if (NvAPI_GPU_GetMemoryInfo(gpu, &memInfo) == NVAPI_OK) {
                g_GpuVramTotal = memInfo.dedicatedVideoMemory;
                g_GpuVramUsed = memInfo.dedicatedVideoMemory - memInfo.availableDedicatedVideoMemory;
            }
            // Optionally: get PCIe, mem type, etc.
            return;
        }
    }
#endif
#if HAVE_ADLX
    // ADLX AMD GPU detection (pseudo-code, requires ADLX SDK)
    adlx::IADLXSystem* pSystem = nullptr;
    if (ADLXHelper_Initialize(&pSystem) == ADLX_OK && pSystem) {
        adlx::IADLXGPUList* pGpuList = nullptr;
        if (pSystem->GetGPUs(&pGpuList) == ADLX_OK && pGpuList) {
            adlx::IADLXGPU* pGpu = nullptr;
            if (pGpuList->At(0, &pGpu) == ADLX_OK && pGpu) {
                adlx::IADLXGPUName* pName = nullptr;
                if (pGpu->Name(&pName) == ADLX_OK && pName) {
                    const wchar_t* wname = nullptr;
                    pName->GetName(&wname);
                    if (wname) g_GpuName = wname;
                    pName->Release();
                }
                adlx::IADLXGPUMemory* pMem = nullptr;
                if (pGpu->Memory(&pMem) == ADLX_OK && pMem) {
                    g_GpuVramTotal = pMem->TotalMemory();
                    g_GpuVramUsed = pMem->UsedMemory();
                    pMem->Release();
                }
                pGpu->Release();
            }
            pGpuList->Release();
        }
        pSystem->Release();
    }
#endif
}

void UpdateGpuVram() {
    IDXGIFactory* f = NULL; CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&f);
    if (f) {
        IDXGIAdapter* a = NULL;
        if (f->EnumAdapters(0, &a) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC d; a->GetDesc(&d);
            std::lock_guard<std::mutex> l(g_StatsMutex);
            g_GpuVramTotal = d.DedicatedVideoMemory;
            a->Release();
        }
        f->Release();
    }
}

HWND CreateHiddenGLWindow(HDC& hDC, HGLRC& hRC) {
    WNDCLASS wc = { 0 }; wc.lpfnWndProc = DefWindowProc; wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"GLBench"; RegisterClass(&wc);
    HWND hWnd = CreateWindow(L"GLBench", L"", WS_POPUP, 0, 0, 100, 100, NULL, NULL, GetModuleHandle(NULL), NULL);
    hDC = GetDC(hWnd);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32, 0,0,0,0,0,0, 0,0,0,0,0,0,0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
    SetPixelFormat(hDC, ChoosePixelFormat(hDC, &pfd), &pfd);
    hRC = wglCreateContext(hDC); wglMakeCurrent(hDC, hRC);
    return hWnd;
}

void GpuBenchWorker() {
    g_GpuBenchRunning = true; g_GpuScore = 0; g_BenchProgress = 0;
    HDC hDC; HGLRC hRC; HWND hWnd = CreateHiddenGLWindow(hDC, hRC);

    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    auto start = std::chrono::high_resolution_clock::now();
    int frames = 0;
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> el = now - start;
        if (el.count() > 5.0) break;
        g_BenchProgress = (int)(el.count() / 5.0 * 100.0);

        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS); glColor4f(0.01f, 0.01f, 0.01f, 0.01f);
        for (int i = 0; i < 1000; i++) { glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); }
        glEnd(); SwapBuffers(hDC); frames++;
    }
    g_GpuScore = frames; g_BenchProgress = 100; g_GpuBenchRunning = false;
    wglMakeCurrent(NULL, NULL); wglDeleteContext(hRC); ReleaseDC(hWnd, hDC); DestroyWindow(hWnd);
}

void StartGpuBenchmark() { if (!g_GpuBenchRunning) std::thread(GpuBenchWorker).detach(); }

void GpuStressWorker() {
    HDC hDC; HGLRC hRC; HWND hWnd = CreateHiddenGLWindow(hDC, hRC);
    while (g_GpuStress && g_AppRunning) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        for (int i = 0; i < 4000; i++) {
            glColor4f((rand() % 10) / 10.f, (rand() % 10) / 10.f, (rand() % 10) / 10.f, 0.1f);
            float x = (rand() % 200) / 100.f - 1.0f; float y = (rand() % 200) / 100.f - 1.0f;
            glVertex2f(x, y); glVertex2f(x + 0.1f, y); glVertex2f(x + 0.1f, y + 0.1f); glVertex2f(x, y + 0.1f);
        }
        glEnd(); SwapBuffers(hDC);
    }
    wglMakeCurrent(NULL, NULL); wglDeleteContext(hRC); ReleaseDC(hWnd, hDC); DestroyWindow(hWnd);
}

void StartGpuStress() {
    g_GpuStress = true;
    std::thread(GpuStressWorker).detach();
}

void UpdateGpuStats() {
    std::lock_guard<std::mutex> lock(g_GpuMutex);
    std::string gpuNameStr;
    bool nvapiSuccess = false;
#if HAVE_NVAPI
    if (NvAPI_Initialize() == NVAPI_OK) {
        NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS] = { 0 };
        NvU32 count = 0;
        if (NvAPI_EnumPhysicalGPUs(handles, &count) == NVAPI_OK && count > 0) {
            NvPhysicalGpuHandle gpu = handles[0];
            NvAPI_ShortString name = { 0 };
            if (NvAPI_GPU_GetFullName(gpu, name) == NVAPI_OK) {
                gpuNameStr = name;
                g_GpuName = std::wstring(name, name + strlen(name));
            }
            NV_DISPLAY_DRIVER_MEMORY_INFO memInfo = { 0 };
            memInfo.version = NV_DISPLAY_DRIVER_MEMORY_INFO_VER;
            if (NvAPI_GPU_GetMemoryInfo(gpu, &memInfo) == NVAPI_OK) {
                g_GpuVramTotal = memInfo.dedicatedVideoMemory;
                g_GpuVramUsed = memInfo.dedicatedVideoMemory - memInfo.availableDedicatedVideoMemory;
            }
            NV_GPU_THERMAL_SETTINGS thermalInfo = { 0 };
            thermalInfo.version = NV_GPU_THERMAL_SETTINGS_VER;
            if (NvAPI_GPU_GetThermalSettings(gpu, NVAPI_THERMAL_TARGET_ALL, &thermalInfo) == NVAPI_OK) {
                int temp = (int)thermalInfo.sensor[0].currentTemp;
                g_GpuTemp = temp;
                if (temp > g_GpuTempMax) g_GpuTempMax = temp;
                if (temp < g_GpuTempMin) g_GpuTempMin = temp;
                g_GpuTempAvg = (g_GpuTempAvg + temp) / 2;
            }
            NV_GPU_CLOCK_FREQUENCIES clockInfo = { 0 };
            clockInfo.version = NV_GPU_CLOCK_FREQUENCIES_VER;
            if (NvAPI_GPU_GetAllClockFrequencies(gpu, &clockInfo) == NVAPI_OK) {
                if (clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].bIsPresent) {
                    int coreClock = clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_GRAPHICS].frequency / 1000;
                    g_GpuCoreClock = coreClock;
                    if (coreClock > g_GpuCoreClockMax) g_GpuCoreClockMax = coreClock;
                    if (coreClock < g_GpuCoreClockMin) g_GpuCoreClockMin = coreClock;
                }
                if (clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].bIsPresent) {
                    int memClock = clockInfo.domain[NVAPI_GPU_PUBLIC_CLOCK_MEMORY].frequency / 1000;
                    g_GpuMemClock = memClock;
                    if (memClock > g_GpuMemClockMax) g_GpuMemClockMax = memClock;
                    if (memClock < g_GpuMemClockMin) g_GpuMemClockMin = memClock;
                }
            }
            NV_GPU_DYNAMIC_PSTATES_INFO_EX pstateInfo = { 0 };
            pstateInfo.version = NV_GPU_DYNAMIC_PSTATES_INFO_EX_VER;
            if (NvAPI_GPU_GetDynamicPstatesInfoEx(gpu, &pstateInfo) == NVAPI_OK) {
                g_GpuUtilization = pstateInfo.utilization[NVAPI_GPU_UTILIZATION_DOMAIN_GPU].percentage;
                g_GpuMemUtilization = pstateInfo.utilization[NVAPI_GPU_UTILIZATION_DOMAIN_FB].percentage;
            }
            NvU32 power = 0;
            if (NvAPI_GPU_GetPowerUsage(gpu, &power) == NVAPI_OK) {
                g_GpuPowerDraw = power / 1000.0;
            }
            NvU32 powerLimit = 0;
            if (NvAPI_GPU_GetPowerManagementLimit(gpu, &powerLimit) == NVAPI_OK) {
                g_GpuPowerLimit = powerLimit / 1000.0;
            }
            NvU32 fanSpeedRPM = 0;
            if (NvAPI_GPU_GetTachReading(gpu, &fanSpeedRPM) == NVAPI_OK) {
                g_GpuFanRPM = fanSpeedRPM;
            }
            nvapiSuccess = true;
        }
    }
#endif
    // Fallback: Use nvidia-smi if NVAPI unavailable or failed
    if (!nvapiSuccess) {
        std::string cmd_str = "nvidia-smi --query-gpu=name,temperature.gpu,utilization.gpu,utilization.memory,power.draw,power.limit,fan.speed,clocks.current.graphics,clocks.current.memory,memory.used,memory.total --format=csv,noheader,nounits -i 0";
        
        // CreateProcessA needs a modifiable string
        char cmdLine[512];
        strcpy_s(cmdLine, sizeof(cmdLine), cmd_str.c_str());

        HANDLE hPipeRead, hPipeWrite;
        SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

        if (CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) {
            STARTUPINFOA si = { 0 };
            si.cb = sizeof(STARTUPINFOA);
            si.hStdOutput = hPipeWrite;
            si.hStdError = hPipeWrite;
            si.dwFlags = STARTF_USESTDHANDLES;

            PROCESS_INFORMATION pi = { 0 };

            if (CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                CloseHandle(hPipeWrite);
                
                // Wait for process to complete BEFORE reading pipe
                WaitForSingleObject(pi.hProcess, INFINITE);

                char buffer[512] = { 0 };
                DWORD bytesRead = 0;
                if (ReadFile(hPipeRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
                    buffer[bytesRead] = '\0';
                    std::stringstream ss(buffer);
                    std::string item;
                    if (std::getline(ss, item, ',')) {
                        // Remove leading/trailing whitespace and stray chars
                        item.erase(0, item.find_first_not_of(" \t\r\n"));
                        item.erase(item.find_last_not_of(" \t\r\n") + 1);
                        gpuNameStr = item;
                        g_GpuName = std::wstring(item.begin(), item.end());
                    }
                    if (std::getline(ss, item, ',')) g_GpuTemp = std::stoi(item);
                    if (std::getline(ss, item, ',')) g_GpuUtilization = std::stoi(item);
                    if (std::getline(ss, item, ',')) g_GpuMemUtilization = std::stoi(item);
                    if (std::getline(ss, item, ',')) g_GpuPowerDraw = std::stod(item);
                    if (std::getline(ss, item, ',')) g_GpuPowerLimit = std::stod(item);
                    if (std::getline(ss, item, ',')) g_GpuFanRPM = (std::stoi(item) * 3000) / 100;
                    if (std::getline(ss, item, ',')) g_GpuCoreClock = std::stoi(item);
                    if (std::getline(ss, item, ',')) g_GpuMemClock = std::stoi(item);
                    if (std::getline(ss, item, ',')) g_GpuVramUsed = std::stoull(item) * 1024 * 1024;
                    if (std::getline(ss, item, ',')) g_GpuVramTotal = std::stoull(item) * 1024 * 1024;
                }

                CloseHandle(hPipeRead);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            } else {
                std::cout << "[nvidia-smi] Failed to run nvidia-smi" << std::endl;
            }
        }
    }
    // AMD GPU (ADLX)
#if HAVE_ADLX
    if (gpuNameStr.find("AMD") == 0 || gpuNameStr.find("Radeon") == 0) {
        adlx::IADLXSystem* pSystem = nullptr;
        if (ADLXHelper_Initialize(&pSystem) == ADLX_OK && pSystem) {
            adlx::IADLXGPUList* pGpuList = nullptr;
            if (pSystem->GetGPUs(&pGpuList) == ADLX_OK && pGpuList) {
                adlx::IADLXGPU* pGpu = nullptr;
                if (pGpuList->At(0, &pGpu) == ADLX_OK && pGpu) {
                    adlx::IADLXGPUName* pName = nullptr;
                    if (pGpu->Name(&pName) == ADLX_OK && pName) {
                        const wchar_t* wname = nullptr;
                        pName->GetName(&wname);
                        if (wname) g_GpuName = wname;
                        pName->Release();
                    }
                    adlx::IADLXGPUMemory* pMem = nullptr;
                    if (pGpu->Memory(&pMem) == ADLX_OK && pMem) {
                        g_GpuVramTotal = pMem->TotalMemory();
                        g_GpuVramUsed = pMem->UsedMemory();
                        pMem->Release();
                    }
                    adlx::IADLXGPUThermal* pThermal = nullptr;
                    if (pGpu->Thermal(&pThermal) == ADLX_OK && pThermal) {
                        g_GpuTemp = pThermal->Temperature();
                        if (g_GpuTemp > g_GpuTempMax) g_GpuTempMax = g_GpuTemp;
                        if (g_GpuTemp < g_GpuTempMin) g_GpuTempMin = g_GpuTemp;
                        g_GpuTempAvg = (g_GpuTempAvg + g_GpuTemp) / 2;
                        pThermal->Release();
                    }
                    adlx::IADLXGPUClocks* pClocks = nullptr;
                    if (pGpu->Clocks(&pClocks) == ADLX_OK && pClocks) {
                        g_GpuCoreClock = pClocks->CoreClock();
                        g_GpuMemClock = pClocks->MemoryClock();
                        pClocks->Release();
                    }
                    adlx::IADLXGPUUtilization* pUtil = nullptr;
                    if (pGpu->Utilization(&pUtil) == ADLX_OK && pUtil) {
                        g_GpuUtilization = pUtil->GPUUtilization();
                        g_GpuMemUtilization = pUtil->MemoryUtilization();
                        pUtil->Release();
                    }
                    adlx::IADLXGPUPower* pPower = nullptr;
                    if (pGpu->Power(&pPower) == ADLX_OK && pPower) {
                        g_GpuPowerDraw = pPower->PowerDraw();
                        g_GpuPowerLimit = pPower->PowerLimit();
                        pPower->Release();
                    }
                    adlx::IADLXGPUFan* pFan = nullptr;
                    if (pGpu->Fan(&pFan) == ADLX_OK && pFan) {
                        g_GpuFanRPM = pFan->FanSpeedRPM();
                        pFan->Release();
                    }
                    pGpu->Release();
                }
                pGpuList->Release();
            }
            pSystem->Release();
        }
    }
#endif
}

// Provide compatibility wrappers for legacy Nvidia functions
void InitNvidiaGpu() { InitGpuInfo(); }
void MonitorGpu() { UpdateGpuStats(); }
void UpdateNvidiaStats() { UpdateGpuStats(); }
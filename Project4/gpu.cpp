#include "shared.hpp"
#include <gl/GL.h>
#include <chrono>
#include <dxgi.h>

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "dxgi.lib")

// --- DEFINITIONS ---
std::wstring g_GpuName = L"GPU";
std::atomic<int> g_GpuScore = 0;
std::atomic<bool> g_GpuBenchRunning = false;

unsigned long long g_GpuVramUsed = 0;
unsigned long long g_GpuVramTotal = 0;

// GPU Stats (NVAPI)
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

void InitGpuInfo() {
    std::string command = "nvidia-smi --query-gpu=name --format=csv,noheader -i 0";
    std::string output;
    
    HANDLE hPipeRead, hPipeWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    
    if (CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) {
        STARTUPINFOA si = { 0 };
        si.cb = sizeof(STARTUPINFOA);
        si.hStdOutput = hPipeWrite;
        si.hStdError = hPipeWrite;
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi = { 0 };
        
        if (CreateProcessA(NULL, (LPSTR)command.c_str(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hPipeWrite);
            
            char buffer[256] = { 0 };
            DWORD bytesRead = 0;
            if (ReadFile(hPipeRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
                std::string gpuName(buffer);
                // Remove trailing whitespace/newlines
                gpuName.erase(gpuName.find_last_not_of(" \n\r\t") + 1);
                
                if (!gpuName.empty()) {
                    std::lock_guard<std::mutex> l(g_StatsMutex);
                    int len = MultiByteToWideChar(CP_UTF8, 0, gpuName.c_str(), -1, NULL, 0);
                    if (len > 0) {
                        wchar_t* wstr = new wchar_t[len];
                        MultiByteToWideChar(CP_UTF8, 0, gpuName.c_str(), -1, wstr, len);
                        g_GpuName = wstr;
                        delete[] wstr;
                    }
                }
            }
            
            CloseHandle(hPipeRead);
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            CloseHandle(hPipeRead);
            CloseHandle(hPipeWrite);
        }
    }
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
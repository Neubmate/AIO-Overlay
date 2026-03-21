#include "shared.hpp"
#include <gdiplus.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <windowsx.h>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <regex>
#include <dxgi1_4.h>
#include <powrprof.h>
#include <Pdh.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

constexpr int UI_WIDTH_NORMAL = 550;
constexpr int UI_WIDTH_MINI = 220;
constexpr int CORNER_RADIUS = 18;
constexpr int BTN_HEIGHT = 30;

struct AppConfig {
    bool showCpu = true;
    bool showCores = true;
    bool showRam = true;
    bool showRamDetail = true;
    bool showGpu = true;
    bool showDrives = true;
    bool showDiskIo = true;
    bool showBios = true;
    bool showUptime = true;
    bool showBattery = true;
    bool enableLogging = false;
    bool miniMode = false;
    int opacity = 230;
    int xOffset = 30;
    int yOffset = 30;
};

AppConfig g_Cfg;
std::atomic<bool> g_AppRunning(true);
std::atomic<bool> g_CpuStress(false);
std::atomic<bool> g_RamStress(false);
std::atomic<bool> g_GpuStress(false);
std::mutex g_StatsMutex;
HWND g_hOverlay = NULL;
HWND g_hSettings = NULL;

RECT g_RectMultiCore = { 0 }, g_RectSingleCore = { 0 }, g_RectGpuTest = { 0 };
RECT g_RectCpuBurn = { 0 }, g_RectRamBurn = { 0 }, g_RectGpuBurn = { 0 };
RECT g_RectFanControl = { 0 };

// --- CORE CLOCK INPUT STATE ---
bool g_EditingCoreClk = false;
int g_SelectedCore = 0;
std::wstring g_CoreClkInput = L"";
RECT g_RectCoreClkInput = { 0 };

void SaveSettings() {
}

void LoadSettings() {
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE) ShowWindow(hwnd, SW_HIDE);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OpenSettingsWindow(HINSTANCE hInst) {
}

void DrawRoundedRect(Gdiplus::Graphics* g, Gdiplus::Brush* fillBrush, Gdiplus::Pen* borderPen, int x, int y, int w, int h, int r) {
    Gdiplus::GraphicsPath path; path.AddArc(x, y, r, r, 180, 90); path.AddArc(x + w - r, y, r, r, 270, 90);
    path.AddArc(x + w - r, y + h - r, r, r, 0, 90); path.AddArc(x, y + h - r, r, r, 90, 90); path.CloseFigure();
    if (fillBrush) g->FillPath(fillBrush, &path); if (borderPen) g->DrawPath(borderPen, &path);
}

void DrawPillBar(Gdiplus::Graphics* g, float x, float y, float w, float h, float pct, Gdiplus::Brush* bFill, Gdiplus::Brush* bBg) {
    DrawRoundedRect(g, bBg, NULL, (int)x, (int)y, (int)w, (int)h, (int)h);
    float fillW = w * pct; if (fillW < h) fillW = h; if (fillW > w) fillW = w;
    DrawRoundedRect(g, bFill, NULL, (int)x, (int)y, (int)fillW, (int)h, (int)h);
}

void DrawStr(Gdiplus::Graphics* g, const WCHAR* s, Gdiplus::Font* f, float x, float y, Gdiplus::Brush* b) {
    g->DrawString(s, -1, f, Gdiplus::PointF(x, y), b);
}

void DrawButton(Gdiplus::Graphics* g, const WCHAR* s, float x, float y, float w, float h, bool active, Gdiplus::Font* f) {
    Gdiplus::SolidBrush bActive(Gdiplus::Color(200, 255, 69, 58));
    Gdiplus::SolidBrush bIdle(Gdiplus::Color(80, 80, 80, 80));
    Gdiplus::SolidBrush bText(Gdiplus::Color(255, 255, 255, 255));
    DrawRoundedRect(g, active ? &bActive : &bIdle, NULL, (int)x, (int)y, (int)w, (int)h, 10);
    Gdiplus::RectF rect(x, y + 5, w, h);
    Gdiplus::StringFormat format; format.SetAlignment(Gdiplus::StringAlignmentCenter);
    g->DrawString(s, -1, f, rect, &format, &bText);
}

void DrawAppleUI(Gdiplus::Graphics* g, int w, int h) {
    Gdiplus::SolidBrush bBg(Gdiplus::Color(g_Cfg.opacity, 20, 20, 22));
    Gdiplus::Pen pBorder(Gdiplus::Color(50, 255, 255, 255), 1);
    DrawRoundedRect(g, &bBg, &pBorder, 0, 0, w, h, CORNER_RADIUS);

    Gdiplus::SolidBrush bWhite(Gdiplus::Color(255, 255, 255, 255));
    Gdiplus::SolidBrush bGray(Gdiplus::Color(150, 142, 142, 147));
    Gdiplus::SolidBrush bBlue(Gdiplus::Color(255, 10, 132, 255));
    Gdiplus::SolidBrush bRed(Gdiplus::Color(255, 255, 69, 58));
    Gdiplus::SolidBrush bYellow(Gdiplus::Color(255, 255, 204, 0));
    Gdiplus::SolidBrush bGreen(Gdiplus::Color(255, 46, 204, 113));
    Gdiplus::SolidBrush bTrack(Gdiplus::Color(50, 255, 255, 255));

    Gdiplus::Font fHeader(L"Segoe UI", 11, Gdiplus::FontStyleBold);
    Gdiplus::Font fBody(L"Segoe UI", 9, Gdiplus::FontStyleRegular);
    Gdiplus::Font fSmall(L"Segoe UI", 8, Gdiplus::FontStyleRegular);

    DrawRoundedRect(g, &bRed, NULL, 20, 15, 14, 14, 7);
    DrawRoundedRect(g, &bYellow, NULL, 40, 15, 14, 14, 7);

    if (g_Cfg.miniMode) {
        std::lock_guard<std::mutex> l(g_StatsMutex);
        wchar_t buf[128]; swprintf_s(buf, L"CPU %d%%", g_CpuUsage);
        DrawStr(g, buf, &fHeader, 90, 12, &bWhite);
        swprintf_s(buf, L"%d\u00B0C", g_CpuTemp); DrawStr(g, buf, &fBody, 160, 14, &bGray);
        return;
    }

    float y = 40.0f; float x = 25.0f; float contentW = w - 50.0f;
    wchar_t buf[512];

    if (g_Cfg.showCpu) {
        // CPU name: prefer SmuBridge name (more detailed), fall back to WMI name
        DrawStr(g, g_SmuReady && !g_SmuCpuName.empty() ? g_SmuCpuName.c_str() : g_CpuName.c_str(), &fBody, x, y, &bWhite); y += 18.0f;
        DrawPillBar(g, x, y, contentW, 8, g_CpuUsage / 100.0f, (g_CpuTemp > 85) ? &bRed : &bBlue, &bTrack); y += 12.0f;
        swprintf_s(buf, L"%d%% Load  \u2022  %d\u00B0C Temp  \u2022  %d Thr", g_CpuUsage, g_CpuTemp, g_GlobalThreads);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 16.0f;

        // Prochot + SMU temp
        if (g_SmuReady) {
            if (g_SmuProchot) {
                Gdiplus::SolidBrush bHot(Gdiplus::Color(255, 255, 69, 58));
                DrawStr(g, L"\u26A0 PROCHOT: THROTTLING", &fSmall, x, y, &bHot);
            } else {
                Gdiplus::SolidBrush bOk(Gdiplus::Color(255, 46, 204, 113));
                DrawStr(g, L"\u2714 PROCHOT: OK", &fSmall, x, y, &bOk);
            }
            if (!g_SmuTemp.empty()) {
                swprintf_s(buf, L"  \u2022  %s\u00B0C", g_SmuTemp.c_str());
                DrawStr(g, buf, &fSmall, x + 140, y, &bGray);
            }
        } else {
            DrawStr(g, L"PROCHOT: initializing...", &fSmall, x, y, &bGray);
        }
        y += 14.0f;

        // Config + board + SMU version
        if (g_SmuReady) {
            if (!g_SmuConfig.empty())  { DrawStr(g, g_SmuConfig.c_str(),  &fSmall, x, y, &bGray); y += 12.0f; }
            if (!g_SmuMobo.empty())    { DrawStr(g, g_SmuMobo.c_str(),    &fSmall, x, y, &bGray); y += 12.0f; }
            if (!g_SmuVersion.empty()) {
                swprintf_s(buf, L"SMU %s  \u2022  Patch %s", g_SmuVersion.c_str(), g_SmuPatch.c_str());
                DrawStr(g, buf, &fSmall, x, y, &bGray); y += 12.0f;
            }
            if (!g_SmuAcfResult.empty()) {
                Gdiplus::SolidBrush bAcf(g_SmuAcfResult.find(L"OK") != std::wstring::npos
                    ? Gdiplus::Color(255, 46, 204, 113) : Gdiplus::Color(255, 255, 69, 58));
                DrawStr(g, g_SmuAcfResult.c_str(), &fSmall, x, y, &bAcf); y += 12.0f;
            }
        }
        y += 6.0f;
    }

    if (g_Cfg.showCores && !g_CoreLoad.empty()) {
        float startX = x; int col = 0; int maxCols = 4;
        for (size_t i = 0; i < g_CoreLoad.size(); i++) {
            float coreX = startX + (col * (contentW / maxCols));
            float pct = g_CoreLoad[i] / 100.0f;
            DrawPillBar(g, coreX, y + 4, (contentW / maxCols) - 10, 4, pct, (pct > 0.8f) ? &bRed : &bBlue, &bTrack);
            col++; if (col >= maxCols) { col = 0; y += 8.0f; }
        }
        if (col != 0) y += 8.0f; y += 10.0f;
    }

    // All-core frequency control
    {
        DrawStr(g, L"All-Core Frequency", &fBody, x, y, &bWhite); y += 18.0f;

        // Input box
        g_RectCoreClkInput = { (int)x, (int)y, (int)(x + contentW), (int)(y + 28) };
        Gdiplus::SolidBrush bInputBg(g_EditingCoreClk
            ? Gdiplus::Color(160, 0, 80, 160)
            : Gdiplus::Color(80, 50, 50, 60));
        Gdiplus::Pen pInputBorder(g_EditingCoreClk
            ? Gdiplus::Color(200, 10, 132, 255)
            : Gdiplus::Color(60, 255, 255, 255), 1);
        DrawRoundedRect(g, &bInputBg, &pInputBorder,
            g_RectCoreClkInput.left, g_RectCoreClkInput.top,
            (int)contentW, 28, 6);

        if (g_EditingCoreClk)
            swprintf_s(buf, L"  %s_  (550-7000 MHz, x25)", g_CoreClkInput.c_str());
        else
            swprintf_s(buf, L"  Click here, type MHz, press Enter");

        Gdiplus::RectF inputRect((float)g_RectCoreClkInput.left,
                                  (float)g_RectCoreClkInput.top + 6,
                                  contentW, 20);
        Gdiplus::StringFormat fmt;
        g->DrawString(buf, -1, &fSmall, inputRect, &fmt, &bWhite);
        y += 34.0f;

        // Last ACF result
        if (!g_SmuAcfResult.empty()) {
            Gdiplus::SolidBrush bAcf(g_SmuAcfResult.find(L"OK") != std::wstring::npos
                ? Gdiplus::Color(255, 46, 204, 113)
                : Gdiplus::Color(255, 255, 69, 58));
            DrawStr(g, g_SmuAcfResult.c_str(), &fSmall, x, y, &bAcf); y += 14.0f;
        }
        y += 4.0f;
    }

    if (g_Cfg.showGpu) {
        DrawStr(g, g_GpuName.c_str(), &fBody, x, y, &bWhite); y += 18.0f;

        // GPU Temperature
        DrawPillBar(g, x, y, contentW, 8, g_GpuTemp / 100.0f, (g_GpuTemp > 85) ? &bRed : &bBlue, &bTrack); y += 12.0f;
        swprintf_s(buf, L"%d°C Temp  •  %d%% GPU Util  •  %d%% Mem Util", g_GpuTemp, g_GpuUtilization, g_GpuMemUtilization);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 20.0f;

        // Clock Information
        swprintf_s(buf, 512, L"Core: %d MHz (Min: %d Max: %d)  •  Mem: %d MHz", 
                   g_GpuCoreClock, 
                   g_GpuCoreClockMin == 999999 ? 0 : g_GpuCoreClockMin, 
                   g_GpuCoreClockMax);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 14.0f;

        swprintf_s(buf, 512, L"Memory: %d MHz (Min: %d Max: %d)", 
                   g_GpuMemClock, 
                   g_GpuMemClockMin == 999999 ? 0 : g_GpuMemClockMin, 
                   g_GpuMemClockMax);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 14.0f;

        // Power and Fan
        Gdiplus::SolidBrush bPowerColor(g_GpuPowerDraw > (g_GpuPowerLimit * 0.9) ? Gdiplus::Color(255, 255, 204, 0) : 
                                        Gdiplus::Color(255, 46, 204, 113));
        swprintf_s(buf, L"Power: %.1fW / %.1fW  •  Fan: %d RPM", g_GpuPowerDraw, g_GpuPowerLimit, g_GpuFanRPM);
        DrawStr(g, buf, &fSmall, x, y, &bPowerColor); y += 14.0f;

        // VRAM Info (simplified)
        double vramUsedGB = g_GpuVramUsed / (1024.0 * 1024.0 * 1024.0);
        double vramTotalGB = g_GpuVramTotal / (1024.0 * 1024.0 * 1024.0);
        float vramPct = vramTotalGB > 0 ? (vramUsedGB / vramTotalGB) : 0.0f;
        swprintf_s(buf, L"VRAM: %.1f/%.1f GB", vramUsedGB, vramTotalGB);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 14.0f;
    }

    if (g_Cfg.showDrives) {
        DrawStr(g, L"Storage", &fBody, x, y, &bWhite); y += 18.0f;
        for (const auto& drive : g_DriveInfo) {
            DrawStr(g, drive.c_str(), &fSmall, x, y, &bGray); y += 12.0f;
        }
        y += 8.0f;
    }

    if (g_Cfg.showRam) {
        DrawStr(g, L"Memory", &fBody, x, y, &bWhite); y += 18.0f;
        DrawPillBar(g, x, y, contentW, 8, g_RamLoad / 100.0f, (g_RamLoad > 80) ? &bRed : &bGreen, &bTrack); y += 12.0f;
        swprintf_s(buf, L"%s  •  %d%% Load", g_RamText.c_str(), g_RamLoad);
        DrawStr(g, buf, &fSmall, x, y, &bGray); y += 20.0f;
    }

    if (g_Cfg.showBattery && g_HasBattery) {
        DrawStr(g, L"Battery", &fBody, x, y, &bWhite); y += 18.0f;
        DrawPillBar(g, x, y, contentW, 8, g_BatteryPct / 100.0f, g_BatteryPct < 20 ? &bRed : &bGreen, &bTrack);
        swprintf_s(buf, L"%d%% (%s)", g_BatteryPct, g_BatteryTime.c_str());
        y += 12.0f; DrawStr(g, buf, &fSmall, x, y, &bGray); y += 20.0f;
    }

    y += 10.0f;
    DrawStr(g, L"Benchmarks", &fBody, x, y, &bWhite); y += 20.0f;

    if (g_BenchRunning) {
        swprintf_s(buf, L"Running CPU Test (%d%%)", g_BenchProgress.load());
        DrawStr(g, buf, &fSmall, x, y, &bYellow);
        DrawPillBar(g, x, y + 15, contentW, 6, g_BenchProgress / 100.0f, &bYellow, &bTrack);
    }
    else if (g_GpuBenchRunning) {
        swprintf_s(buf, L"Running GPU Test (%d%%)", g_BenchProgress.load());
        DrawStr(g, buf, &fSmall, x, y, &bYellow);
        DrawPillBar(g, x, y + 15, contentW, 6, g_BenchProgress / 100.0f, &bYellow, &bTrack);
    }
    else {
        if (g_BenchScore > 0) { swprintf_s(buf, L"CPU Score: %d pts", g_BenchScore.load()); DrawStr(g, buf, &fHeader, x, y, &bGreen); }
        if (g_GpuScore > 0) { swprintf_s(buf, L"GPU Score: %d pts", g_GpuScore.load()); DrawStr(g, buf, &fHeader, x + 150, y, &bBlue); }
        if (g_BenchScore == 0 && g_GpuScore == 0) DrawStr(g, L"Ready to Test", &fSmall, x, y, &bGray);
    }
    y += 30.0f;

    float btnW = (contentW - 15) / 3;
    DrawButton(g, L"Multi Core", x, y, btnW, BTN_HEIGHT, g_BenchRunning && g_BenchMode.find(L"Multi") != std::wstring::npos, &fBody);
    g_RectMultiCore = { (long)x, (long)y, (long)(x + btnW), (long)(y + BTN_HEIGHT) };

    DrawButton(g, L"Single Core", x + btnW + 5, y, btnW, BTN_HEIGHT, g_BenchRunning && g_BenchMode.find(L"Single") != std::wstring::npos, &fBody);
    g_RectSingleCore = { (long)(x + btnW + 5), (long)y, (long)(x + btnW + 5 + btnW), (long)(y + BTN_HEIGHT) };

    DrawButton(g, L"GPU Test", x + (btnW * 2) + 10, y, btnW, BTN_HEIGHT, g_GpuBenchRunning, &fBody);
    g_RectGpuTest = { (long)(x + (btnW * 2) + 10), (long)y, (long)(x + (btnW * 2) + 10 + btnW), (long)(y + BTN_HEIGHT) };

    y += 45.0f;
    float stressW = (contentW - 20) / 3;
    DrawButton(g, L"CPU BURN", x, y, stressW, BTN_HEIGHT, g_CpuStress, &fSmall);
    g_RectCpuBurn = { (long)x, (long)y, (long)(x + stressW), (long)(y + BTN_HEIGHT) };

    DrawButton(g, L"RAM BURN", x + stressW + 10, y, stressW, BTN_HEIGHT, g_RamStress, &fSmall);
    g_RectRamBurn = { (long)(x + stressW + 10), (long)y, (long)(x + stressW + 10 + stressW), (long)(y + BTN_HEIGHT) };

    DrawButton(g, L"GPU BURN", x + (stressW * 2) + 20, y, stressW, BTN_HEIGHT, g_GpuStress, &fSmall);
    g_RectGpuBurn = { (long)(x + (stressW * 2) + 20), (long)y, (long)(x + (stressW * 2) + 20 + stressW), (long)(y + BTN_HEIGHT) };
}

// Input handler for custom CPU core clocks
void HandleSetCoreClockInput(int core, int mhz) {
    if (SmuSetAllCoreFrequency(mhz)) {
        MessageBoxW(g_hOverlay, L"All-core frequency set successfully!", L"ZenStates", MB_OK);
    } else {
        MessageBoxW(g_hOverlay, L"Failed to set frequency. Run as Administrator.", L"ZenStates", MB_OK);
    }
}

bool IsPointInRect(int x, int y, RECT r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam); int y = GET_Y_LPARAM(lParam);

        if (y < 40) {
            if (x >= 20 && x <= 34) { PostQuitMessage(0); return 0; }
            if (x >= 40 && x <= 54) { g_Cfg.miniMode = !g_Cfg.miniMode; SaveSettings(); return 0; }
        }

        if (g_Cfg.miniMode) {
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0;
        }

        if (IsPointInRect(x, y, g_RectMultiCore)) { StartBenchmark(true); return 0; }
        if (IsPointInRect(x, y, g_RectSingleCore)) { StartBenchmark(false); return 0; }
        if (IsPointInRect(x, y, g_RectGpuTest)) { StartGpuBenchmark(); return 0; }

        if (IsPointInRect(x, y, g_RectCpuBurn)) { g_CpuStress = !g_CpuStress; if (g_CpuStress) StartCpuStress(); return 0; }
        if (IsPointInRect(x, y, g_RectRamBurn)) { g_RamStress = !g_RamStress; if (g_RamStress) StartRamStress(); return 0; }
        if (IsPointInRect(x, y, g_RectGpuBurn)) { g_GpuStress = !g_GpuStress; if (g_GpuStress) StartGpuStress(); return 0; }

        // All-core frequency input box
        if (IsPointInRect(x, y, g_RectCoreClkInput)) {
            g_EditingCoreClk = true;
            g_CoreClkInput = L"";
            SetForegroundWindow(hwnd);
            SetFocus(hwnd);
            return 0;
        }

        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0;
    }
    case WM_KEYDOWN: {
        if (g_EditingCoreClk) {
            if (wParam == VK_RETURN) {
                if (!g_CoreClkInput.empty()) {
                    int mhz = _wtoi(g_CoreClkInput.c_str());
                    if (mhz >= 550 && mhz <= 7000 && mhz % 25 == 0) {
                        int freqCopy = mhz;
                        std::thread([freqCopy]() { SmuSetAllCoreFrequency(freqCopy); }).detach();
                        g_CoreClkInput = L"";
                        g_EditingCoreClk = false;
                    } else {
                        g_CoreClkInput = L"INVALID";
                    }
                }
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                g_EditingCoreClk = false;
                g_CoreClkInput = L"";
                return 0;
            }
        } else {
            // Core selection (0-7)
            int coreCount = (int)g_CoreLoad.size();
            if (coreCount > 8) coreCount = 8;
            if (wParam >= '0' && wParam <= '7') {
                int coreIdx = wParam - '0';
                if (coreIdx < coreCount) {
                    g_SelectedCore = coreIdx;
                    return 0;
                }
            }
        }
        return 0;
    }
    case WM_CHAR: {
        if (g_EditingCoreClk) {
            if (isdigit((char)wParam)) {
                if (g_CoreClkInput.length() < 5) {
                    g_CoreClkInput += (wchar_t)wParam;
                }
            } else if (wParam == VK_BACK && !g_CoreClkInput.empty()) {
                g_CoreClkInput.pop_back();
            }
            return 0;
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ... (Rest of main function remains the same as previous) ...
int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);
    LoadSettings();
    InitDiskPdh();
    InitGpuInfo();
    InitNvidiaGpu();

    std::thread(MonitorCpu).detach();
    std::thread(MonitorSystem).detach();
    std::thread([]() { while (g_AppRunning) { UpdateCpuAndRamInfo(); std::this_thread::sleep_for(std::chrono::milliseconds(2000)); } }).detach();
    std::thread([]() { GetDetailedRamInfo(); }).detach();
    std::thread(CheckStorage).detach();
    std::thread([]() { while (g_AppRunning) { MonitorGpu(); UpdateNvidiaStats(); std::this_thread::sleep_for(std::chrono::milliseconds(1000)); } }).detach();
    std::thread([]() { InitSmuBridge(); }).detach();

    Gdiplus::GdiplusStartupInput gsi; ULONG_PTR tok; Gdiplus::GdiplusStartup(&tok, &gsi, NULL);
    int w = UI_WIDTH_NORMAL; int h = 850; int x = GetSystemMetrics(SM_CXSCREEN) - w - g_Cfg.xOffset;
    WNDCLASSW wc = { 0 }; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandle(NULL); wc.lpszClassName = L"AppleOverlay"; wc.hCursor = LoadCursor(NULL, IDC_ARROW); RegisterClassW(&wc);
    g_hOverlay = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"AppleOverlay", L"", WS_POPUP | WS_VISIBLE, x, g_Cfg.yOffset, w, h, 0, 0, wc.hInstance, 0);
    HDC sc = GetDC(NULL); HDC memDC = CreateCompatibleDC(sc); HBITMAP memBM = CreateCompatibleBitmap(sc, w, h); SelectObject(memDC, memBM);
    Gdiplus::Graphics g(memDC); g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    while (g_AppRunning) {
        MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) g_AppRunning = false; }
        if (GetAsyncKeyState(VK_END) & 0x8000) break;

        MEMORYSTATUSEX m; m.dwLength = sizeof(m); GlobalMemoryStatusEx(&m);
        {
            std::lock_guard<std::mutex> l(g_StatsMutex); g_RamLoad = m.dwMemoryLoad;
            double usedGB = (m.ullTotalPhys - m.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
            double totalGB = m.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
            wchar_t buf[64]; swprintf_s(buf, L"%.1f/%.1f GB", usedGB, totalGB); g_RamText = buf;
        }
        UpdateDiskIo(); UpdateGpuVram(); UpdateBattery();

        int curW = g_Cfg.miniMode ? UI_WIDTH_MINI : UI_WIDTH_NORMAL; int curH = g_Cfg.miniMode ? 70 : 850;
        g.Clear(Gdiplus::Color(0, 0, 0, 0)); DrawAppleUI(&g, curW, curH);
         POINT pSrc = { 0,0 }; SIZE sSize = { curW, curH }; POINT pPos = { x, g_Cfg.yOffset };
         BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
         UpdateLayeredWindow(g_hOverlay, sc, &pPos, &sSize, memDC, &pSrc, 0, &bf, ULW_ALPHA);
         Sleep(30);
     }
     DeleteObject(memBM); DeleteDC(memDC); ReleaseDC(NULL, sc); Gdiplus::GdiplusShutdown(tok); return 0;
}
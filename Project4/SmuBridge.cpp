// SmuBridge.cpp
// Extracts the embedded SMUDebugTool.exe + ZenStates-Core.dll from resources,
// calls the exe via CreateProcess, pipes stdout back, and parses the output
// into the overlay globals declared in Shared.hpp.
//
// NO CLR, NO WRAPPER — pure native Win32.

#include "Shared.hpp"
#include "resource.h"
#include <windows.h>
#include <string>
#include <mutex>
#include <sstream>

// ---------------------------------------------------------------------------
// SMU info globals  (read by DrawAppleUI)
// ---------------------------------------------------------------------------
std::wstring g_SmuCpuName;
std::wstring g_SmuCpuId;
std::wstring g_SmuModel;
std::wstring g_SmuPackage;
std::wstring g_SmuConfig;
std::wstring g_SmuMobo;
std::wstring g_SmuBios;
std::wstring g_SmuVersion;
std::wstring g_SmuPatch;
std::wstring g_SmuTemp;
std::wstring g_SmuAcfResult;
bool         g_SmuReady   = false;
bool         g_SmuProchot = false;
std::mutex   g_SmuMutex;

// Temp folder where we extract the tool
static std::wstring g_SmuTempDir;

// ---------------------------------------------------------------------------
// Helper: extract a Win32 RCDATA resource to a file
// ---------------------------------------------------------------------------
static bool ExtractResource(int resourceId, const std::wstring& destPath)
{
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return false;
    void* pData = LockResource(hData);
    DWORD size  = SizeofResource(NULL, hRes);
    if (!pData || size == 0) return false;

    HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(hFile, pData, size, &written, NULL);
    CloseHandle(hFile);
    return written == size;
}

// ---------------------------------------------------------------------------
// Find SMU tool: first check exe directory (PostBuildEvent copies there),
// then fall back to extracting from resources to %TEMP%\SmuBridge\
// ---------------------------------------------------------------------------
static bool ExtractSmuTool()
{
    // 1) Try exe directory first (PostBuildEvent copies all 3 files there)
    wchar_t exeDir[MAX_PATH];
    GetModuleFileNameW(NULL, exeDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exeDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    std::wstring localDir = exeDir;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    bool localExe = GetFileAttributesExW((localDir + L"SMUDebugTool.exe").c_str(), GetFileExInfoStandard, &fa) != 0;
    bool localDll = GetFileAttributesExW((localDir + L"ZenStates-Core.dll").c_str(), GetFileExInfoStandard, &fa) != 0;
    bool localInp = GetFileAttributesExW((localDir + L"inpoutx64.dll").c_str(), GetFileExInfoStandard, &fa) != 0;

    if (localExe && localDll && localInp)
    {
        g_SmuTempDir = localDir;
        return true;
    }

    // 2) Fall back: extract from resources to %TEMP%\SmuBridge\ + copy inpoutx64
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    g_SmuTempDir = std::wstring(tmp) + L"SmuBridge\\";
    CreateDirectoryW(g_SmuTempDir.c_str(), NULL);

    std::wstring exePath = g_SmuTempDir + L"SMUDebugTool.exe";
    std::wstring dllPath = g_SmuTempDir + L"ZenStates-Core.dll";
    std::wstring inpPath = g_SmuTempDir + L"inpoutx64.dll";

    bool exeOk = GetFileAttributesExW(exePath.c_str(), GetFileExInfoStandard, &fa) != 0;
    bool dllOk = GetFileAttributesExW(dllPath.c_str(), GetFileExInfoStandard, &fa) != 0;
    bool inpOk = GetFileAttributesExW(inpPath.c_str(), GetFileExInfoStandard, &fa) != 0;

    if (!exeOk && !ExtractResource(IDR_SMUTOOL_EXE, exePath))   return false;
    if (!dllOk && !ExtractResource(IDR_ZENSTATES_DLL, dllPath)) return false;

    // inpoutx64.dll is not embedded as a resource, try to copy from exe dir
    if (!inpOk && localInp)
        CopyFileW((localDir + L"inpoutx64.dll").c_str(), inpPath.c_str(), FALSE);

    return true;
}

// ---------------------------------------------------------------------------
// Call the embedded exe with arguments and capture stdout
// ---------------------------------------------------------------------------
static std::string RunSmuTool(const std::wstring& args)
{
    std::wstring cmd = L"\"" + g_SmuTempDir + L"SMUDebugTool.exe\" " + args;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return "";
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL,
                             g_SmuTempDir.c_str(), &si, &pi);
    CloseHandle(hWritePipe);

    std::string output;
    if (ok)
    {
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0)
        {
            buf[bytesRead] = '\0';
            output += buf;
        }
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(hReadPipe);
    return output;
}

// ---------------------------------------------------------------------------
// Parse "Key:           Value" lines from the exe output
// ---------------------------------------------------------------------------
static std::wstring GetField(const std::string& output, const char* key)
{
    std::string tag = std::string(key) + ":";
    auto pos = output.find(tag);
    if (pos == std::string::npos) return L"";

    auto valStart = pos + tag.size();
    while (valStart < output.size() && output[valStart] == ' ') valStart++;
    auto valEnd = output.find('\n', valStart);
    if (valEnd == std::string::npos) valEnd = output.size();
    while (valEnd > valStart && (output[valEnd - 1] == '\r' || output[valEnd - 1] == '\n'))
        valEnd--;

    std::string val = output.substr(valStart, valEnd - valStart);
    return std::wstring(val.begin(), val.end());
}

static void ParseSmuOutput(const std::string& output)
{
    std::lock_guard<std::mutex> lock(g_SmuMutex);
    g_SmuCpuName  = GetField(output, "CPU");
    g_SmuCpuId    = GetField(output, "CPU ID");
    g_SmuModel    = GetField(output, "Model");
    g_SmuPackage  = GetField(output, "Package");
    g_SmuConfig   = GetField(output, "Config");
    g_SmuMobo     = GetField(output, "Mobo");
    g_SmuBios     = GetField(output, "BIOS");
    g_SmuVersion  = GetField(output, "SMU Version");
    g_SmuPatch    = GetField(output, "Patch");
    g_SmuTemp     = GetField(output, "Temp");
    g_SmuProchot  = (GetField(output, "Prochot") == L"1");

    // ACF result line
    auto acfPos = output.find("Setting all cores");
    if (acfPos != std::string::npos)
    {
        auto lineEnd = output.find('\n', acfPos);
        std::string acfLine = output.substr(acfPos, lineEnd - acfPos);
        g_SmuAcfResult = std::wstring(acfLine.begin(), acfLine.end());
    }

    g_SmuReady = !g_SmuCpuName.empty();
}

// ---------------------------------------------------------------------------
// Public API  (declared in Shared.hpp, called from main.cpp)
// ---------------------------------------------------------------------------

bool InitSmuBridge()
{
    if (!ExtractSmuTool())
    {
        OutputDebugStringW(L"SmuBridge: failed to locate/extract SMU tool files\n");
        return false;
    }

    OutputDebugStringW((L"SmuBridge: running from " + g_SmuTempDir + L"\n").c_str());
    std::string output = RunSmuTool(L"");

    if (output.empty())
    {
        OutputDebugStringW(L"SmuBridge: SMUDebugTool.exe returned no output (needs admin?)\n");
        return false;
    }

    // Log raw output for debugging
    OutputDebugStringA(("SmuBridge output:\n" + output).c_str());

    ParseSmuOutput(output);
    return g_SmuReady;
}

bool SmuSetAllCoreFrequency(int mhz)
{
    if (g_SmuTempDir.empty() && !ExtractSmuTool()) return false;

    wchar_t arg[64];
    swprintf_s(arg, L"--acf %d", mhz);
    std::string output = RunSmuTool(arg);
    if (output.empty()) return false;
    ParseSmuOutput(output);
    return output.find("OK") != std::string::npos;
}

std::string SmuGetRawOutput()
{
    if (g_SmuTempDir.empty() && !ExtractSmuTool()) return "";
    return RunSmuTool(L"");
}

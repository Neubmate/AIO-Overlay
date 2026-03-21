#include <windows.h>
#include <string>
#include <iostream>

// Simple unmanaged C++ wrapper - no CLR needed
// This directly loads ZenStates-core.dll and calls its functions

typedef int(__stdcall* SETCORECLOCK)(int, int);
typedef int(__stdcall* GETCORECLOCK)(int, int*);
typedef int(__stdcall* SETCOREVOLTAGE)(int, int);
typedef const char*(__stdcall* GETRAMTIMINGS)();

// CPU Info structure matching ZenStates-Core.Cpu.Info
typedef struct {
    unsigned int cpuid;
    unsigned char family;           // Family enum
    unsigned char codeName;         // CodeName enum
    const char* cpuName;
    unsigned char packageType;      // PackageType enum
    unsigned int baseModel;
    unsigned int extModel;
    unsigned int model;
    unsigned int ccds;
    unsigned int ccxs;
    unsigned int coresPerCcx;
    unsigned int cores;
    unsigned int logicalCores;
    unsigned int physicalCores;
    unsigned int threadsPerCore;
    unsigned int patchLevel;
    unsigned int coreDisableMap;
    // SVI2 struct
    struct {
        unsigned int coreAddress;
        unsigned int socAddress;
    } svi2;
} CPU_INFO;

typedef CPU_INFO* (__stdcall* GETCPUINFO)();

static HMODULE g_hZenStates = NULL;

bool LoadZenStatesDll()
{
    if (g_hZenStates != NULL)
        return true;

    // Try multiple locations
    const wchar_t* searchPaths[] = {
        L"ZenStates-core.dll",
        L".\\ZenStates-core.dll",
        L"..\\ZenStates-core.dll",
        L"..\\..\\ZenStates-core.dll",
    };

    for (const wchar_t* path : searchPaths)
    {
        g_hZenStates = LoadLibraryW(path);
        if (g_hZenStates != NULL)
        {
            wchar_t msg[512];
            swprintf_s(msg, L"✓ Loaded ZenStates-core.dll from: %s", path);
            OutputDebugStringW(msg);
            return true;
        }
    }

    OutputDebugStringW(L"✗ FAILED: ZenStates-core.dll not found in any location!");
    return false;
}

extern "C" {
    // CPU Info getters
    __declspec(dllexport) const char* GetCpuNameNative()
    {
        if (!LoadZenStatesDll())
            return "ERROR";

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return "NOT_FOUND";

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info && info->cpuName ? info->cpuName : "NULL";
        }
        catch (...)
        {
            return "ERROR";
        }
    }

    __declspec(dllexport) unsigned int GetCpuFamilyNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->family : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetCpuCodeNameNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->codeName : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetPackageTypeNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->packageType : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetCcdsNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->ccds : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetCcxsNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->ccxs : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetCoresPerCcxNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->coresPerCcx : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetLogicalCoresNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->logicalCores : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetPhysicalCoresNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->physicalCores : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetPatchLevelNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->patchLevel : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetSvi2CoreAddressNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->svi2.coreAddress : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) unsigned int GetSvi2SocAddressNative()
    {
        if (!LoadZenStatesDll())
            return 0;

        GETCPUINFO getCpuInfoFunc = (GETCPUINFO)GetProcAddress(g_hZenStates, "GetCpuInfo");
        if (!getCpuInfoFunc)
            return 0;

        try
        {
            CPU_INFO* info = getCpuInfoFunc();
            return info ? info->svi2.socAddress : 0;
        }
        catch (...)
        {
            return 0;
        }
    }

    __declspec(dllexport) bool SetCoreClockNative(int core, int mhz)
    {
        if (!LoadZenStatesDll())
        {
            OutputDebugStringW(L"✗ Could not load ZenStates-core.dll");
            return false;
        }

        SETCORECLOCK setClockFunc = NULL;

        const char* functionNames[] = {
            "SetCoreClock",
            "SetP0Clock",
            "ApplyCoreClock",
        };

        for (const char* funcName : functionNames)
        {
            setClockFunc = (SETCORECLOCK)GetProcAddress(g_hZenStates, funcName);
            if (setClockFunc != NULL)
            {
                wchar_t msg[256];
                swprintf_s(msg, L"✓ Found function: %S", funcName);
                OutputDebugStringW(msg);
                break;
            }
        }

        if (setClockFunc == NULL)
        {
            OutputDebugStringW(L"✗ SetCoreClock function not found");
            return false;
        }

        try
        {
            int result = setClockFunc(core, mhz);

            wchar_t msg[256];
            swprintf_s(msg, L"✓ SetCoreClock(core=%d, mhz=%d) returned: %d", core, mhz, result);
            OutputDebugStringW(msg);

            return (result == 0);
        }
        catch (...)
        {
            OutputDebugStringW(L"✗ Exception calling SetCoreClock");
            return false;
        }
    }

    __declspec(dllexport) void UnloadZenStatesDll()
    {
        if (g_hZenStates != NULL)
        {
            FreeLibrary(g_hZenStates);
            g_hZenStates = NULL;
        }
    }
}

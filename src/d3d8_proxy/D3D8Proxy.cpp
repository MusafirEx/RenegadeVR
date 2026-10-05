#include <windows.h>

#include <cstdio>
#include <cstring>

#include "RenegadeVR/D3D8Hooks.h"
#include "RenegadeVR/D3D8Proxy.h"

// The bootstrap proxy intentionally does not include <d3d8.h>.
// Modern Windows SDKs no longer ship the legacy Direct3D 8 headers, and for
// these early milestones we only need the COM ABI and selected vtable slots.
//
// Direct3DCreate8 returns an IDirect3D8* COM interface pointer. Treating that
// pointer as void* is ABI-compatible for transparent forwarding on x86.
using IDirect3D8Opaque = void;

namespace
{
    HMODULE g_proxyModule = nullptr;
    HMODULE g_realD3D8 = nullptr;
    HMODULE g_vrPlugin = nullptr;
    volatile LONG g_bootstrapLogInitialized = 0;

    using Direct3DCreate8Fn = IDirect3D8Opaque* (WINAPI*)(UINT);
    Direct3DCreate8Fn g_realDirect3DCreate8 = nullptr;

    bool GetProxyDirectory(char* output, DWORD outputSize)
    {
        if (!output || outputSize == 0 || !g_proxyModule)
        {
            return false;
        }

        const DWORD length = GetModuleFileNameA(g_proxyModule, output, outputSize);
        if (length == 0 || length >= outputSize)
        {
            return false;
        }

        char* slash = std::strrchr(output, '\\');
        if (!slash)
        {
            slash = std::strrchr(output, '/');
        }

        if (!slash)
        {
            return false;
        }

        *slash = '\0';
        return true;
    }

    void LogLastError(const char* operation)
    {
        char message[512] = {};
        sprintf_s(
            message,
            "%s failed. GetLastError=%lu",
            operation ? operation : "Operation",
            static_cast<unsigned long>(GetLastError())
        );
        RenegadeVR::D3D8ProxyLog(message);
    }
}

namespace RenegadeVR
{
    void D3D8ProxyLog(const char* message)
    {
        if (!message)
        {
            return;
        }

        char debugLine[1024] = {};
        sprintf_s(debugLine, "[RenegadeVR:D3D8] %s\n", message);
        OutputDebugStringA(debugLine);

        char directory[MAX_PATH] = {};
        if (!GetProxyDirectory(directory, MAX_PATH))
        {
            return;
        }

        char logPath[MAX_PATH] = {};
        sprintf_s(logPath, "%s\\RenegadeVR-bootstrap.log", directory);

        // Start each Renegade process with a fresh bootstrap log. This runs on
        // the first real logging call, outside DllMain/loader lock.
        if (InterlockedCompareExchange(&g_bootstrapLogInitialized, 1, 0) == 0)
        {
            HANDLE resetFile = CreateFileA(
                logPath,
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (resetFile != INVALID_HANDLE_VALUE)
            {
                CloseHandle(resetFile);
            }
        }

        HANDLE file = CreateFileA(
            logPath,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (file == INVALID_HANDLE_VALUE)
        {
            return;
        }

        SYSTEMTIME time = {};
        GetLocalTime(&time);

        char line[1280] = {};
        sprintf_s(
            line,
            "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\r\n",
            static_cast<unsigned>(time.wYear),
            static_cast<unsigned>(time.wMonth),
            static_cast<unsigned>(time.wDay),
            static_cast<unsigned>(time.wHour),
            static_cast<unsigned>(time.wMinute),
            static_cast<unsigned>(time.wSecond),
            static_cast<unsigned>(time.wMilliseconds),
            message
        );

        DWORD written = 0;
        WriteFile(
            file,
            line,
            static_cast<DWORD>(std::strlen(line)),
            &written,
            nullptr
        );

        CloseHandle(file);
    }

    HMODULE LoadRealD3D8()
    {
        if (g_realD3D8)
        {
            return g_realD3D8;
        }

        char systemDirectory[MAX_PATH] = {};
        const UINT length = GetSystemDirectoryA(systemDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            LogLastError("GetSystemDirectoryA");
            return nullptr;
        }

        char d3d8Path[MAX_PATH] = {};
        sprintf_s(d3d8Path, "%s\\d3d8.dll", systemDirectory);

        D3D8ProxyLog("Loading system Direct3D 8 runtime.");

        g_realD3D8 = LoadLibraryA(d3d8Path);
        if (!g_realD3D8)
        {
            LogLastError("LoadLibraryA(system d3d8.dll)");
            return nullptr;
        }

        g_realDirect3DCreate8 = reinterpret_cast<Direct3DCreate8Fn>(
            GetProcAddress(g_realD3D8, "Direct3DCreate8")
        );

        if (!g_realDirect3DCreate8)
        {
            LogLastError("GetProcAddress(Direct3DCreate8)");
            return nullptr;
        }

        D3D8ProxyLog("System Direct3D 8 runtime loaded.");
        return g_realD3D8;
    }

    bool LoadVRPlugin()
    {
        if (g_vrPlugin)
        {
            return true;
        }

        char directory[MAX_PATH] = {};
        if (!GetProxyDirectory(directory, MAX_PATH))
        {
            D3D8ProxyLog("Unable to resolve RenegadeVR proxy directory.");
            return false;
        }

        char pluginPath[MAX_PATH] = {};
        sprintf_s(pluginPath, "%s\\RenegadeVR.dll", directory);

        D3D8ProxyLog("Loading RenegadeVR.dll.");

        g_vrPlugin = LoadLibraryA(pluginPath);
        if (!g_vrPlugin)
        {
            LogLastError("LoadLibraryA(RenegadeVR.dll)");
            return false;
        }

        D3D8ProxyLog("RenegadeVR.dll loaded.");
        return true;
    }
}

extern "C" __declspec(dllexport)
IDirect3D8Opaque* WINAPI Direct3DCreate8(UINT sdkVersion)
{
    RenegadeVR::D3D8ProxyLog("Direct3DCreate8 intercepted.");

    // The VR plugin is optional during bootstrap testing. A failure to load it
    // must not prevent Renegade from falling through to the real D3D8 runtime.
    RenegadeVR::LoadVRPlugin();

    if (!RenegadeVR::LoadRealD3D8() || !g_realDirect3DCreate8)
    {
        RenegadeVR::D3D8ProxyLog("Direct3DCreate8 forwarding unavailable.");
        return nullptr;
    }

    IDirect3D8Opaque* d3d8 = g_realDirect3DCreate8(sdkVersion);

    if (!d3d8)
    {
        RenegadeVR::D3D8ProxyLog("Real Direct3DCreate8 returned nullptr.");
        return nullptr;
    }

    RenegadeVR::D3D8ProxyLog("Direct3DCreate8 forwarded successfully.");

    if (!RenegadeVR::InstallD3D8Hooks(d3d8))
    {
        // Hook failure is non-fatal at this stage. Preserve the original game
        // behavior so debugging can continue without blocking Direct3D.
        RenegadeVR::D3D8ProxyLog(
            "Warning: failed to install IDirect3D8 hook; continuing unmodified."
        );
    }

    return d3d8;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_proxyModule = module;
        DisableThreadLibraryCalls(module);
    }

    // Do not LoadLibrary/FreeLibrary here. DllMain runs under the Windows
    // loader lock; bootstrap work is deliberately deferred to Direct3DCreate8.
    return TRUE;
}

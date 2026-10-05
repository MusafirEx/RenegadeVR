#include <windows.h>

#include <string>

#include "RenegadeVR/Logger.h"
#include "RenegadeVR/PluginAPI.h"
#include "RenegadeVR/VRCore.h"
#include "RenegadeVR/Version.h"

namespace
{
    RenegadeVR::VRCore g_vrCore;
    bool g_initialized = false;
}

extern "C" __declspec(dllexport)
BOOL WINAPI RenegadeVR_Initialize()
{
    if (g_initialized)
    {
        return TRUE;
    }

    RenegadeVR::InitializeLogger();
    RenegadeVR::Log(
        std::string("RenegadeVR.dll initialized. Version ") +
        RENEGADEVR_VERSION_STRING
    );

    if (!g_vrCore.Initialize())
    {
        RenegadeVR::Log("VRCore initialization failed.");
        RenegadeVR::ShutdownLogger();
        return FALSE;
    }

    g_initialized = true;
    return TRUE;
}

extern "C" __declspec(dllexport)
void WINAPI RenegadeVR_Shutdown()
{
    if (!g_initialized)
    {
        return;
    }

    g_vrCore.Shutdown();
    RenegadeVR::Log("RenegadeVR.dll shutting down.");
    RenegadeVR::ShutdownLogger();
    g_initialized = false;
}

extern "C" __declspec(dllexport)
void WINAPI RenegadeVR_Update()
{
    if (g_initialized)
    {
        g_vrCore.Update();
    }
}

extern "C" __declspec(dllexport)
BOOL WINAPI RenegadeVR_GetHeadPose(RenegadeVR::HeadPose* pose)
{
    if (!pose)
    {
        return FALSE;
    }

    *pose = {};

    if (!g_initialized)
    {
        return FALSE;
    }

    return g_vrCore.GetHeadPose(*pose) ? TRUE : FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
    }

    // Keep DllMain loader-lock safe. Logger, VR runtime and other subsystems are
    // initialized explicitly by the D3D8 proxy after LoadLibrary returns.
    return TRUE;
}

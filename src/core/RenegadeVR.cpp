#include <windows.h>
#include <string>
#include "RenegadeVR/Logger.h"
#include "RenegadeVR/VRCore.h"
#include "RenegadeVR/Version.h"
namespace { RenegadeVR::VRCore g_vrCore; }
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(module);
            RenegadeVR::InitializeLogger();
            RenegadeVR::Log(std::string("RenegadeVR.dll loaded. Version ") + RENEGADEVR_VERSION_STRING);
            g_vrCore.Initialize();
            break;
        case DLL_PROCESS_DETACH:
            g_vrCore.Shutdown();
            RenegadeVR::Log("RenegadeVR.dll unloading.");
            RenegadeVR::ShutdownLogger();
            break;
    }
    return TRUE;
}

#pragma once

#include <windows.h>

namespace RenegadeVR
{
    HMODULE LoadRealD3D8();
    bool LoadVRPlugin();

    // Shared bootstrap logger used by the proxy and D3D8 hook layer.
    void D3D8ProxyLog(const char* message);
}

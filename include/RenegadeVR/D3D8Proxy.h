#pragma once

#include <windows.h>

#include "RenegadeVR/PluginAPI.h"

namespace RenegadeVR
{
    HMODULE LoadRealD3D8();
    bool LoadVRPlugin();

    // Returns a live head pose when the plugin has a valid tracking provider.
    // While OpenXR is not connected this returns false and the renderer hook
    // can continue using its debug INI rotation fallback.
    bool TryGetVRHeadPose(HeadPose& pose);

    // Shared bootstrap logger used by the proxy and D3D8 hook layer.
    void D3D8ProxyLog(const char* message);
}

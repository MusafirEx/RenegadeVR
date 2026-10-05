#pragma once

#include <windows.h>

namespace RenegadeVR
{
    struct HeadPose
    {
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float rollDegrees = 0.0f;
        BOOL orientationValid = FALSE;
    };

    using PluginInitializeFn = BOOL (WINAPI*)();
    using PluginShutdownFn = void (WINAPI*)();
    using PluginUpdateFn = void (WINAPI*)();
    using PluginGetHeadPoseFn = BOOL (WINAPI*)(HeadPose* pose);
}

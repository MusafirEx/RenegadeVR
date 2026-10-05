#include "RenegadeVR/VRCore.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "RenegadeVR/Logger.h"

namespace
{
    bool GetCoreIniPath(char* output, DWORD outputSize)
    {
        if (!output || outputSize == 0)
        {
            return false;
        }

        HMODULE module = nullptr;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetCoreIniPath),
                &module))
        {
            return false;
        }

        const DWORD length = GetModuleFileNameA(module, output, outputSize);
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

        *(slash + 1) = '\0';
        strcat_s(output, outputSize, "RenegadeVR.ini");
        return true;
    }
}

namespace RenegadeVR
{
    bool VRCore::Initialize()
    {
        if (initialized_)
        {
            return true;
        }

        headPose_ = {};

        char iniPath[MAX_PATH] = {};
        if (GetCoreIniPath(iniPath, MAX_PATH))
        {
            vrEnabled_ =
                GetPrivateProfileIntA("VR", "Enabled", 0, iniPath) != 0;
            headTrackingEnabled_ =
                GetPrivateProfileIntA("VR", "HeadTracking", 0, iniPath) != 0;
            useXRHost_ =
                GetPrivateProfileIntA("VR", "UseXRHost", 1, iniPath) != 0;
        }
        else
        {
            Log("VRCore: unable to resolve RenegadeVR.ini path.");
        }

        initialized_ = true;

        char message[256] = {};
        sprintf_s(
            message,
            "VRCore::Initialize - VR=%s HeadTracking=%s XRHost=%s.",
            vrEnabled_ ? "on" : "off",
            headTrackingEnabled_ ? "on" : "off",
            useXRHost_ ? "on" : "off"
        );
        Log(message);

        if (vrEnabled_ && headTrackingEnabled_ && useXRHost_)
        {
            hostClient_.Initialize();
        }

        return true;
    }

    void VRCore::Shutdown()
    {
        if (!initialized_)
        {
            return;
        }

        hostClient_.Shutdown();

        headPose_ = {};
        initialized_ = false;
        Log("VRCore::Shutdown");
    }

    void VRCore::Update()
    {
        if (!initialized_)
        {
            return;
        }

        headPose_ = {};

        if (!vrEnabled_ || !headTrackingEnabled_ || !useXRHost_)
        {
            return;
        }

        hostClient_.Update(headPose_);
    }

    bool VRCore::GetHeadPose(HeadPose& pose) const
    {
        pose = headPose_;
        return initialized_ && headPose_.orientationValid != FALSE;
    }
}

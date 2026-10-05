#include "RenegadeVR/VRCore.h"

#include "RenegadeVR/Logger.h"

namespace RenegadeVR
{
    bool VRCore::Initialize()
    {
        if (initialized_)
        {
            return true;
        }

        headPose_ = {};
        initialized_ = true;

        Log("VRCore::Initialize - head pose interface ready; OpenXR provider not connected yet.");
        return true;
    }

    void VRCore::Shutdown()
    {
        if (!initialized_)
        {
            return;
        }

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

        // Future milestone:
        // poll OpenXR here and populate headPose_. Until then orientationValid
        // remains FALSE, so the D3D8 camera hook continues to use debug INI
        // rotation as its fallback test source.
    }

    bool VRCore::GetHeadPose(HeadPose& pose) const
    {
        pose = headPose_;
        return initialized_ && headPose_.orientationValid != FALSE;
    }
}

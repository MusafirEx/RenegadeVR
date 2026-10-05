#pragma once

#include "RenegadeVR/PluginAPI.h"
#include "RenegadeVR/VRHostClient.h"

namespace RenegadeVR
{
    class VRCore
    {
    public:
        bool Initialize();
        void Shutdown();
        void Update();

        bool IsInitialized() const { return initialized_; }
        bool GetHeadPose(HeadPose& pose) const;

    private:
        bool initialized_ = false;
        bool vrEnabled_ = false;
        bool headTrackingEnabled_ = false;
        bool useXRHost_ = true;

        HeadPose headPose_ = {};
        VRHostClient hostClient_;
    };
}

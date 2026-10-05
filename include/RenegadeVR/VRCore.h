#pragma once

#include "RenegadeVR/PluginAPI.h"

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
        HeadPose headPose_ = {};
    };
}

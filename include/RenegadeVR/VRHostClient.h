#pragma once

#include <windows.h>

#include <cstdint>

#include "RenegadeVR/PluginAPI.h"
#include "RenegadeVR/VRHostProtocol.h"

namespace RenegadeVR
{
    class VRHostClient
    {
    public:
        VRHostClient() = default;
        ~VRHostClient();

        bool Initialize();
        void Shutdown();
        bool Update(HeadPose& pose);

        bool IsAvailable() const { return shared_ != nullptr; }

    private:
        bool CreateSharedMemory();
        bool LaunchHost();
        bool ReadStableSnapshot(VRHostSharedBlock& snapshot) const;
        void LogStateChange(const VRHostSharedBlock& snapshot);
        void ResetTrackingOrigin();

        HANDLE mapping_ = nullptr;
        VRHostSharedBlock* shared_ = nullptr;

        PROCESS_INFORMATION hostProcess_ = {};

        std::uint32_t lastState_ = 0xFFFFFFFFu;
        std::uint64_t lastFrameIndex_ = 0;

        bool originValid_ = false;
        float originQuatX_ = 0.0f;
        float originQuatY_ = 0.0f;
        float originQuatZ_ = 0.0f;
        float originQuatW_ = 1.0f;

        bool firstPoseLogged_ = false;
    };
}

#pragma once

#include <cstdint>

#define RVR_XRHOST_EXE_NAME "RenegadeVR-xrhost.exe"
#define RVR_XRHOST_SHM_NAME_FMT "Local\\RenegadeVR-xrhost-shm-%lu"
#define RVR_XRHOST_MAGIC 0x52565248u
#define RVR_XRHOST_PROTOCOL_VERSION 1u

#pragma pack(push, 4)

namespace RenegadeVR
{
    enum class VRHostState : std::uint32_t
    {
        Starting = 0,
        NoRuntime = 1,
        WaitingForSession = 2,
        Running = 3,
        SessionLost = 4,
        Fatal = 5
    };

    struct VRHostPose
    {
        float posX;
        float posY;
        float posZ;

        float quatX;
        float quatY;
        float quatZ;
        float quatW;

        std::uint32_t orientationValid;
        std::uint32_t positionValid;
    };

    struct VRHostSharedBlock
    {
        std::uint32_t magic;
        std::uint32_t protocolVersion;
        std::uint32_t state;
        std::uint32_t lastError;

        std::uint32_t gamePid;
        std::uint32_t hostPid;

        // Host increments to odd before writing frame data and to even after.
        std::uint32_t frameSeq;
        std::uint32_t sessionFocused;

        std::uint64_t hostFrameIndex;
        VRHostPose headPose;

        std::uint32_t gameRequestsStop;
        std::uint32_t reserved[15];
    };
}

#pragma pack(pop)

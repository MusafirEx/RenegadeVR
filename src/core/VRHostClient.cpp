#include "RenegadeVR/VRHostClient.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "RenegadeVR/Logger.h"

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    bool GetModuleDirectory(char* output, DWORD outputSize)
    {
        if (!output || outputSize == 0)
        {
            return false;
        }

        HMODULE module = nullptr;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetModuleDirectory),
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

        *slash = '\0';
        return true;
    }

    void NormalizeQuaternion(float& x, float& y, float& z, float& w)
    {
        const float length = std::sqrt(x * x + y * y + z * z + w * w);
        if (length <= 0.000001f)
        {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
            w = 1.0f;
            return;
        }

        const float inv = 1.0f / length;
        x *= inv;
        y *= inv;
        z *= inv;
        w *= inv;
    }

    void MultiplyQuaternion(
        float ax, float ay, float az, float aw,
        float bx, float by, float bz, float bw,
        float& outX, float& outY, float& outZ, float& outW)
    {
        outW = aw * bw - ax * bx - ay * by - az * bz;
        outX = aw * bx + ax * bw + ay * bz - az * by;
        outY = aw * by - ax * bz + ay * bw + az * bx;
        outZ = aw * bz + ax * by - ay * bx + az * bw;
        NormalizeQuaternion(outX, outY, outZ, outW);
    }

    void QuaternionToEulerDegrees(
        float x, float y, float z, float w,
        float& yawDegrees,
        float& pitchDegrees,
        float& rollDegrees)
    {
        NormalizeQuaternion(x, y, z, w);

        // OpenXR is right-handed with +Y up. These equations extract an
        // orientation compatible with RenegadeVR's camera-space Yaw(Y),
        // Pitch(X), Roll(Z) debug transform.
        const float sinPitch =
            std::clamp(2.0f * (w * x - z * y), -1.0f, 1.0f);

        const float pitch = std::asin(sinPitch);
        const float yaw = std::atan2(
            2.0f * (w * y + x * z),
            1.0f - 2.0f * (x * x + y * y)
        );
        const float roll = std::atan2(
            2.0f * (w * z + x * y),
            1.0f - 2.0f * (x * x + z * z)
        );

        constexpr float toDegrees = 180.0f / kPi;
        yawDegrees = yaw * toDegrees;
        pitchDegrees = pitch * toDegrees;
        rollDegrees = roll * toDegrees;
    }
}

namespace RenegadeVR
{
    VRHostClient::~VRHostClient()
    {
        Shutdown();
    }

    bool VRHostClient::Initialize()
    {
        if (shared_)
        {
            return true;
        }

        if (!CreateSharedMemory())
        {
            Log("VRHostClient: failed to create shared memory.");
            return false;
        }

        if (!LaunchHost())
        {
            Log("VRHostClient: xrhost unavailable; continuing without live head tracking.");
            Shutdown();
            return false;
        }

        Log("VRHostClient: RenegadeVR-xrhost.exe launched.");
        return true;
    }

    void VRHostClient::Shutdown()
    {
        if (shared_)
        {
            shared_->gameRequestsStop = 1;
            MemoryBarrier();
        }

        if (hostProcess_.hProcess)
        {
            WaitForSingleObject(hostProcess_.hProcess, 1000);
            CloseHandle(hostProcess_.hProcess);
            hostProcess_.hProcess = nullptr;
        }

        if (hostProcess_.hThread)
        {
            CloseHandle(hostProcess_.hThread);
            hostProcess_.hThread = nullptr;
        }

        if (shared_)
        {
            UnmapViewOfFile(shared_);
            shared_ = nullptr;
        }

        if (mapping_)
        {
            CloseHandle(mapping_);
            mapping_ = nullptr;
        }

        lastState_ = 0xFFFFFFFFu;
        lastFrameIndex_ = 0;
        ResetTrackingOrigin();
        firstPoseLogged_ = false;
    }

    bool VRHostClient::CreateSharedMemory()
    {
        char name[128] = {};
        sprintf_s(
            name,
            RVR_XRHOST_SHM_NAME_FMT,
            static_cast<unsigned long>(GetCurrentProcessId())
        );

        mapping_ = CreateFileMappingA(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(sizeof(VRHostSharedBlock)),
            name
        );

        if (!mapping_)
        {
            return false;
        }

        shared_ = static_cast<VRHostSharedBlock*>(
            MapViewOfFile(
                mapping_,
                FILE_MAP_ALL_ACCESS,
                0,
                0,
                sizeof(VRHostSharedBlock)
            )
        );

        if (!shared_)
        {
            CloseHandle(mapping_);
            mapping_ = nullptr;
            return false;
        }

        std::memset(shared_, 0, sizeof(VRHostSharedBlock));
        shared_->magic = RVR_XRHOST_MAGIC;
        shared_->protocolVersion = RVR_XRHOST_PROTOCOL_VERSION;
        shared_->state = static_cast<std::uint32_t>(VRHostState::Starting);
        shared_->gamePid = GetCurrentProcessId();
        MemoryBarrier();

        return true;
    }

    bool VRHostClient::LaunchHost()
    {
        char directory[MAX_PATH] = {};
        if (!GetModuleDirectory(directory, MAX_PATH))
        {
            return false;
        }

        char hostPath[MAX_PATH] = {};
        sprintf_s(hostPath, "%s\\%s", directory, RVR_XRHOST_EXE_NAME);

        const DWORD attributes = GetFileAttributesA(hostPath);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            Log(
                std::string("VRHostClient: host executable not found: ") +
                hostPath
            );
            return false;
        }

        char commandLine[MAX_PATH + 64] = {};
        sprintf_s(
            commandLine,
            "\"%s\" %lu",
            hostPath,
            static_cast<unsigned long>(GetCurrentProcessId())
        );

        STARTUPINFOA startup = {};
        startup.cb = sizeof(startup);

        PROCESS_INFORMATION process = {};

        if (!CreateProcessA(
                hostPath,
                commandLine,
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                directory,
                &startup,
                &process))
        {
            char message[256] = {};
            sprintf_s(
                message,
                "VRHostClient: CreateProcess failed. GetLastError=%lu",
                static_cast<unsigned long>(GetLastError())
            );
            Log(message);
            return false;
        }

        hostProcess_ = process;
        shared_->hostPid = process.dwProcessId;
        MemoryBarrier();
        return true;
    }

    bool VRHostClient::ReadStableSnapshot(VRHostSharedBlock& snapshot) const
    {
        if (!shared_)
        {
            return false;
        }

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const std::uint32_t seqBefore = shared_->frameSeq;
            MemoryBarrier();

            if ((seqBefore & 1u) != 0)
            {
                continue;
            }

            std::memcpy(&snapshot, shared_, sizeof(snapshot));
            MemoryBarrier();

            const std::uint32_t seqAfter = shared_->frameSeq;

            if (seqBefore == seqAfter && (seqAfter & 1u) == 0)
            {
                return true;
            }
        }

        return false;
    }

    void VRHostClient::LogStateChange(const VRHostSharedBlock& snapshot)
    {
        if (snapshot.state == lastState_)
        {
            return;
        }

        lastState_ = snapshot.state;

        const char* name = "Unknown";
        switch (static_cast<VRHostState>(snapshot.state))
        {
            case VRHostState::Starting: name = "Starting"; break;
            case VRHostState::NoRuntime: name = "NoRuntime"; break;
            case VRHostState::WaitingForSession: name = "WaitingForSession"; break;
            case VRHostState::Running: name = "Running"; break;
            case VRHostState::SessionLost: name = "SessionLost"; break;
            case VRHostState::Fatal: name = "Fatal"; break;
            default: break;
        }

        char message[256] = {};
        sprintf_s(
            message,
            "VRHostClient: state=%s (%u), lastError=%u.",
            name,
            snapshot.state,
            snapshot.lastError
        );
        Log(message);
    }

    void VRHostClient::ResetTrackingOrigin()
    {
        originValid_ = false;
        originQuatX_ = 0.0f;
        originQuatY_ = 0.0f;
        originQuatZ_ = 0.0f;
        originQuatW_ = 1.0f;
    }

    bool VRHostClient::Update(HeadPose& pose)
    {
        pose = {};

        if (!shared_)
        {
            return false;
        }

        if (hostProcess_.hProcess &&
            WaitForSingleObject(hostProcess_.hProcess, 0) == WAIT_OBJECT_0)
        {
            Log("VRHostClient: xrhost process exited.");
            Shutdown();
            return false;
        }

        VRHostSharedBlock snapshot = {};
        if (!ReadStableSnapshot(snapshot))
        {
            return false;
        }

        if (snapshot.magic != RVR_XRHOST_MAGIC ||
            snapshot.protocolVersion != RVR_XRHOST_PROTOCOL_VERSION)
        {
            Log("VRHostClient: shared-memory protocol mismatch.");
            return false;
        }

        LogStateChange(snapshot);

        if (static_cast<VRHostState>(snapshot.state) != VRHostState::Running ||
            snapshot.headPose.orientationValid == 0)
        {
            return false;
        }

        if (snapshot.hostFrameIndex == 0)
        {
            return false;
        }

        // The game may render faster than the headset refresh rate. Reuse the
        // most recent valid pose between OpenXR frames instead of dropping back
        // to an untracked camera for those extra game frames.
        lastFrameIndex_ = snapshot.hostFrameIndex;

        float currentX = snapshot.headPose.quatX;
        float currentY = snapshot.headPose.quatY;
        float currentZ = snapshot.headPose.quatZ;
        float currentW = snapshot.headPose.quatW;
        NormalizeQuaternion(currentX, currentY, currentZ, currentW);

        if (!originValid_)
        {
            originQuatX_ = currentX;
            originQuatY_ = currentY;
            originQuatZ_ = currentZ;
            originQuatW_ = currentW;
            originValid_ = true;

            pose.orientationValid = TRUE;

            Log("VRHostClient: tracking origin captured; current HMD pose is neutral.");
            return true;
        }

        // Relative orientation = inverse(origin) * current.
        float relX = 0.0f;
        float relY = 0.0f;
        float relZ = 0.0f;
        float relW = 1.0f;

        MultiplyQuaternion(
            -originQuatX_,
            -originQuatY_,
            -originQuatZ_,
            originQuatW_,
            currentX,
            currentY,
            currentZ,
            currentW,
            relX,
            relY,
            relZ,
            relW
        );

        QuaternionToEulerDegrees(
            relX,
            relY,
            relZ,
            relW,
            pose.yawDegrees,
            pose.pitchDegrees,
            pose.rollDegrees
        );

        pose.orientationValid = TRUE;

        if (!firstPoseLogged_)
        {
            char message[256] = {};
            sprintf_s(
                message,
                "VRHostClient: first live relative pose Yaw=%.3f Pitch=%.3f Roll=%.3f.",
                pose.yawDegrees,
                pose.pitchDegrees,
                pose.rollDegrees
            );
            Log(message);
            firstPoseLogged_ = true;
        }

        return true;
    }
}

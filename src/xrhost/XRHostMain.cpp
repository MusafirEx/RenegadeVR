#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "RenegadeVR/VRHostProtocol.h"

namespace
{
    FILE* g_log = nullptr;

    void LogLine(const char* format, ...)
    {
        char buffer[1024] = {};

        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (g_log)
        {
            fprintf(g_log, "%s\n", buffer);
            fflush(g_log);
        }

        OutputDebugStringA(buffer);
        OutputDebugStringA("\n");
    }

    void SetHostState(
        RenegadeVR::VRHostSharedBlock* shared,
        RenegadeVR::VRHostState state,
        std::uint32_t lastError = 0)
    {
        if (!shared)
        {
            return;
        }

        shared->lastError = lastError;
        MemoryBarrier();
        shared->state = static_cast<std::uint32_t>(state);
        MemoryBarrier();
    }

    bool IsGameAlive(HANDLE gameProcess)
    {
        return gameProcess &&
            WaitForSingleObject(gameProcess, 0) == WAIT_TIMEOUT;
    }

    bool FindAdapterForLuid(
        const LUID& luid,
        IDXGIAdapter1** adapterOut)
    {
        if (!adapterOut)
        {
            return false;
        }

        *adapterOut = nullptr;

        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(
                __uuidof(IDXGIFactory1),
                reinterpret_cast<void**>(&factory))))
        {
            return false;
        }

        bool found = false;

        for (UINT index = 0; ; ++index)
        {
            IDXGIAdapter1* adapter = nullptr;
            const HRESULT result = factory->EnumAdapters1(index, &adapter);

            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(result) || !adapter)
            {
                continue;
            }

            DXGI_ADAPTER_DESC1 desc = {};
            adapter->GetDesc1(&desc);

            if (desc.AdapterLuid.LowPart == luid.LowPart &&
                desc.AdapterLuid.HighPart == luid.HighPart)
            {
                *adapterOut = adapter;
                found = true;
                break;
            }

            adapter->Release();
        }

        factory->Release();
        return found;
    }

    XrEnvironmentBlendMode PickBlendMode(
        XrInstance instance,
        XrSystemId systemId)
    {
        uint32_t count = 0;
        if (XR_FAILED(xrEnumerateEnvironmentBlendModes(
                instance,
                systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                0,
                &count,
                nullptr)) ||
            count == 0)
        {
            return XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        }

        std::vector<XrEnvironmentBlendMode> modes(count);
        if (XR_FAILED(xrEnumerateEnvironmentBlendModes(
                instance,
                systemId,
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                count,
                &count,
                modes.data())))
        {
            return XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        }

        for (XrEnvironmentBlendMode mode : modes)
        {
            if (mode == XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
            {
                return mode;
            }
        }

        return modes[0];
    }

    void PublishHeadPose(
        RenegadeVR::VRHostSharedBlock* shared,
        const XrView* views,
        uint32_t viewCount,
        XrViewStateFlags flags,
        XrSessionState sessionState,
        std::uint64_t frameIndex)
    {
        if (!shared || !views || viewCount == 0)
        {
            return;
        }

        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&shared->frameSeq)
        );
        MemoryBarrier();

        RenegadeVR::VRHostPose pose = {};

        const bool orientationValid =
            (flags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
        const bool positionValid =
            (flags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;

        const XrPosef& left = views[0].pose;
        pose.quatX = left.orientation.x;
        pose.quatY = left.orientation.y;
        pose.quatZ = left.orientation.z;
        pose.quatW = left.orientation.w;

        if (viewCount >= 2)
        {
            pose.posX =
                0.5f * (views[0].pose.position.x + views[1].pose.position.x);
            pose.posY =
                0.5f * (views[0].pose.position.y + views[1].pose.position.y);
            pose.posZ =
                0.5f * (views[0].pose.position.z + views[1].pose.position.z);
        }
        else
        {
            pose.posX = left.position.x;
            pose.posY = left.position.y;
            pose.posZ = left.position.z;
        }

        pose.orientationValid = orientationValid ? 1u : 0u;
        pose.positionValid = positionValid ? 1u : 0u;

        shared->headPose = pose;
        shared->hostFrameIndex = frameIndex;
        shared->sessionFocused =
            sessionState == XR_SESSION_STATE_FOCUSED ? 1u : 0u;

        MemoryBarrier();
        InterlockedIncrement(
            reinterpret_cast<volatile LONG*>(&shared->frameSeq)
        );
    }

    struct XRContext
    {
        XrInstance instance = XR_NULL_HANDLE;
        XrSystemId systemId = XR_NULL_SYSTEM_ID;
        XrSession session = XR_NULL_HANDLE;
        XrSpace localSpace = XR_NULL_HANDLE;

        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* deviceContext = nullptr;
        IDXGIAdapter1* adapter = nullptr;

        XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
        bool sessionRunning = false;
        XrEnvironmentBlendMode blendMode =
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    };

    void DestroyXR(XRContext& xr)
    {
        if (xr.sessionRunning && xr.session != XR_NULL_HANDLE)
        {
            xrEndSession(xr.session);
            xr.sessionRunning = false;
        }

        if (xr.localSpace != XR_NULL_HANDLE)
        {
            xrDestroySpace(xr.localSpace);
            xr.localSpace = XR_NULL_HANDLE;
        }

        if (xr.session != XR_NULL_HANDLE)
        {
            xrDestroySession(xr.session);
            xr.session = XR_NULL_HANDLE;
        }

        if (xr.instance != XR_NULL_HANDLE)
        {
            xrDestroyInstance(xr.instance);
            xr.instance = XR_NULL_HANDLE;
        }

        if (xr.deviceContext)
        {
            xr.deviceContext->Release();
            xr.deviceContext = nullptr;
        }

        if (xr.device)
        {
            xr.device->Release();
            xr.device = nullptr;
        }

        if (xr.adapter)
        {
            xr.adapter->Release();
            xr.adapter = nullptr;
        }
    }

    XrResult InitializeXR(XRContext& xr)
    {
        const char* extensions[] =
        {
            XR_KHR_D3D11_ENABLE_EXTENSION_NAME
        };

        XrInstanceCreateInfo instanceInfo = {
            XR_TYPE_INSTANCE_CREATE_INFO
        };

        strcpy_s(
            instanceInfo.applicationInfo.applicationName,
            "RenegadeVR"
        );
        strcpy_s(
            instanceInfo.applicationInfo.engineName,
            "Renegade"
        );

        instanceInfo.applicationInfo.applicationVersion = 1;
        instanceInfo.applicationInfo.engineVersion = 1;
        instanceInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

        instanceInfo.enabledExtensionCount = 1;
        instanceInfo.enabledExtensionNames = extensions;

        XrResult result = xrCreateInstance(&instanceInfo, &xr.instance);
        if (XR_FAILED(result))
        {
            return result;
        }

        XrInstanceProperties properties = {
            XR_TYPE_INSTANCE_PROPERTIES
        };

        if (XR_SUCCEEDED(xrGetInstanceProperties(
                xr.instance,
                &properties)))
        {
            LogLine(
                "OpenXR runtime: %s %u.%u.%u",
                properties.runtimeName,
                XR_VERSION_MAJOR(properties.runtimeVersion),
                XR_VERSION_MINOR(properties.runtimeVersion),
                XR_VERSION_PATCH(properties.runtimeVersion)
            );
        }

        XrSystemGetInfo systemInfo = {
            XR_TYPE_SYSTEM_GET_INFO
        };
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        result = xrGetSystem(
            xr.instance,
            &systemInfo,
            &xr.systemId
        );

        if (XR_FAILED(result))
        {
            return result;
        }

        PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;

        result = xrGetInstanceProcAddr(
            xr.instance,
            "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)
        );

        if (XR_FAILED(result) || !getRequirements)
        {
            return XR_ERROR_FUNCTION_UNSUPPORTED;
        }

        XrGraphicsRequirementsD3D11KHR requirements = {
            XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR
        };

        result = getRequirements(
            xr.instance,
            xr.systemId,
            &requirements
        );

        if (XR_FAILED(result))
        {
            return result;
        }

        if (!FindAdapterForLuid(
                requirements.adapterLuid,
                &xr.adapter))
        {
            LogLine("Unable to find runtime-required D3D11 adapter.");
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        }

        const D3D_FEATURE_LEVEL requestedLevels[] =
        {
            requirements.minFeatureLevel
        };
        D3D_FEATURE_LEVEL featureLevel = requirements.minFeatureLevel;

        const HRESULT deviceResult = D3D11CreateDevice(
            xr.adapter,
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            0,
            requestedLevels,
            1,
            D3D11_SDK_VERSION,
            &xr.device,
            &featureLevel,
            &xr.deviceContext
        );

        if (FAILED(deviceResult))
        {
            LogLine(
                "D3D11CreateDevice failed: 0x%08lX",
                static_cast<unsigned long>(deviceResult)
            );
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        }

        XrGraphicsBindingD3D11KHR graphicsBinding = {
            XR_TYPE_GRAPHICS_BINDING_D3D11_KHR
        };
        graphicsBinding.device = xr.device;

        XrSessionCreateInfo sessionInfo = {
            XR_TYPE_SESSION_CREATE_INFO
        };
        sessionInfo.next = &graphicsBinding;
        sessionInfo.systemId = xr.systemId;

        result = xrCreateSession(
            xr.instance,
            &sessionInfo,
            &xr.session
        );

        if (XR_FAILED(result))
        {
            return result;
        }

        XrReferenceSpaceCreateInfo spaceInfo = {
            XR_TYPE_REFERENCE_SPACE_CREATE_INFO
        };
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;

        result = xrCreateReferenceSpace(
            xr.session,
            &spaceInfo,
            &xr.localSpace
        );

        if (XR_FAILED(result))
        {
            return result;
        }

        xr.blendMode = PickBlendMode(xr.instance, xr.systemId);
        return XR_SUCCESS;
    }

    bool PumpSessionEvents(
        XRContext& xr,
        RenegadeVR::VRHostSharedBlock* shared,
        bool& shouldExit)
    {
        XrEventDataBuffer event = {
            XR_TYPE_EVENT_DATA_BUFFER
        };

        while (xrPollEvent(xr.instance, &event) == XR_SUCCESS)
        {
            if (event.type ==
                XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto* changed =
                    reinterpret_cast<const XrEventDataSessionStateChanged*>(
                        &event
                    );

                xr.sessionState = changed->state;
                LogLine(
                    "OpenXR session state: %d",
                    static_cast<int>(xr.sessionState)
                );

                if (xr.sessionState == XR_SESSION_STATE_READY &&
                    !xr.sessionRunning)
                {
                    XrSessionBeginInfo beginInfo = {
                        XR_TYPE_SESSION_BEGIN_INFO
                    };
                    beginInfo.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

                    const XrResult result =
                        xrBeginSession(xr.session, &beginInfo);

                    if (XR_SUCCEEDED(result))
                    {
                        xr.sessionRunning = true;
                        SetHostState(
                            shared,
                            RenegadeVR::VRHostState::Running
                        );
                        LogLine("OpenXR session running.");
                    }
                    else
                    {
                        SetHostState(
                            shared,
                            RenegadeVR::VRHostState::Fatal,
                            static_cast<std::uint32_t>(result)
                        );
                        return false;
                    }
                }
                else if (
                    xr.sessionState == XR_SESSION_STATE_STOPPING &&
                    xr.sessionRunning)
                {
                    xrEndSession(xr.session);
                    xr.sessionRunning = false;

                    SetHostState(
                        shared,
                        RenegadeVR::VRHostState::WaitingForSession
                    );

                    LogLine("OpenXR session paused.");
                }
                else if (
                    xr.sessionState == XR_SESSION_STATE_EXITING ||
                    xr.sessionState == XR_SESSION_STATE_LOSS_PENDING)
                {
                    SetHostState(
                        shared,
                        RenegadeVR::VRHostState::SessionLost
                    );
                    shouldExit = true;
                }
            }

            event = { XR_TYPE_EVENT_DATA_BUFFER };
        }

        return true;
    }

    bool RunTrackingFrame(
        XRContext& xr,
        RenegadeVR::VRHostSharedBlock* shared,
        std::uint64_t& frameIndex)
    {
        XrFrameWaitInfo waitInfo = {
            XR_TYPE_FRAME_WAIT_INFO
        };
        XrFrameState frameState = {
            XR_TYPE_FRAME_STATE
        };

        XrResult result = xrWaitFrame(
            xr.session,
            &waitInfo,
            &frameState
        );

        if (XR_FAILED(result))
        {
            return false;
        }

        XrFrameBeginInfo beginInfo = {
            XR_TYPE_FRAME_BEGIN_INFO
        };

        result = xrBeginFrame(xr.session, &beginInfo);
        if (XR_FAILED(result))
        {
            return false;
        }

        XrViewLocateInfo locateInfo = {
            XR_TYPE_VIEW_LOCATE_INFO
        };

        locateInfo.viewConfigurationType =
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = xr.localSpace;

        XrViewState viewState = {
            XR_TYPE_VIEW_STATE
        };

        XrView views[2] =
        {
            { XR_TYPE_VIEW },
            { XR_TYPE_VIEW }
        };

        uint32_t viewCount = 0;

        result = xrLocateViews(
            xr.session,
            &locateInfo,
            &viewState,
            2,
            &viewCount,
            views
        );

        if (XR_SUCCEEDED(result) && viewCount > 0)
        {
            PublishHeadPose(
                shared,
                views,
                viewCount,
                viewState.viewStateFlags,
                xr.sessionState,
                ++frameIndex
            );
        }

        XrFrameEndInfo endInfo = {
            XR_TYPE_FRAME_END_INFO
        };

        endInfo.displayTime = frameState.predictedDisplayTime;
        endInfo.environmentBlendMode = xr.blendMode;
        endInfo.layerCount = 0;
        endInfo.layers = nullptr;

        result = xrEndFrame(xr.session, &endInfo);
        return XR_SUCCEEDED(result);
    }
}

int main(int argc, char** argv)
{
    fopen_s(&g_log, "RenegadeVR-xrhost.log", "w");

    LogLine("RenegadeVR xrhost starting.");

    if (argc < 2)
    {
        LogLine("Missing game PID.");
        return 2;
    }

    const DWORD gamePid = static_cast<DWORD>(
        std::strtoul(argv[1], nullptr, 10)
    );

    if (gamePid == 0)
    {
        LogLine("Invalid game PID.");
        return 3;
    }

    char mappingName[128] = {};
    sprintf_s(
        mappingName,
        RVR_XRHOST_SHM_NAME_FMT,
        static_cast<unsigned long>(gamePid)
    );

    HANDLE mapping = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        mappingName
    );

    if (!mapping)
    {
        LogLine(
            "OpenFileMapping failed. GetLastError=%lu",
            static_cast<unsigned long>(GetLastError())
        );
        return 4;
    }

    auto* shared = static_cast<RenegadeVR::VRHostSharedBlock*>(
        MapViewOfFile(
            mapping,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(RenegadeVR::VRHostSharedBlock)
        )
    );

    if (!shared)
    {
        CloseHandle(mapping);
        LogLine("MapViewOfFile failed.");
        return 5;
    }

    if (shared->magic != RVR_XRHOST_MAGIC ||
        shared->protocolVersion != RVR_XRHOST_PROTOCOL_VERSION)
    {
        LogLine("Protocol mismatch.");
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        return 6;
    }

    HANDLE gameProcess = OpenProcess(
        SYNCHRONIZE,
        FALSE,
        gamePid
    );

    shared->hostPid = GetCurrentProcessId();
    SetHostState(
        shared,
        RenegadeVR::VRHostState::Starting
    );

    XRContext xr;
    const XrResult initResult = InitializeXR(xr);

    if (XR_FAILED(initResult))
    {
        LogLine(
            "OpenXR initialization failed: %d",
            static_cast<int>(initResult)
        );

        SetHostState(
            shared,
            RenegadeVR::VRHostState::NoRuntime,
            static_cast<std::uint32_t>(initResult)
        );

        DestroyXR(xr);

        if (gameProcess)
        {
            CloseHandle(gameProcess);
        }

        UnmapViewOfFile(shared);
        CloseHandle(mapping);

        if (g_log)
        {
            fclose(g_log);
        }

        return 7;
    }

    SetHostState(
        shared,
        RenegadeVR::VRHostState::WaitingForSession
    );

    bool exitRequested = false;
    std::uint64_t frameIndex = 0;

    while (!exitRequested)
    {
        if (shared->gameRequestsStop != 0)
        {
            LogLine("Game requested xrhost shutdown.");
            break;
        }

        if (gameProcess && !IsGameAlive(gameProcess))
        {
            LogLine("Game process ended.");
            break;
        }

        if (!PumpSessionEvents(xr, shared, exitRequested))
        {
            break;
        }

        if (exitRequested)
        {
            break;
        }

        if (!xr.sessionRunning)
        {
            Sleep(10);
            continue;
        }

        if (!RunTrackingFrame(xr, shared, frameIndex))
        {
            LogLine("Tracking frame failed; waiting for runtime recovery.");
            Sleep(5);
        }
    }

    DestroyXR(xr);

    if (gameProcess)
    {
        CloseHandle(gameProcess);
    }

    UnmapViewOfFile(shared);
    CloseHandle(mapping);

    LogLine(
        "RenegadeVR xrhost exiting after %llu frames.",
        static_cast<unsigned long long>(frameIndex)
    );

    if (g_log)
    {
        fclose(g_log);
        g_log = nullptr;
    }

    return 0;
}

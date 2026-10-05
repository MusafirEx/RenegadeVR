#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdio>

#include "RenegadeVR/D3D8Hooks.h"
#include "RenegadeVR/D3D8Proxy.h"

namespace
{
    // Direct3D 8 COM vtable indices.
    constexpr std::size_t kIDirect3D8_CreateDevice = 15;

    constexpr std::size_t kIDirect3DDevice8_Reset = 14;
    constexpr std::size_t kIDirect3DDevice8_Present = 15;
    constexpr std::size_t kIDirect3DDevice8_BeginScene = 34;
    constexpr std::size_t kIDirect3DDevice8_EndScene = 35;
    constexpr std::size_t kIDirect3DDevice8_SetTransform = 37;

    // D3DTRANSFORMSTATETYPE values from the legacy Direct3D 8 API.
    constexpr DWORD kD3DTS_View = 2;
    constexpr DWORD kD3DTS_Projection = 3;
    constexpr DWORD kD3DTS_World = 256;

    constexpr float kIdentityEpsilon = 0.0005f;
    constexpr float kMatrixChangeEpsilon = 0.0010f;
    constexpr LONG kCameraLogFrameInterval = 60;

    struct LegacyD3DMatrix
    {
        float m[4][4];
    };

    using CreateDeviceFn = HRESULT(WINAPI*)(
        void* self,
        UINT adapter,
        DWORD deviceType,
        HWND focusWindow,
        DWORD behaviorFlags,
        void* presentationParameters,
        void** returnedDevice
    );

    using ResetFn = HRESULT(WINAPI*)(
        void* self,
        void* presentationParameters
    );

    using PresentFn = HRESULT(WINAPI*)(
        void* self,
        const RECT* sourceRect,
        const RECT* destRect,
        HWND destWindowOverride,
        const RGNDATA* dirtyRegion
    );

    using SceneFn = HRESULT(WINAPI*)(void* self);

    using SetTransformFn = HRESULT(WINAPI*)(
        void* self,
        DWORD state,
        const LegacyD3DMatrix* matrix
    );

    CreateDeviceFn g_originalCreateDevice = nullptr;
    ResetFn g_originalReset = nullptr;
    PresentFn g_originalPresent = nullptr;
    SceneFn g_originalBeginScene = nullptr;
    SceneFn g_originalEndScene = nullptr;
    SetTransformFn g_originalSetTransform = nullptr;

    volatile LONG g_presentCount = 0;
    volatile LONG g_beginSceneCount = 0;
    volatile LONG g_endSceneCount = 0;
    volatile LONG g_viewTransformCount = 0;
    volatile LONG g_projectionTransformCount = 0;
    volatile LONG g_worldTransformCount = 0;
    volatile LONG g_cameraCandidateCount = 0;

    LegacyD3DMatrix g_lastProjection = {};
    LegacyD3DMatrix g_lastLoggedCameraView = {};
    bool g_hasProjection = false;
    bool g_hasLoggedCameraView = false;
    LONG g_lastCameraLogFrame = -kCameraLogFrameInterval;

    bool IsApproximatelyIdentity(const LegacyD3DMatrix* matrix)
    {
        if (!matrix)
        {
            return false;
        }

        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                const float expected = (row == column) ? 1.0f : 0.0f;
                if (std::fabs(matrix->m[row][column] - expected) > kIdentityEpsilon)
                {
                    return false;
                }
            }
        }

        return true;
    }

    bool MatricesDiffer(
        const LegacyD3DMatrix& a,
        const LegacyD3DMatrix& b,
        float epsilon)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                if (std::fabs(a.m[row][column] - b.m[row][column]) > epsilon)
                {
                    return true;
                }
            }
        }

        return false;
    }

    template <typename T>
    bool HookVTableEntry(
        void* interfacePointer,
        std::size_t index,
        T replacement,
        T& original,
        const char* name)
    {
        if (!interfacePointer || !replacement)
        {
            RenegadeVR::D3D8ProxyLog("HookVTableEntry received an invalid pointer.");
            return false;
        }

        void*** object = reinterpret_cast<void***>(interfacePointer);
        if (!object || !*object)
        {
            RenegadeVR::D3D8ProxyLog("COM interface has no vtable.");
            return false;
        }

        void** entry = &((*object)[index]);
        void* replacementAddress = reinterpret_cast<void*>(replacement);

        if (*entry == replacementAddress)
        {
            return true;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(entry, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            char message[256] = {};
            sprintf_s(
                message,
                "VirtualProtect failed while hooking %s. GetLastError=%lu",
                name,
                static_cast<unsigned long>(GetLastError())
            );
            RenegadeVR::D3D8ProxyLog(message);
            return false;
        }

        original = reinterpret_cast<T>(*entry);
        *entry = replacementAddress;

        DWORD restoredProtect = 0;
        if (!VirtualProtect(entry, sizeof(void*), oldProtect, &restoredProtect))
        {
            char message[256] = {};
            sprintf_s(
                message,
                "Warning: failed to restore vtable protection for %s. GetLastError=%lu",
                name,
                static_cast<unsigned long>(GetLastError())
            );
            RenegadeVR::D3D8ProxyLog(message);
        }

        char message[256] = {};
        sprintf_s(message, "Hook installed: %s (vtable index %zu).", name, index);
        RenegadeVR::D3D8ProxyLog(message);
        return true;
    }

    void LogMatrix(const char* label, LONG count, const LegacyD3DMatrix* matrix)
    {
        if (!matrix)
        {
            char message[192] = {};
            sprintf_s(message, "%s #%ld has a null matrix.", label, count);
            RenegadeVR::D3D8ProxyLog(message);
            return;
        }

        char message[768] = {};
        sprintf_s(
            message,
            "%s #%ld: "
            "[%.5f %.5f %.5f %.5f] "
            "[%.5f %.5f %.5f %.5f] "
            "[%.5f %.5f %.5f %.5f] "
            "[%.5f %.5f %.5f %.5f]",
            label,
            count,
            matrix->m[0][0], matrix->m[0][1], matrix->m[0][2], matrix->m[0][3],
            matrix->m[1][0], matrix->m[1][1], matrix->m[1][2], matrix->m[1][3],
            matrix->m[2][0], matrix->m[2][1], matrix->m[2][2], matrix->m[2][3],
            matrix->m[3][0], matrix->m[3][1], matrix->m[3][2], matrix->m[3][3]
        );
        RenegadeVR::D3D8ProxyLog(message);
    }

    void LogCameraCandidate(
        LONG viewTransformCount,
        LONG candidateCount,
        const LegacyD3DMatrix* view)
    {
        const LONG frame = g_presentCount;
        const LONG scene = g_beginSceneCount;

        char message[256] = {};
        sprintf_s(
            message,
            "CAMERA_CANDIDATE #%ld detected at PresentFrame=%ld BeginSceneCount=%ld ViewTransform=%ld.",
            candidateCount,
            frame,
            scene,
            viewTransformCount
        );
        RenegadeVR::D3D8ProxyLog(message);

        LogMatrix("CAMERA_VIEW", candidateCount, view);

        if (g_hasProjection)
        {
            LogMatrix(
                "CAMERA_ACTIVE_PROJECTION",
                g_projectionTransformCount,
                &g_lastProjection
            );
        }
        else
        {
            RenegadeVR::D3D8ProxyLog(
                "CAMERA_ACTIVE_PROJECTION unavailable: no projection captured yet."
            );
        }
    }

    HRESULT WINAPI HookReset(void* self, void* presentationParameters)
    {
        RenegadeVR::D3D8ProxyLog("IDirect3DDevice8::Reset intercepted.");

        if (!g_originalReset)
        {
            return E_FAIL;
        }

        const HRESULT result = g_originalReset(self, presentationParameters);

        char message[160] = {};
        sprintf_s(
            message,
            "IDirect3DDevice8::Reset returned 0x%08lX.",
            static_cast<unsigned long>(result)
        );
        RenegadeVR::D3D8ProxyLog(message);

        return result;
    }

    HRESULT WINAPI HookPresent(
        void* self,
        const RECT* sourceRect,
        const RECT* destRect,
        HWND destWindowOverride,
        const RGNDATA* dirtyRegion)
    {
        const LONG frame = InterlockedIncrement(&g_presentCount);

        if (frame == 1 || (frame % 600) == 0)
        {
            char message[160] = {};
            sprintf_s(message, "IDirect3DDevice8::Present intercepted. Frame=%ld.", frame);
            RenegadeVR::D3D8ProxyLog(message);
        }

        if (!g_originalPresent)
        {
            return E_FAIL;
        }

        return g_originalPresent(
            self,
            sourceRect,
            destRect,
            destWindowOverride,
            dirtyRegion
        );
    }

    HRESULT WINAPI HookBeginScene(void* self)
    {
        const LONG count = InterlockedIncrement(&g_beginSceneCount);

        if (count == 1 || (count % 1200) == 0)
        {
            char message[160] = {};
            sprintf_s(message, "IDirect3DDevice8::BeginScene intercepted. Count=%ld.", count);
            RenegadeVR::D3D8ProxyLog(message);
        }

        if (!g_originalBeginScene)
        {
            return E_FAIL;
        }

        return g_originalBeginScene(self);
    }

    HRESULT WINAPI HookEndScene(void* self)
    {
        const LONG count = InterlockedIncrement(&g_endSceneCount);

        if (count == 1 || (count % 1200) == 0)
        {
            char message[160] = {};
            sprintf_s(message, "IDirect3DDevice8::EndScene intercepted. Count=%ld.", count);
            RenegadeVR::D3D8ProxyLog(message);
        }

        if (!g_originalEndScene)
        {
            return E_FAIL;
        }

        return g_originalEndScene(self);
    }

    HRESULT WINAPI HookSetTransform(
        void* self,
        DWORD state,
        const LegacyD3DMatrix* matrix)
    {
        if (state == kD3DTS_View)
        {
            const LONG count = InterlockedIncrement(&g_viewTransformCount);

            if (matrix && !IsApproximatelyIdentity(matrix))
            {
                const LONG candidateCount = InterlockedIncrement(&g_cameraCandidateCount);
                const LONG frame = g_presentCount;

                const bool changed =
                    !g_hasLoggedCameraView ||
                    MatricesDiffer(
                        *matrix,
                        g_lastLoggedCameraView,
                        kMatrixChangeEpsilon
                    );

                const bool enoughFramesElapsed =
                    (frame - g_lastCameraLogFrame) >= kCameraLogFrameInterval;

                if (changed && enoughFramesElapsed)
                {
                    LogCameraCandidate(count, candidateCount, matrix);
                    g_lastLoggedCameraView = *matrix;
                    g_hasLoggedCameraView = true;
                    g_lastCameraLogFrame = frame;
                }
            }
            else if (count <= 3)
            {
                LogMatrix("D3DTS_VIEW_INITIAL", count, matrix);
            }
        }
        else if (state == kD3DTS_Projection)
        {
            const LONG count = InterlockedIncrement(&g_projectionTransformCount);

            if (matrix)
            {
                g_lastProjection = *matrix;
                g_hasProjection = true;
            }

            // Initial samples are useful for verifying the projection hook.
            // After that, projection is logged only alongside camera candidates.
            if (count <= 8)
            {
                LogMatrix("D3DTS_PROJECTION_INITIAL", count, matrix);
            }
        }
        else if (state >= kD3DTS_World)
        {
            const LONG count = InterlockedIncrement(&g_worldTransformCount);

            if (count == 1 || (count % 50000) == 0)
            {
                char message[192] = {};
                sprintf_s(
                    message,
                    "D3DTS_WORLD-family transform intercepted. Count=%ld State=%lu.",
                    count,
                    static_cast<unsigned long>(state)
                );
                RenegadeVR::D3D8ProxyLog(message);
            }
        }

        if (!g_originalSetTransform)
        {
            return E_FAIL;
        }

        return g_originalSetTransform(self, state, matrix);
    }

    bool InstallDeviceHooks(void* device)
    {
        if (!device)
        {
            RenegadeVR::D3D8ProxyLog("CreateDevice succeeded without a device pointer.");
            return false;
        }

        RenegadeVR::D3D8ProxyLog("Installing IDirect3DDevice8 hooks.");

        bool success = true;

        success &= HookVTableEntry(
            device,
            kIDirect3DDevice8_Reset,
            &HookReset,
            g_originalReset,
            "IDirect3DDevice8::Reset"
        );

        success &= HookVTableEntry(
            device,
            kIDirect3DDevice8_Present,
            &HookPresent,
            g_originalPresent,
            "IDirect3DDevice8::Present"
        );

        success &= HookVTableEntry(
            device,
            kIDirect3DDevice8_BeginScene,
            &HookBeginScene,
            g_originalBeginScene,
            "IDirect3DDevice8::BeginScene"
        );

        success &= HookVTableEntry(
            device,
            kIDirect3DDevice8_EndScene,
            &HookEndScene,
            g_originalEndScene,
            "IDirect3DDevice8::EndScene"
        );

        success &= HookVTableEntry(
            device,
            kIDirect3DDevice8_SetTransform,
            &HookSetTransform,
            g_originalSetTransform,
            "IDirect3DDevice8::SetTransform"
        );

        RenegadeVR::D3D8ProxyLog(
            success
                ? "IDirect3DDevice8 hooks installed successfully."
                : "One or more IDirect3DDevice8 hooks failed."
        );

        return success;
    }

    HRESULT WINAPI HookCreateDevice(
        void* self,
        UINT adapter,
        DWORD deviceType,
        HWND focusWindow,
        DWORD behaviorFlags,
        void* presentationParameters,
        void** returnedDevice)
    {
        char message[256] = {};
        sprintf_s(
            message,
            "IDirect3D8::CreateDevice intercepted. Adapter=%u DeviceType=%lu BehaviorFlags=0x%08lX.",
            adapter,
            static_cast<unsigned long>(deviceType),
            static_cast<unsigned long>(behaviorFlags)
        );
        RenegadeVR::D3D8ProxyLog(message);

        if (!g_originalCreateDevice)
        {
            RenegadeVR::D3D8ProxyLog("Original IDirect3D8::CreateDevice pointer is null.");
            return E_FAIL;
        }

        const HRESULT result = g_originalCreateDevice(
            self,
            adapter,
            deviceType,
            focusWindow,
            behaviorFlags,
            presentationParameters,
            returnedDevice
        );

        sprintf_s(
            message,
            "IDirect3D8::CreateDevice returned 0x%08lX. Device=%p.",
            static_cast<unsigned long>(result),
            returnedDevice ? *returnedDevice : nullptr
        );
        RenegadeVR::D3D8ProxyLog(message);

        if (SUCCEEDED(result) && returnedDevice && *returnedDevice)
        {
            InstallDeviceHooks(*returnedDevice);
        }

        return result;
    }
}

namespace RenegadeVR
{
    bool InstallD3D8Hooks(void* d3d8Interface)
    {
        if (!d3d8Interface)
        {
            D3D8ProxyLog("Cannot install IDirect3D8 hooks: interface is null.");
            return false;
        }

        D3D8ProxyLog("Installing IDirect3D8::CreateDevice hook.");

        return HookVTableEntry(
            d3d8Interface,
            kIDirect3D8_CreateDevice,
            &HookCreateDevice,
            g_originalCreateDevice,
            "IDirect3D8::CreateDevice"
        );
    }
}

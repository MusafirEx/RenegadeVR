#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    // Renegade's main world camera uses a standard perspective transform with
    // m[2][3] ~= -1, m[3][3] ~= 0 and a short near plane. Other passes seen in
    // the game use orthographic/utility matrices or a much larger near plane.
    constexpr float kPerspectiveEpsilon = 0.01f;
    constexpr float kMainCameraNearMin = 0.05f;
    constexpr float kMainCameraNearMax = 1.00f;
    constexpr float kPi = 3.14159265358979323846f;

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
    volatile LONG g_mainCameraViewCount = 0;

    LegacyD3DMatrix g_lastProjection = {};
    LegacyD3DMatrix g_lastLoggedCameraView = {};
    bool g_hasProjection = false;
    bool g_hasLoggedCameraView = false;
    LONG g_lastCameraLogFrame = -kCameraLogFrameInterval;
    bool g_mainProjectionAnnounced = false;

    bool g_debugCameraYawEnabled = false;
    float g_debugCameraYawDegrees = 0.0f;
    volatile LONG g_debugCameraYawApplyCount = 0;

    bool GetProxyIniPath(char* output, DWORD outputSize)
    {
        if (!output || outputSize == 0)
        {
            return false;
        }

        HMODULE module = nullptr;
        if (!GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&GetProxyIniPath),
                &module))
        {
            return false;
        }

        char modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(module, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return false;
        }

        char* slash = std::strrchr(modulePath, '\\');
        if (!slash)
        {
            slash = std::strrchr(modulePath, '/');
        }

        if (!slash)
        {
            return false;
        }

        *(slash + 1) = '\0';
        sprintf_s(output, outputSize, "%sRenegadeVR.ini", modulePath);
        return true;
    }

    void LoadDebugCameraSettings()
    {
        char iniPath[MAX_PATH] = {};
        if (!GetProxyIniPath(iniPath, MAX_PATH))
        {
            RenegadeVR::D3D8ProxyLog(
                "Debug camera settings unavailable: RenegadeVR.ini path could not be resolved."
            );
            return;
        }

        g_debugCameraYawEnabled =
            GetPrivateProfileIntA(
                "Debug",
                "EnableCameraYawTest",
                0,
                iniPath
            ) != 0;

        char yawText[64] = {};
        GetPrivateProfileStringA(
            "Debug",
            "CameraYawDegrees",
            "5.0",
            yawText,
            static_cast<DWORD>(sizeof(yawText)),
            iniPath
        );

        g_debugCameraYawDegrees = static_cast<float>(std::atof(yawText));

        char message[256] = {};
        sprintf_s(
            message,
            "Debug camera yaw test: %s, Degrees=%.3f.",
            g_debugCameraYawEnabled ? "enabled" : "disabled",
            g_debugCameraYawDegrees
        );
        RenegadeVR::D3D8ProxyLog(message);
    }

    LegacyD3DMatrix MultiplyMatrices(
        const LegacyD3DMatrix& a,
        const LegacyD3DMatrix& b)
    {
        LegacyD3DMatrix result = {};

        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                float value = 0.0f;

                for (int k = 0; k < 4; ++k)
                {
                    value += a.m[row][k] * b.m[k][column];
                }

                result.m[row][column] = value;
            }
        }

        return result;
    }

    LegacyD3DMatrix ApplyCameraSpaceYaw(
        const LegacyD3DMatrix& view,
        float yawDegrees)
    {
        const float radians = yawDegrees * (kPi / 180.0f);
        const float c = std::cos(radians);
        const float s = std::sin(radians);

        // D3D fixed-function matrices use row-vector convention. Post-
        // multiplying the view transform applies a rotation in camera space,
        // which is exactly what an HMD orientation offset will eventually do.
        LegacyD3DMatrix yaw = {};
        yaw.m[0][0] = c;
        yaw.m[0][2] = -s;
        yaw.m[1][1] = 1.0f;
        yaw.m[2][0] = s;
        yaw.m[2][2] = c;
        yaw.m[3][3] = 1.0f;

        return MultiplyMatrices(view, yaw);
    }

    float EstimateNearPlane(const LegacyD3DMatrix& projection)
    {
        // For Renegade's observed D3D8 perspective matrices:
        // near ~= m[3][2] / m[2][2].
        if (std::fabs(projection.m[2][2]) < 0.000001f)
        {
            return 0.0f;
        }

        return std::fabs(projection.m[3][2] / projection.m[2][2]);
    }

    bool IsMainPerspectiveProjection(const LegacyD3DMatrix& projection)
    {
        if (std::fabs(projection.m[2][3] + 1.0f) > kPerspectiveEpsilon)
        {
            return false;
        }

        if (std::fabs(projection.m[3][3]) > kPerspectiveEpsilon)
        {
            return false;
        }

        if (projection.m[0][0] <= 0.0f || projection.m[1][1] <= 0.0f)
        {
            return false;
        }

        const float nearPlane = EstimateNearPlane(projection);
        return nearPlane >= kMainCameraNearMin && nearPlane <= kMainCameraNearMax;
    }

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
            "MAIN_CAMERA #%ld detected at PresentFrame=%ld BeginSceneCount=%ld ViewTransform=%ld.",
            candidateCount,
            frame,
            scene,
            viewTransformCount
        );
        RenegadeVR::D3D8ProxyLog(message);

        LogMatrix("MAIN_CAMERA_VIEW", candidateCount, view);

        if (g_hasProjection)
        {
            LogMatrix(
                "MAIN_CAMERA_PROJECTION",
                g_projectionTransformCount,
                &g_lastProjection
            );
        }
        else
        {
            RenegadeVR::D3D8ProxyLog(
                "MAIN_CAMERA_PROJECTION unavailable: no projection captured yet."
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

            const bool mainProjectionActive =
                g_hasProjection && IsMainPerspectiveProjection(g_lastProjection);

            if (matrix && !IsApproximatelyIdentity(matrix) && mainProjectionActive)
            {
                const LONG candidateCount = InterlockedIncrement(&g_cameraCandidateCount);
                InterlockedIncrement(&g_mainCameraViewCount);
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

                if (!g_mainProjectionAnnounced && IsMainPerspectiveProjection(*matrix))
                {
                    const float nearPlane = EstimateNearPlane(*matrix);
                    char message[256] = {};
                    sprintf_s(
                        message,
                        "Main perspective projection identified. NearPlane~=%.5f XScale=%.5f YScale=%.5f.",
                        nearPlane,
                        matrix->m[0][0],
                        matrix->m[1][1]
                    );
                    RenegadeVR::D3D8ProxyLog(message);
                    g_mainProjectionAnnounced = true;
                }
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

        if (
            state == kD3DTS_View &&
            matrix &&
            g_debugCameraYawEnabled &&
            g_hasProjection &&
            IsMainPerspectiveProjection(g_lastProjection) &&
            !IsApproximatelyIdentity(matrix) &&
            std::fabs(g_debugCameraYawDegrees) > 0.0001f)
        {
            LegacyD3DMatrix modifiedView =
                ApplyCameraSpaceYaw(*matrix, g_debugCameraYawDegrees);

            const LONG applyCount =
                InterlockedIncrement(&g_debugCameraYawApplyCount);

            if (applyCount == 1)
            {
                char message[256] = {};
                sprintf_s(
                    message,
                    "DEBUG_CAMERA_YAW applied for first time. Degrees=%.3f PresentFrame=%ld.",
                    g_debugCameraYawDegrees,
                    g_presentCount
                );
                RenegadeVR::D3D8ProxyLog(message);
                LogMatrix("DEBUG_CAMERA_YAW_ORIGINAL_VIEW", applyCount, matrix);
                LogMatrix("DEBUG_CAMERA_YAW_MODIFIED_VIEW", applyCount, &modifiedView);
            }

            return g_originalSetTransform(self, state, &modifiedView);
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

        LoadDebugCameraSettings();

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

#include <windows.h>

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

    CreateDeviceFn g_originalCreateDevice = nullptr;
    ResetFn g_originalReset = nullptr;
    PresentFn g_originalPresent = nullptr;
    SceneFn g_originalBeginScene = nullptr;
    SceneFn g_originalEndScene = nullptr;

    volatile LONG g_presentCount = 0;
    volatile LONG g_beginSceneCount = 0;
    volatile LONG g_endSceneCount = 0;

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

        // Avoid writing to disk every frame. The first Present proves the hook
        // works; periodic messages make it easy to confirm it remains active.
        if (frame == 1 || (frame % 300) == 0)
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

        if (count == 1 || (count % 300) == 0)
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

        if (count == 1 || (count % 300) == 0)
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

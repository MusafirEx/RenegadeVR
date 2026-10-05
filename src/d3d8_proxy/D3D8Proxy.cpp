#include <windows.h>
#include <d3d8.h>
#include "RenegadeVR/D3D8Proxy.h"
namespace {
HMODULE g_realD3D8 = nullptr;
HMODULE g_vrPlugin = nullptr;
using Direct3DCreate8Fn = IDirect3D8* (WINAPI*)(UINT);
Direct3DCreate8Fn g_realDirect3DCreate8 = nullptr;
}
namespace RenegadeVR {
HMODULE LoadRealD3D8() {
    if (g_realD3D8) return g_realD3D8;
    char systemDirectory[MAX_PATH] = {};
    if (!GetSystemDirectoryA(systemDirectory, MAX_PATH)) return nullptr;
    char d3d8Path[MAX_PATH] = {};
    wsprintfA(d3d8Path, "%s\\d3d8.dll", systemDirectory);
    g_realD3D8 = LoadLibraryA(d3d8Path);
    if (!g_realD3D8) return nullptr;
    g_realDirect3DCreate8 = reinterpret_cast<Direct3DCreate8Fn>(GetProcAddress(g_realD3D8, "Direct3DCreate8"));
    return g_realD3D8;
}
bool LoadVRPlugin() {
    if (g_vrPlugin) return true;
    g_vrPlugin = LoadLibraryA("RenegadeVR.dll");
    return g_vrPlugin != nullptr;
}
}
extern "C" __declspec(dllexport) IDirect3D8* WINAPI Direct3DCreate8(UINT sdkVersion) {
    RenegadeVR::LoadVRPlugin();
    if (!RenegadeVR::LoadRealD3D8() || !g_realDirect3DCreate8) return nullptr;
    return g_realDirect3DCreate8(sdkVersion);
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(module);
    else if (reason == DLL_PROCESS_DETACH && g_realD3D8) { FreeLibrary(g_realD3D8); g_realD3D8 = nullptr; }
    return TRUE;
}

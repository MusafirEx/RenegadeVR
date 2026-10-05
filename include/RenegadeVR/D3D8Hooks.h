#pragma once

namespace RenegadeVR
{
    // Installs a minimal vtable hook on IDirect3D8::CreateDevice.
    // The interface is intentionally opaque so the project does not require
    // the legacy DirectX 8 SDK headers.
    bool InstallD3D8Hooks(void* d3d8Interface);
}

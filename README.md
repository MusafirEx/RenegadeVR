# RenegadeVR

Experimental VR plugin for **Command & Conquer: Renegade (Steam, Windows x86)**.

## Current architecture

```text
Game.exe (x86)
   |
   v
d3d8.dll                    <- D3D8 proxy / renderer hooks
   |
   +--> RenegadeVR.dll      <- x86 VR core + xrhost client
   |       |
   |       +--> shared memory head pose
   |               ^
   |               |
   |    RenegadeVR-xrhost.exe (x64)
   |               |
   |               +--> OpenXR runtime
   |
   +--> Windows system Direct3D 8
```

The x64 OpenXR host is deliberate. The original game remains a 32-bit
Direct3D 8 process, while the host owns the modern OpenXR session and sends
tracking data back through a fixed shared-memory protocol.

## Current status

Working development milestones:

1. x86 local `d3d8.dll` proxy forwards to the real system Direct3D 8 runtime.
2. `RenegadeVR.dll` loads through an explicit plugin API.
3. D3D8 device hooks capture the main Renegade world camera.
4. Debug yaw / pitch / roll transforms have validated camera manipulation.
5. Gameplay mouse focus is automatically restored when the world camera starts.
6. x86/x64 shared-memory head-pose bridge is implemented.
7. x64 `RenegadeVR-xrhost.exe` can own a tracking-only OpenXR session.
8. Stereo rendering, eye textures, motion controllers, weapon tracking and hands
   remain future milestones.

## Target

- Game: Command & Conquer: Renegade
- Distribution: Steam
- Game process: x86 / 32-bit
- Renderer: Direct3D 8 / WW3D
- VR runtime: OpenXR through x64 host
- Build system: CMake
- Language: C++17
- Initial testing: single-player/offline

## Build the game-side plugin (Win32)

Use a 32-bit MSVC toolchain:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Outputs:

```text
build/bin/Release/
├─ d3d8.dll
├─ RenegadeVR.dll
└─ RenegadeVR.ini
```

## Build the OpenXR host (x64)

The host is a separate x64 CMake project and uses a pinned vcpkg manifest
for `openxr-loader`. This is compatible with the manifest-only vcpkg bundled
with recent Visual Studio installations.

With `VCPKG_ROOT` set to your vcpkg folder:

```bat
rmdir /s /q build-xrhost

cmake -S xrhost -B build-xrhost -A x64 ^
  "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  "-DVCPKG_TARGET_TRIPLET=x64-windows"

cmake --build build-xrhost --config Release
```

Expected host output:

```text
build-xrhost/bin/Release/
└─ RenegadeVR-xrhost.exe
```

vcpkg may also place the required `openxr_loader.dll` beside the executable.
Keep any runtime DLL copied by vcpkg with the host executable.

## Test installation

Place these beside Renegade's `Game.exe`:

```text
d3d8.dll
RenegadeVR.dll
RenegadeVR.ini
RenegadeVR-xrhost.exe
openxr_loader.dll           <- if produced/required by the x64 host build
```

Current default configuration enables hosted head tracking but leaves stereo and
controllers disabled:

```ini
[VR]
Enabled=1
Runtime=OpenXR
UseXRHost=1
StereoRendering=0
HeadTracking=1
MotionControllers=0

[Debug]
EnableCameraRotationTest=0

[Input]
AutoRestoreMouseFocus=1
```

If the host executable or a working OpenXR runtime is unavailable, RenegadeVR
falls back to the normal non-tracked camera rather than blocking the game.

Do **not** replace or modify the Windows system copy of `d3d8.dll`.
The proxy DLL belongs only beside Renegade's `Game.exe`.

Do not commit proprietary Renegade game assets or Steam binaries to this repository.

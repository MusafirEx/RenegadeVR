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
   |       +--> shared-memory head pose
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
build\bin\Release\
├─ d3d8.dll
├─ RenegadeVR.dll
└─ RenegadeVR.ini
```

## Build the OpenXR host (x64)

The host is a separate x64 CMake project and uses a pinned vcpkg manifest for
`openxr-loader`. This is compatible with the manifest-only vcpkg bundled with
recent Visual Studio 2022 installations.

Example Visual Studio vcpkg path:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg
```

Confirm `VCPKG_ROOT` first:

```bat
echo %VCPKG_ROOT%
dir "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
```

Delete any failed xrhost configure before retrying:

```bat
rmdir /s /q build-xrhost
```

### Recommended CMD command

Use this **single-line command**. This avoids CMD entering the `More?` continuation
prompt when a multi-line command is pasted incorrectly.

```bat
cmake -S xrhost -B build-xrhost -A x64 "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" "-DVCPKG_TARGET_TRIPLET=x64-windows"
```

vcpkg will install the manifest dependencies automatically during configure.
Do **not** run a classic-mode command such as:

```bat
vcpkg install openxr-loader:x64-windows
```

when using the Visual Studio bundled manifest-only vcpkg instance.

After configure succeeds:

```bat
cmake --build build-xrhost --config Release
```

Expected host output:

```text
build-xrhost\bin\Release\
└─ RenegadeVR-xrhost.exe
```

Depending on the vcpkg build, copy the runtime DLLs produced for the x64 host
as well:

```text
openxr_loader.dll
jsoncpp.dll
```

## Test installation

Place the following files beside Renegade's `Game.exe`:

```text
Game.exe
d3d8.dll
RenegadeVR.dll
RenegadeVR.ini
RenegadeVR-xrhost.exe
openxr_loader.dll
jsoncpp.dll
```

Important: the config file must be named exactly:

```text
RenegadeVR.ini
```

not `RenegadeVR(1).ini`, `RenegadeVR(2).ini`, or another renamed copy.

Current default configuration enables hosted head tracking while leaving stereo
rendering and controllers disabled:

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
CameraYawDegrees=0.0
CameraPitchDegrees=0.0
CameraRollDegrees=0.0

[Input]
AutoRestoreMouseFocus=1
```

## Meta Quest 3 / Quest 3S setup

For the first RenegadeVR OpenXR test, use **Meta Horizon Link / Quest Link**.
A wired USB Link connection is recommended for initial debugging because it
removes Wi-Fi/Air Link issues from the test.

### 1. Connect the headset

1. Install and open **Meta Horizon Link** on the PC.
2. Connect the Quest 3/3S to the PC with a USB-C data cable.
3. Put on the headset.
4. Open **Settings** on the headset.
5. Open **Quest Link** and enable it.
6. Select the PC.
7. Select **Launch Quest Link**.

Quest Link makes the Quest behave as a PC VR headset while Link is active.

### 2. Make Meta Horizon Link the active OpenXR runtime

In the Meta Horizon Link PC application:

```text
Settings
  -> General
     -> OpenXR Runtime
        -> Set Meta Horizon Link as active
```

When Meta Horizon Link is already the active OpenXR runtime, the option is
normally disabled/grayed out.

For development/testing, also enable:

```text
Settings
  -> General
     -> Unknown Sources = ON
```

Only one OpenXR runtime can be active at a time. For the first RenegadeVR test,
do not start SteamVR unless you intentionally want SteamVR to own OpenXR.

### 3. Recommended launch order

```text
Quest 3 / Quest 3S ON
        |
        v
Meta Horizon Link running
        |
        v
Quest Link connected
        |
        v
Meta Horizon Link is active OpenXR runtime
        |
        v
Launch Renegade Game.exe
        |
        v
d3d8.dll loads RenegadeVR.dll
        |
        v
RenegadeVR.dll launches RenegadeVR-xrhost.exe
        |
        v
xrhost opens the OpenXR session
        |
        v
head pose is sent back to RenegadeVR
```

Do not launch `RenegadeVR-xrhost.exe` manually during the normal test. The game
plugin launches it automatically.

## Current OpenXR test scope

This milestone is **tracking-only**, not full stereo VR yet.

Expected behavior:

```text
Move head left/right  -> camera yaw
Move head up/down     -> camera pitch
Tilt head             -> camera roll

Mouse                 -> Renegade's normal camera input remains available
HMD                    -> additional tracked head orientation
```

The first valid HMD pose is captured as the neutral orientation so the camera
does not jump to an absolute OpenXR pose when tracking begins.

## Logs to collect

After a test, check or provide these files:

```text
RenegadeVR.log
RenegadeVR-bootstrap.log
RenegadeVR-xrhost.log
```

Useful successful messages include:

```text
VRCore::Initialize - VR=on HeadTracking=on XRHost=on.
VRHostClient: RenegadeVR-xrhost.exe launched.
VRHostClient: state=WaitingForSession
VRHostClient: state=Running
VRHostClient: tracking origin captured; current HMD pose is neutral.
VRHostClient: first live relative pose ...
LIVE_HEAD_POSE applied for first time ...
```

If the host executable or a working OpenXR runtime is unavailable, RenegadeVR
falls back to the normal non-tracked camera rather than intentionally blocking
the game.

## Safety / installation notes

Do **not** replace or modify the Windows system copy of `d3d8.dll`.
The proxy DLL belongs only beside Renegade's `Game.exe`.

Do not commit proprietary Renegade game assets or Steam binaries to this repository.

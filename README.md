# RenegadeVR

Experimental VR plugin scaffold for **Command & Conquer: Renegade (Steam, Windows x86)**.

## Initial architecture

```text
Game.exe
   |
   v
d3d8.dll          <- RenegadeVR D3D8 proxy / bootstrap
   |
   +--> RenegadeVR.dll
   |
   +--> real Direct3D 8 backend / future DXVK backend
```

## Current status

This repository is currently a **development placeholder/scaffold**.

Initial goals:

1. Build a 32-bit `d3d8.dll` proxy.
2. Load `RenegadeVR.dll` from the game directory.
3. Create `RenegadeVR.log`.
4. Forward Direct3D 8 calls without breaking the game.
5. Add VR runtime integration later.
6. Add stereo rendering, HMD tracking, VR input, weapon tracking, hands and IK incrementally.

## Target

- Game: Command & Conquer: Renegade
- Distribution: Steam
- Process architecture: x86 / 32-bit
- Renderer: Direct3D 8 / WW3D
- Build system: CMake
- Language: C++
- Initial testing: single-player/offline

## Build

Use a 32-bit MSVC toolchain:

```bat
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Expected initial outputs:

```text
build/bin/Release/
├─ d3d8.dll
└─ RenegadeVR.dll
```

Do **not** replace or modify the Windows system copy of `d3d8.dll`.
The proxy DLL is intended to be placed only beside Renegade's `Game.exe` during testing.

Do not commit proprietary Renegade game assets or Steam binaries to this repository.

# FO4 Community Shaders

Port of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) to Fallout 4. Permission obtained from doodlum.

Press **END** in game to open the settings menu.

## Features

| Feature | Description |
|---|---|
| MotionVectorFixes | Fixes weapon ghosting, menu ghosting, animated objects, LOD motion vectors |
| Upscaling | DLSS / FSR3 / XeSS with quality modes; replaces engine TAA, dynamic-resolution aware |
| FrameGeneration | DLSS-G / FSR3-FG / XeSS-FG; D3D11/D3D12 interop |
| ScreenSpaceShadows | Sony Bend SSS pipeline + sidecar attenuation pass on the diffuse light buffer. Performance / Quality / Cinematic presets, ENB auto-skip |
| Imagespace | Tonemap (Hable / Reinhard / Lottes), 32³ LUT colour grading, adaptive exposure, HDR bloom, vignette + chromatic aberration + CAS sharpen, Bokeh DOF, sunsprite + lens flare. Subtle / Standard / Vivid / Cinematic presets, suite-wide ENB yield with opt-in stacking |
| PerformanceOverlay | FPS / frametime overlay with 4 presets, four-corner snap or free-drag, Shift+F11 toggle |
| RenderDoc | One-click frame capture from inside the menu |

## Known issues

- **DLSS-G UI ghosting.** DLSS-G interpolates the HUD as part of the scene, which produces mild UI ghosting during fast camera motion. FSR3 mode is unaffected. Investigation ongoing.

## Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (Desktop C++ workload)
- [CMake 3.21+](https://cmake.org/)
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` environment variable set
- [Git](https://git-scm.com/)
- Optional: a sibling `../_tools/` checkout (the shared build/deploy/test harness). Without it, use the raw CMake commands below; `scripts/deploy.sh` and `scripts/test.sh` will not work.

## User Requirements

- [Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)
- [Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)

## Build

```bash
git clone --recursive https://github.com/<owner>/fallout4-community-shaders.git
cd fallout4-community-shaders

# Fetch proprietary SDK runtime DLLs (NVIDIA Streamline, AMD FidelityFX, Intel XeSS)
./scripts/fetch-sdks.sh

cmake -S . --preset=default
cmake --build build --config Release
```

Output: `build/Release/FO4CommunityShaders.dll` plus runtime SDK DLLs staged under `package/F4SE/Plugins/`.

## Deploy

```bash
cp scripts/.env.example scripts/.env
# Edit scripts/.env with your MO2 mod folder + vcpkg path

./scripts/deploy.sh build    # build + deploy
./scripts/deploy.sh deploy   # deploy only (skip build)
```

## Project structure

```
src/                          Core: Feature framework, F4SE entry
features/<Name>/              One subdirectory per feature
extern/                       Submodules: CommonLibF4, FidelityFX-SDK, Streamline, XeSS
include/                      Shared headers (PCH, Detours static lib)
cmake/                        Build config (Common.cmake, Plugin.h.in, Version.rc.in)
package/F4SE/Plugins/         Runtime files staged here for MO2 deployment
scripts/                      deploy.sh, fetch-sdks.sh (added when SDK-using features absorbed)
```

## License

GPL-3.0-or-later with Modding Exception and GPL-3.0 Linking Exception (matching upstream Skyrim Community Shaders). See `LICENSE` and `EXCEPTIONS.md`.

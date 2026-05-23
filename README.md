# FO4 Community Shaders

Port of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders) to Fallout 4. See [PERMISSIONS.md](PERMISSIONS.md) for the upstream-author permission record.

Press **END** in game to open the settings menu.

## Game compatibility

| Runtime | Status |
|---|---|
| OG (1.10.163) | Supported |
| NG (1.10.984) | Supported |
| AE (1.10.980) | Supported |

A single DLL handles all three runtimes via [Address Library](https://www.nexusmods.com/fallout4/mods/47327) IDs. OG/NG/AE share a single binary; nothing per-runtime needs to be selected at install time.

## Features

| Feature | Description |
|---|---|
| MotionVectorFixes | Fixes weapon ghosting, menu ghosting, animated objects, LOD motion vectors |
| Upscaling | DLSS / FSR3 / XeSS with quality modes; replaces engine TAA, dynamic-resolution aware |
| FrameGeneration | DLSS-G / FSR3-FG / XeSS-FG; D3D11/D3D12 interop |
| ScreenSpaceShadows | Sony Bend SSS pipeline + sidecar attenuation pass on the diffuse light buffer. Performance / Quality / Cinematic presets, ENB auto-skip |
| ScreenSpaceGI | GTAO + Spherical-Harmonic indirect-lighting bounce, half/quarter resolution permutations, Performance / Quality / Cinematic presets |
| Imagespace | Tonemap (Hable / Reinhard / Lottes), 32^3 LUT colour grading, adaptive exposure, HDR bloom, vignette + chromatic aberration + CAS sharpen, Bokeh DOF, sunsprite + lens flare. Per-weather profile blending, Subtle / Standard / Vivid / Cinematic presets, suite-wide ENB yield with opt-in stacking |
| PerformanceOverlay | FPS / frametime overlay with 4 presets, four-corner snap or free-drag, Shift+F11 toggle |
| RenderDoc | One-click frame capture from inside the menu |

## Developer tools

| Tool feature | Description |
|---|---|
| ShaderCatalog | Runtime D3D shader inventory with SQLite output and `BSShader::SetupTechnique` attribution |
| ShaderReplacement | Development-only pixel-shader substitution harness for validating reconstructed FO4 shaders |

## Installation

1. Install [Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327) and [Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/) first.
2. Drop the release archive into Mod Organizer 2 (or any mod manager). The shipped layout is the canonical `Data/F4SE/Plugins/...` tree.
3. Launch the game via F4SE. On first launch the plugin writes default `.toml` configs under `Data\F4SE\Plugins\FO4CommunityShaders\<Feature>\`.
4. In game, press **END** to open the settings menu and toggle features.

### ENB coexistence

When ENB is loaded, the plugin auto-yields most effects to ENB so the two don't double-up. Per-feature `force_with_enb` (Imagespace) or feature toggles let you stack specific passes if desired. Streamline-based features (DLSS / DLSS-G / Reflex) skip swap-chain upgrades when ENB is detected, because ENB already owns the swap chain.

### Performance notes

- ScreenSpaceGI: ~168 MB VRAM at 4K in HALF/QUARTER resolution modes (full-res upsample destinations); 0 MB at FULL.
- FrameGeneration: requires hardware support (NVIDIA RTX 40+ for DLSS-G, AMD RDNA3+ for FSR3-FG with hardware path, Intel Arc for XeSS-FG). Effective on a GPU that already runs the game at 60+ fps; below that, latency dominates the perceived smoothness benefit.
- Use the PerformanceOverlay (Shift+F11) to bisect impact when toggling features.

## Known issues

- **DLSS-G UI ghosting.** DLSS-G interpolates the HUD as part of the scene, which produces mild UI ghosting during fast camera motion. FSR3 mode is unaffected. Investigation ongoing.

## Requirements

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (Desktop C++ workload)
- [CMake 3.21+](https://cmake.org/)
- [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` environment variable set
- [Git](https://git-scm.com/)
- Python 3 with Pillow for screenshot-diff smoke scripts
- Optional: a sibling `../_tools/` checkout, or `FALLOUT_TOOLS_DIR` pointing to the shared build/deploy/test harness. Without it, use the raw CMake commands below and mirror `scripts/mod-manifest.toml` manually.

## User Requirements

- [Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)
- [Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)

## Build

```bash
git clone --recursive https://github.com/northaxosky/fallout4-community-shaders.git
cd fallout4-community-shaders

# Fetch proprietary SDK runtime DLLs (NVIDIA Streamline, AMD FidelityFX, Intel XeSS)
./scripts/fetch-sdks.sh

cmake -S . --preset=default
cmake --build build --config Release
```

Output: `build/Release/FO4CommunityShaders.dll` plus runtime SDK DLLs staged under `package/F4SE/Plugins/`.

## Deploy and test

```bash
cp scripts/.env.example scripts/.env
# Edit scripts/.env with your MO2 mod folder + vcpkg path

./scripts/deploy.sh build    # build + deploy
./scripts/deploy.sh deploy   # deploy only (skip build)
./scripts/test.sh            # launch through MO2/F4SE, capture logs + screenshot
```

The deploy and test wrappers delegate to the sibling `../_tools/` harness. If that checkout is missing, use the raw CMake build commands and copy the assets listed in `scripts/mod-manifest.toml`.

Feature smoke scripts live under `scripts/`. They are designed for repeatable agent-driven validation:

| Script | Purpose |
|---|---|
| `smoke-shader-replacement.sh` | Generic OFF/ON ShaderReplacement screenshot diff sweep |
| `smoke-shader-replacement-active-scenes.sh` | Role-focused BSDF/VLS scene validation with ShaderCatalog evidence capture |
| `smoke-imagespace-*.sh` | Imagespace preset / DOF validation |
| `smoke-*.sh` | Feature-specific runtime smoke wrappers |

`smoke-shader-replacement-active-scenes.sh` expects an MO2 profile/save already positioned in the scene being tested. Use `FO4CS_SCENE_COMPOUND_PROFILE`, the per-role profile variables documented by `--help`, or `FO4CS_ACTIVE_SCENE_USE_CURRENT_PROFILE=1`.

## Project structure

```
src/                          Core: Feature framework, F4SE entry
features/<Name>/              One subdirectory per feature
extern/                       Submodules: CommonLibF4, FidelityFX-SDK, Streamline, XeSS
include/                      Shared headers (PCH, Detours static lib)
cmake/                        Build config (Common.cmake, Plugin.h.in, Version.rc.in)
package/F4SE/Plugins/         Static runtime assets and proprietary SDK DLL staging
scripts/                      Build, deploy, smoke, and shader-validation helpers
shaders/                      Reconstructed reference HLSL and shader notes
test-results/                 Ignored runtime validation output
```

## License

GPL-3.0-or-later with Modding Exception and GPL-3.0 Linking Exception (matching upstream Skyrim Community Shaders). See `LICENSE` and `EXCEPTIONS.md`.

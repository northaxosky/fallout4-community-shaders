<div align="center">

# FO4 Community Shaders

**Modern rendering features for Fallout 4 - screen-space lighting - as an open
[F4SE](https://f4se.silverlock.org/) plugin.**

A Fallout 4 port of the ideas in
[Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders),
hooking the DirectX renderer directly.

Like upstream, it **owns the game's deferred shaders** rather than patching them: the deferred
lighting and composite permutations are reconstructed as readable HLSL, compiled at runtime, and
injected in place of the stock shaders.

<br>

[![CI](https://img.shields.io/github/actions/workflow/status/northaxosky/fallout4-community-shaders/pr.yml?branch=main&style=for-the-badge&label=CI&logo=githubactions&logoColor=white)](https://github.com/northaxosky/fallout4-community-shaders/actions/workflows/pr.yml)
[![Version](https://img.shields.io/badge/version-0.2.0%20·%20WIP-orange?style=for-the-badge)](version.txt)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue?style=for-the-badge)](LICENSE)

[![Fallout 4](https://img.shields.io/badge/Fallout%204-1.11.240-3a7d44?style=for-the-badge)](https://www.nexusmods.com/fallout4)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)](#-building-from-source)

<sub>[Features](#features) · [Activation](#feature-activation) · [Controls](#controls) · [In-game Menu](#in-game-menu) · [Building](#building-from-source) · [Compatibility](#compatibility-notes) · [License](#license)</sub>

</div>

> [!WARNING]
> **`0.2.0` is active work in progress.** Features are incomplete, unstable, unavailable on
> some systems, or have startup-only options that require a restart. Ordinary enabled and settings
> changes apply live unless a control says otherwise. There is no packaged release yet; source
> builds are intended for development and testing.

---

## Requirements

| | |
|---|---|
| **Game** | Fallout 4 runtime **1.11.240**. Older `1.10.x` code paths remain in the project but are not currently advertised or validated. |
| **[Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)** | Required. |
| **[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)** | Required. |
| **[Addictol](https://www.nexusmods.com/fallout4/mods/84214)** | Recommended. All-in-one engine patch (stability, performance, bug fixes) by Dear-Modding-FO4 (includes me), the maintainers of the CommonLibF4 fork this plugin builds on. |

---

## Features

> Being listed here means the feature is **compiled into the plugin**, not that it has been
> fully validated in game. Every feature ships **inactive** and is opt-in - see
> [activation](#feature-activation).

| Feature | Implementation |
|---|---|
| **Screen Space Shadows** | Bend screen-space contact/sun shadows via depth raymarch, multiplied into the deferred directional light. |
| **Terrain Shadows** | Long-range worldspace terrain shadowing from an xLODGen heightmap, marched into a shadow-height map and multiplied into the deferred directional light. |
| **Screen Space GI** | XeGTAO screen-space ambient occlusion plus a spherical-harmonic indirect diffuse bounce injected into the ambient/IBL pass. |
| **Inverse Square Lighting** | Configurable interior/exterior inverse-square attenuation for owned deferred opaque punctual lights, with a softened near field. |
| **Exponential Height Fog** | Weather-driven exponential distance extinction and height falloff in Fallout 4's deferred exterior fog composite. |
| **Dynamic Cubemaps** | Scene capture and GGX-prefiltered environment reflections for native deferred IBL, opted-in forward materials, and water. |
| **Wetness Effects** | Rain-driven water film: per-light Fresnel coat, darkened wet albedo, and a wet environment reflection in the deferred lighting and composition passes. |
| **Water Effects** | Animated water caustics projected onto submerged surfaces lit by the sun. |
| **Motion Vector Fixes** | Corrects player and animated-object previous transforms plus frozen/menu or LOD geometry motion. |
| **Upscaling** | Native None/TAA policies plus independently selectable FSR 3 and DLSS super-resolution. |
| **Frame Generation** | Independently selectable FSR 3 and DLSS-G presentation strategies behind one stable D3D11-facing D3D12 proxy. |
| **Performance Overlay** | FPS, frame-time, latency, and backend metrics with configurable layout and graphs. |
| **RenderDoc** | In-game frame-capture controls for an external RenderDoc runtime. |

Motion Vector Fixes does not synthesize first-person weapon motion; the FSR 3 frame-generation
path separately conditions first-person alpha pixels.

---

## Feature activation

Every feature ships inactive and is opt-in. Enable features in `Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.User.toml` or through the in-game menu:

```toml
[features.ScreenSpaceShadows]
load = true
```

Feature loading is evaluated **at startup**—restart the game after enabling or disabling features. Settings for already-loaded features can be adjusted live in-game.

Baseline shader ownership can be enabled under `[shader_ownership]` (`enabled = true`). Its
per-family flags select which reconstructed stock shaders Community Shaders owns. Unsupported
variants continue using the game's native shaders.

---

## Controls

Default hotkeys registered with DearModdingUI:

| Hotkey | Action |
|---|---|
| **F10** | Toggle Performance Overlay |
| **F11** | RenderDoc frame capture |
| **Shift + F11** | RenderDoc multi-frame capture |
| **Ctrl + F12** | Telemetry and diagnostic dump |

Key bindings can be customized through the DearModdingUI menu.

---

## In-game Menu

Community Shaders uses [DearModdingUI](https://github.com/Dear-Modding-FO4/dearmoddingui) for in-game configuration, feature toggles, and the performance overlay.

If DearModdingUI is not installed, Community Shaders operates headless and reads settings directly from the TOML configuration files. See [`src/Host/README.md`](src/Host/README.md) for architecture and integration details.

---

## Building from source

**Prerequisites:** Visual Studio 2026 (Desktop C++), CMake ≥ 4.2, [vcpkg](https://vcpkg.io) with `VCPKG_ROOT` set, and Git.

```bash
git clone --recursive https://github.com/northaxosky/fallout4-community-shaders
cd fallout4-community-shaders

# Download vendor SDK runtimes (Streamline, DLSS, FidelityFX)
pwsh scripts/fetch-sdks.ps1

# Configure and build (Release)
cmake -S . --preset=default
cmake --build build --config Release
```

Tests can be run with `ctest --test-dir build -C Release`. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for full setup and deployment details.

---

## Compatibility notes

- **ENB**: Incompatible. Community Shaders automatically disables itself when ENB is detected.
- **Upscaling & Frame Generation**: Supported on OG (1.10.163), NG (1.10.984), and AE (1.11.240).
  - DLSS requires an NVIDIA RTX GPU and the staged Streamline DLLs.
  - FSR 3 requires D3D11 Feature Level 11.1.
  - Frame Generation requires DX12 support and borderless windowed mode.
  - Switching upscaling or frame generation methods requires a game restart.
  - HDR is currently unsupported.
- **Terrain Shadows**: Requires an xLODGen terrain heightmap export placed in `Data\Textures\Terrain\` or `Data\Textures\HeightMaps\`.
- **RenderDoc**: Requires an external `renderdoc.dll` (API 1.7.0). Enabling frame capture requires a restart.

---

## License

Licensed under **GPL-3.0-or-later** with the project [Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full text.

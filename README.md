<div align="center">

# FO4 Community Shaders

**Modern rendering features for Fallout 4, built as an open
[F4SE](https://f4se.silverlock.org/) plugin.**

Based on [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders).

<br>

[![CI](https://img.shields.io/github/actions/workflow/status/northaxosky/fallout4-community-shaders/pr.yml?branch=main&style=for-the-badge&label=CI&logo=githubactions&logoColor=white)](https://github.com/northaxosky/fallout4-community-shaders/actions/workflows/pr.yml)
[![Version](https://img.shields.io/github/v/release/northaxosky/fallout4-community-shaders?include_prereleases&sort=semver&display_name=tag&style=for-the-badge&label=version)](https://github.com/northaxosky/fallout4-community-shaders/releases)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue?style=for-the-badge)](LICENSE)

[![Fallout 4](https://img.shields.io/badge/Fallout%204-1.11.240-3a7d44?style=for-the-badge)](https://www.nexusmods.com/fallout4)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](xmake.lua)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)](#-building-from-source)

<sub>[Install](#installation) · [Features](#features) · [Activation](#feature-activation) · [Controls](#controls) · [In-game Menu](#in-game-menu) · [Building](#building-from-source) · [Compatibility](#compatibility-notes) · [License](#license)</sub>

</div>

> [!WARNING]
> **Beta.** Features are still being developed and tested.

---

## Requirements

| | |
|---|---|
| **Game** | Fallout 4 runtime **1.11.240**. |
| **[Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)** | Required. |
| **[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)** | Required. |
| **[Addictol](https://www.nexusmods.com/fallout4/mods/84214)** | Recommended. All-in-one engine patch (stability, performance, bug fixes) by Dear-Modding-FO4 (includes me), the maintainers of the CommonLibF4 fork this plugin builds on. |

---

## Installation

Download a package from [Releases](https://github.com/northaxosky/fallout4-community-shaders/releases) and install it with your mod manager. The **Latest** release is stable; the prerelease is the newest development build. Uninstall by removing the mod.

See the [changelog](CHANGELOG.md) for release history, also available in-game under **General > Changelog**.

Report problems through [Issues](https://github.com/northaxosky/fallout4-community-shaders/issues/new/choose) with `Documents\My Games\Fallout4\F4SE\FO4CommunityShaders.log` attached. For questions, join the [Community Shaders Discord](https://discord.com/invite/nkrQybAsyy) and use its [Fallout 4 channel](https://discord.com/channels/1080142797870485606/1553853859972124712).

---

## Features

> Features ship **inactive** and are not all fully validated. See
> [activation](#feature-activation).

| Feature | Purpose |
|---|---|
| **Screen Space Shadows** | Contact shadows and finer shadow detail. |
| **Terrain Shadows** | Long-range shadows from terrain. |
| **Screen Space GI** | Ambient occlusion and indirect lighting. |
| **Inverse Square Lighting** | Opt-in per-light falloff; requires [authored light TOML files](features/InverseSquareLighting/README.md). Existing lights stay unchanged. |
| **[Exponential Height Fog](features/ExponentialHeightFog/README.md)** | Analytic and volumetric fog with weather profiles; provider coverage is pending. |
| **Dynamic Cubemaps** | Reflections that respond to the surrounding scene. |
| **Wetness Effects** | Rain-darkened surfaces and wet reflections. |
| **Water Effects** | Sunlight caustics on submerged surfaces. |
| **Motion Vector Fixes** | Motion-data corrections for temporal rendering. |
| **Upscaling** | TAA, FSR 3/4, and DLSS. |
| **Frame Generation** | FSR 3/4, DLSS, and supported Multi Frame Generation modes. |
| **Performance Overlay** | FPS, frame-time, and latency graphs. |
| **RenderDoc** | In-game frame capture for debugging. |

Terrain Shadows requires an xLODGen heightmap under `Data\Textures\Terrain\<worldspace>\`
or a custom map under `Data\Textures\HeightMaps\`. Maps use the upstream filename/height encoding
contract and native DDS resolution. `[features.TerrainShadows.settings]` uses `EnableTerrainShadow`;
the legacy `enabled` and `downsample_factor` keys are no longer supported.

Screen Space GI settings use upstream-cased keys, including `Enabled`, `EnableGI`,
`EnableExperimentalSpecularGI`, `ResolutionMode`, and the two-number `DepthFadeRange`
array. Legacy snake_case keys are no longer supported. `AOPower` retains the FO4 default
of 4 and edit range 0–12. See [deviations](docs/DEVIATIONS.md#screen-space-gi) for
composition coverage and validation limits.

---

## Feature activation

Every feature starts inactive and is opt-in. Settings live in `Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.toml`, created on first launch (in MO2, under Overwrite) and kept across updates. Uncomment a line to change it, or use the in-game menu:

```toml
[features.ScreenSpaceShadows]
load = true
```

Feature loading is evaluated **at startup**—restart the game after enabling or disabling features. Settings for already-loaded features can be adjusted live in-game.

The menu writes only changed values. Reset restores each setting's commented default.

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

The `component=shader_injection` telemetry line reports the previous completed draw frame in
`draw_frame`: `draw_scope_cpu_us` measures accumulated CPU time inside the injection draw scopes
(including native draw submission), and `draw_scopes`, `draw_captures`, `draw_restores`, and
`draw_d3d_binds` count scopes, state-save calls, restore calls, and injection D3D state-setting calls.
These fields are per-frame; the existing `dispatches` counter is cumulative and counts contributor callbacks.

---

## In-game Menu

Community Shaders uses [DearModdingUI](https://github.com/Dear-Modding-FO4/dearmoddingui) for in-game configuration, feature toggles, and the performance overlay.

Without DearModdingUI, settings can be changed through the TOML configuration files.

Shader ownership follows Skyrim Community Shaders: a supported type is replaced only when its
`[shader_ownership.targets]` toggle is on and `Data\Shaders\{engine fxp name}.hlsl` exists.
All type toggles default on; missing source files leave the native shaders stock. Imagespace effects
use their own engine names, so only `ISSSLRRaytracing.hlsl` currently supplies an imagespace replacement.
The Advanced menu's type toggles and `[shader_ownership] enable_shaders` master are checked at shader bind time.
Off binds the game's shader for that type, including feature changes to it. Feature loading still requires a restart.
Effect, distant tree and forward lighting sources remain for fog and terrain-shadow consumers.
Utility, sky, particle and blood splatter have no replacement sources and remain stock.

---

## Building from source

See [CONTRIBUTING.md](CONTRIBUTING.md) for setup, building, testing, and deployment.

---

## Compatibility notes

- ENB is unsupported.
- Native HDR mods (RenoDX, Special K HDR) are unsupported with Upscaling or Frame Generation and untested otherwise.
- Linux/Proton needs a Windows 10 `d3dcompiler_47.dll` in the prefix's `system32` or next to `Fallout4.exe`; Proton's built-in compiler cannot build the shaders.
- Upscaling requires a DX12-capable GPU. DLSS requires compatible NVIDIA RTX hardware.
- FSR 4 requires compatible AMD Radeon hardware.
- Upscaling (except TAA) and frame generation require borderless windowed mode.
- Supported upscaling and frame-generation methods can change while playing; switching may briefly pause rendering.
- Terrain Shadows requires an xLODGen terrain heightmap export.
- RenderDoc capture requires an installed RenderDoc, found automatically; `dll_path` overrides it.

---

## License

Licensed under **GPL-3.0-or-later** with the project [Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full text.

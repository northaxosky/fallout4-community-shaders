<div align="center">

# FO4 Community Shaders

**Modern rendering features for Fallout 4, built as an open
[F4SE](https://f4se.silverlock.org/) plugin.**

Based on [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders).

<br>

[![CI](https://img.shields.io/github/actions/workflow/status/northaxosky/fallout4-community-shaders/pr.yml?branch=main&style=for-the-badge&label=CI&logo=githubactions&logoColor=white)](https://github.com/northaxosky/fallout4-community-shaders/actions/workflows/pr.yml)
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

Report problems through [Issues](https://github.com/northaxosky/fallout4-community-shaders/issues/new/choose) with `Documents\My Games\Fallout4\F4SE\FO4CommunityShaders.log` attached.

---

## Features

> Features ship **inactive** and are not all fully validated. See
> [activation](#feature-activation).

| Feature | Purpose |
|---|---|
| **Screen Space Shadows** | Contact shadows and finer shadow detail. |
| **Terrain Shadows** | Long-range shadows from terrain. |
| **Screen Space GI** | Ambient occlusion and indirect lighting. |
| **Inverse Square Lighting** | More natural light falloff. |
| **Exponential Height Fog** | Weather-driven fog with height falloff. |
| **Dynamic Cubemaps** | Reflections that respond to the surrounding scene. |
| **Wetness Effects** | Rain-darkened surfaces and wet reflections. |
| **Water Effects** | Sunlight caustics on submerged surfaces. |
| **Motion Vector Fixes** | Motion-data corrections for temporal rendering. |
| **Upscaling** | TAA, FSR 3/4, and DLSS. |
| **Frame Generation** | FSR 3/4, DLSS, and supported Multi Frame Generation modes. |
| **Performance Overlay** | FPS, frame-time, and latency graphs. |
| **RenderDoc** | In-game frame capture for debugging. |

---

## Feature activation

Every feature ships inactive and is opt-in. Enable features in `Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.User.toml` or through the in-game menu:

```toml
[features.ScreenSpaceShadows]
load = true
```

Feature loading is evaluated **at startup**—restart the game after enabling or disabling features. Settings for already-loaded features can be adjusted live in-game.

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

Without DearModdingUI, settings can be changed through the TOML configuration files.

---

## Building from source

See [CONTRIBUTING.md](CONTRIBUTING.md) for setup, building, testing, and deployment.

---

## Compatibility notes

- ENB and HDR are unsupported.
- Upscaling requires a DX12-capable GPU. DLSS requires compatible NVIDIA RTX hardware.
- FSR 4 requires compatible AMD Radeon hardware.
- Frame generation requires borderless windowed mode.
- Supported upscaling and frame-generation methods can change while playing; switching may briefly pause rendering.
- Terrain Shadows requires an xLODGen terrain heightmap export.
- RenderDoc capture requires an external RenderDoc runtime.

---

## License

Licensed under **GPL-3.0-or-later** with the project [Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full text.

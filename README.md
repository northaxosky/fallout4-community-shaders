<div align="center">

# FO4 Community Shaders

**Modern rendering features for Fallout 4 - upscaling, frame generation, screen-space
lighting, and post-processing - as an open [F4SE](https://f4se.silverlock.org/) plugin.**

A Fallout 4 port of the ideas in
[Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders),
hooking the DirectX renderer directly.

<br>

[![CI](https://img.shields.io/github/actions/workflow/status/northaxosky/fallout4-community-shaders/pr.yml?branch=main&style=for-the-badge&label=CI&logo=githubactions&logoColor=white)](https://github.com/northaxosky/fallout4-community-shaders/actions/workflows/pr.yml)
[![Version](https://img.shields.io/badge/version-0.2.0%20·%20WIP-orange?style=for-the-badge)](version.txt)
[![License](https://img.shields.io/badge/license-GPL--3.0--or--later-blue?style=for-the-badge)](LICENSE)

[![Fallout 4](https://img.shields.io/badge/Fallout%204-1.11.221-3a7d44?style=for-the-badge)](https://www.nexusmods.com/fallout4)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](CMakeLists.txt)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white)](#-building-from-source)

<sub>[Features](#features) · [Activation](#feature-activation) · [Controls](#controls) · [Building](#building-from-source) · [Compatibility](#compatibility-notes) · [License](#license)</sub>

</div>

> [!WARNING]
> **`0.2.0` is active work in progress.** Features are incomplete, unstable, unavailable on
> some systems, or require a restart after a settings change. There is no packaged release yet;
> source builds are intended for development and testing.

---

## Requirements

| | |
|---|---|
| **Game** | Fallout 4 runtime **1.11.221**. Older `1.10.x` code paths remain in the project but are not currently advertised or validated. |
| **[Fallout 4 Script Extender (F4SE)](https://f4se.silverlock.org/)** | Required. |
| **[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327)** | Required. |
| **[Addictol](https://www.nexusmods.com/fallout4/mods/84214)** | Recommended. All-in-one engine patch (stability, performance, bug fixes) by Dear-Modding-FO4 (includes me), the maintainers of the CommonLibF4 fork this plugin builds on. |

---

## Features

> Being listed here means the feature is **compiled into the plugin**, not that it has been
> fully validated in game. Every feature ships **inactive** and is opt-in - see
> [activation](#-feature-activation).

| Feature | Implementation |
|---|---|
| **Motion Vector Fixes** | Repairs player, weapon, menu, animated-object, and LOD motion-vector paths. |
| **Upscaling** | DLSS and FSR3 upscaling or native AA, with quality modes and sharpening controls. XeSS upscaling is not implemented. |
| **Frame Generation** | FSR3-FG, DLSS-G, and XeSS-FG through D3D11/D3D12 interop. |
| **Screen Space Shadows** | Bend screen-space contact/sun shadows via depth raymarch, multiplied into the deferred directional light. |
| **Screen Space GI** | XeGTAO screen-space ambient occlusion plus a spherical-harmonic indirect diffuse bounce injected into the ambient/IBL pass. |
| **Imagespace** | Tonemapping, exposure, bloom, 32³ LUT grading, vignette, chromatic aberration, sharpening, lens effects, depth of field, and weather profiles. |
| **Performance Overlay** | FPS, frame-time, latency, and backend metrics with configurable layout and graphs. |
| **RenderDoc** | In-game frame-capture controls for an external RenderDoc runtime. |

<details>
<summary><b>Developer tools</b></summary>

<br>

| Feature | Implementation |
|---|---|
| **Shader Catalog** | Records D3D11 shader creation and known `BSShader` attribution to SQLite. |
| **Shader Replacement** | Replaces selected pixel shaders with reconstructed HLSL for validation. |

</details>

---

## Feature activation

Every feature is opt-in. Add its exact key to
`Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.User.toml`:

```toml
[features.Imagespace]
load = true
```

Activation is evaluated **once during startup**, so restart the game after changing it. The
shipped `FO4CommunityShaders.toml` holds documented defaults and is never modified by the
plugin; user and in-game changes are written atomically to `FO4CommunityShaders.User.toml`. The
in-game menu can update a feature's requested load state for the next launch and shows its
runtime state, but it does **not** hot-load or unload features.

A feature loads only when its `[features.<Name>].load` value is `true`. A malformed Default file
disables all features; a malformed User file is ignored. Presets configure only features that
are already activated - they cannot activate Imagespace or any other feature.

---

## Controls

| Key | Action |
|:---:|---|
| **End** | Open or close the settings menu |
| **F10** | Toggle the Performance Overlay when it is enabled |
| **F11** | Capture one frame when RenderDoc is enabled |
| **Shift + F11** | Capture multiple frames when RenderDoc is enabled |

Feature hotkeys are configurable in the unified TOML: `toggle_hotkey` for the overlay,
`capture_hotkey` and `multi_capture_hotkey` for RenderDoc. Set a key to `"none"` to unbind it.
Feature configuration lives directly under `Data\F4SE\Plugins\FO4CommunityShaders\`; supporting
assets (Imagespace LUTs, shaders, presets) live in subdirectories beneath it.

---

## Building from source

**Prerequisites:** Visual Studio 2026 (Desktop C++), CMake ≥ 3.21, [vcpkg](https://vcpkg.io)
with `VCPKG_ROOT` set, and Git.

```bash
# Clone with submodules (CommonLibF4, FidelityFX, Streamline, XeSS)
git clone --recursive https://github.com/northaxosky/fallout4-community-shaders
cd fallout4-community-shaders

# Fetch the proprietary SDK runtime DLLs (not vendored in the repo)
pwsh scripts/fetch-sdks.ps1

# Configure + build (Release)
cmake -S . --preset=default
cmake --build build --config Release        # -> build/Release/FO4CommunityShaders.dll
```

Built on [CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4). C++23, `/W4 /WX`
(warnings are errors). Run the tests with `ctest --test-dir build -C Release`.

---

## Compatibility notes

- **Frame generation** requires a D3D12 feature-level 12.0 device and the runtime files for the
  selected backend; backend availability depends on the device and SDK. Changing the FG backend
  or multiplier requires a restart. RenderDoc, Shader Catalog, and Shader Replacement also
  initialize at startup.
- **ENB** handling is feature-specific: Imagespace yields to ENB unless explicitly forced,
  Upscaling is limited to Native AA, DLSS-G is not used under ENB, and frame generation may fall
  back to FSR3-FG when that runtime is available.
- **Imagespace** does not run CAS sharpening while chromatic aberration is active.
- **RenderDoc** requires an external `renderdoc.dll` exposing API 1.7.0 and is incompatible with
  DLSS-G for that session.
- A successful build or launch does **not** prove a rendering path is visually correct - in-game
  validation is still required.

---

## License

Licensed under **GPL-3.0-or-later** with the project
[Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full text.

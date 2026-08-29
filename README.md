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

<sub>[Features](#features) · [Activation](#feature-activation) · [Controls](#controls) · [Shared menu](#shared-mod-menu) · [Building](#building-from-source) · [Compatibility](#compatibility-notes) · [License](#license)</sub>

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
> [activation](#-feature-activation).

| Feature | Implementation |
|---|---|
| **Screen Space Shadows** | Bend screen-space contact/sun shadows via depth raymarch, multiplied into the deferred directional light. |
| **Screen Space GI** | XeGTAO screen-space ambient occlusion plus a spherical-harmonic indirect diffuse bounce injected into the ambient/IBL pass. |
| **Wetness Effects** | Rain-driven water film: per-light Fresnel coat, darkened wet albedo, and a wet environment reflection in the deferred lighting and composition passes. |
| **Motion Vector Fixes** | Corrects player and animated-object previous transforms plus frozen/menu or LOD geometry motion. |
| **Upscaling** | DLSS and FSR 3 super-resolution, TAA, and AMD FSR 3 frame generation through a D3D11-facing D3D12 proxy. |
| **Performance Overlay** | FPS, frame-time, latency, and backend metrics with configurable layout and graphs. |
| **RenderDoc** | In-game frame-capture controls for an external RenderDoc runtime. |

Motion Vector Fixes does not synthesize first-person weapon motion; the FSR 3 frame-generation
path separately conditions first-person alpha pixels.

---

## Feature activation

Every feature is opt-in. Add its exact key to
`Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.User.toml`:

```toml
[features.ScreenSpaceShadows]
load = true
```

Activation is evaluated **once during startup**, so restart the game after changing it. The
shipped `FO4CommunityShaders.toml` holds documented defaults and is never modified by the
plugin; user and in-game changes are written atomically to `FO4CommunityShaders.User.toml`. The
in-game menu can update a feature's requested load state for the next launch and shows its
runtime state, but it does **not** hot-load or unload features.

A feature loads only when its `[features.<Name>].load` value is `true`. A malformed Default file
disables all features; a malformed User file is ignored. Presets configure only features that
are already activated - they cannot activate any feature.

Baseline shader ownership is separately opt-in and does not load a feature. Loaded features may
request the reconstructed routes they need independently. Every replacement still requires the
stock shader's SHA-1 match. Set `enabled = true` under `[shader_ownership]` in the User TOML to
replace the remaining deferred targets with their stock-equivalent HLSL; the per-target switches in
the shipped Default remain available for bring-up opt-outs. Restart after changing ownership.

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
The menu's own keys live under `[menu]` as `toggle_key` and `overlay_toggle_key` and are
rebindable from the menu's Keybindings tab.

When a shared mod menu hosts the settings (see [shared menu](#shared-mod-menu)), **End** is that
host's key, not this plugin's: the host owns opening and closing the common menu, its own binding,
and the theme and fonts it draws with. The overlay and RenderDoc hotkeys above are unaffected.

Feature configuration lives directly under `Data\F4SE\Plugins\FO4CommunityShaders\`; supporting
assets live in subdirectories beneath it:

| Directory | Contents |
|---|---|
| `Fonts\<Family>\` | Menu fonts, one folder per family; selectable per typography role |
| `Themes\` | Importable menu theme presets as `<Name>.toml`; the unified User TOML records only your edits to the shipped theme |
| `Icons\` | Optional action and category icons from [Phosphor](https://github.com/phosphor-icons/core), MIT; see [Icons/LICENSE](package/F4SE/Plugins/FO4CommunityShaders/Icons/LICENSE) |
| `Presets\` | Cross-feature setting presets |

---

## Shared mod menu

Community Shaders can draw its settings inside a shared Dear-Modding mod menu instead of its own
window. This is entirely optional and needs no configuration: at startup the plugin looks for any
loaded module exposing the neutral `DearModdingUI` client ABI, and joins the first compatible one.
[Addictol](https://www.nexusmods.com/fallout4/mods/84214) implements that ABI, but no host is
required and none is depended on.

When a host takes over, the home, general, advanced, presets, and per-feature pages appear in the
common menu under **Community Shaders**, and that menu's own key opens and closes it. The host owns
the window, theme, and fonts, so this plugin's own interface and menu-key settings do not apply
while hosted. Feature hotkeys, the performance overlay, and RenderDoc capture keep working exactly
as they do standalone.

With no compatible host loaded - or if registration or backend initialization fails before
readiness - Community Shaders opens its own menu with **End**, unchanged. Both sides must be built
against the same pinned Dear ImGui; a mismatch is refused and the standalone menu is used.
Developer details live in [`src/Host/README.md`](src/Host/README.md).

---

## Building from source

**Prerequisites:** Visual Studio 2026 (Desktop C++), CMake ≥ 3.21, [vcpkg](https://vcpkg.io)
with `VCPKG_ROOT` set, and Git.

```bash
# Clone with submodules (CommonLibF4, FidelityFX-SDK, Streamline)
git clone --recursive https://github.com/northaxosky/fallout4-community-shaders
cd fallout4-community-shaders

# Stage the pinned Streamline, DLSS, and FidelityFX frame-generation runtime DLLs
pwsh scripts/fetch-sdks.ps1

# Configure + build (Release)
cmake -S . --preset=default
cmake --build build --config Release        # -> build/Release/FO4CommunityShaders.dll
```

Built on [CommonLibF4](https://github.com/Dear-Modding-FO4/commonlibf4). C++23, `/W4 /WX`
(warnings are errors). Run the tests with `ctest --test-dir build -C Release`.

---

## Compatibility notes

- **ENB is not supported.** Features whose effects overlap ENB - Screen Space Shadows, Screen
  Space GI, Wetness Effects, and Upscaling - deactivate themselves when ENB is loaded. The
  remaining features still run, but the combination is untested.
- **Upscaling** engine anchors are proven for the NG and AE runtimes only; the feature refuses to
  load on OG (1.10.163). DLSS needs the staged Streamline runtime DLLs. AMD FSR 3 frame generation
  needs the staged FidelityFX 3.1.4 DX12 DLLs, windowed or borderless SDR
  `R8G8B8A8_UNORM` output, and a restart after startup-policy changes. It is independent of the
  selected super-resolution method and defaults off in pause, main, loading, and Pip-Boy menus.
- **Motion Vector Fixes** installs its player-transform hook on every runtime, but the
  animation-sequence correction is unproven on OG (1.10.163) and is skipped there.
- **Screen Space GI** temporal reprojection reads the RT 29 motion-vector target, which carries
  render-resolution motion in the upper-left sub-rect while upscaling is active.
- **RenderDoc** requires an external `renderdoc.dll` exposing API 1.7.0. It initializes at startup,
  so enabling it requires a restart. Capturing an FSR3 dispatch can destabilise that dispatch;
  disable capture before diagnosing FSR3 crashes.
- A successful build or launch does **not** prove a rendering path is visually correct - in-game
  validation is still required.

---

## License

Licensed under **GPL-3.0-or-later** with the project
[Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full text.

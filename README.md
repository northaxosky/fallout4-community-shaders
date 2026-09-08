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
| **Terrain Shadows** | Long-range worldspace terrain shadowing from an xLODGen heightmap, marched into a shadow-height map and multiplied into the deferred directional light. |
| **Screen Space GI** | XeGTAO screen-space ambient occlusion plus a spherical-harmonic indirect diffuse bounce injected into the ambient/IBL pass. |
| **Inverse Square Lighting** | Configurable interior/exterior inverse-square attenuation for owned deferred opaque punctual lights, with a softened near field. |
| **Exponential Height Fog** | Weather-driven exponential distance extinction and height falloff in Fallout 4's deferred exterior fog composite. |
| **Dynamic Cubemaps** | Scene capture and GGX-prefiltered environment reflections for native deferred IBL, opted-in forward materials, and water. |
| **Wetness Effects** | Rain-driven water film: per-light Fresnel coat, darkened wet albedo, and a wet environment reflection in the deferred lighting and composition passes. |
| **Water Effects** | Animated water caustics projected onto submerged surfaces lit by the sun. |
| **Motion Vector Fixes** | Corrects player and animated-object previous transforms plus frozen/menu or LOD geometry motion. |
| **Upscaling** | Native None/TAA policies plus independently selectable FSR 3, DLSS, and XeSS super-resolution. XeSS uses native D3D11 on supported Intel adapters and a same-adapter D3D12 bridge elsewhere. |
| **Frame Generation** | Independently selectable FSR 3, DLSS-G, and XeSS-FG presentation strategies behind one stable D3D11-facing D3D12 proxy. |
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

Community Shaders registers its controls with DearModdingUI. The shipped suggestions are **F10**
for the Performance Overlay, **F11** for one RenderDoc capture, **Shift + F11** for a multi-frame
capture, and **Ctrl + F12** for a telemetry dump. The host owns binding overrides, conflict
resolution and key-up pairing. Capture and overlay actions yield while the host owns input;
the telemetry dump action remains available while the menu is open.

The feature TOML strings (`toggle_hotkey`, `capture_hotkey`, `multi_capture_hotkey`, and
`logging.dump_hotkey`) remain suggested defaults. An explicit user
`features.PerformanceOverlay.settings.toggle_hotkey` wins over the retired menu binding; otherwise
the legacy integer/keyboard-array `menu.overlay_toggle_key` is converted once into the host's
suggested chord, including an explicit `0` (unbound). The host's saved override is authoritative
after registration. With no compatible host, these diagnostic hotkeys are intentionally
unavailable.

Feature settings can expose persisted texture previews and fullscreen debug views. Texture
previews are independent; one fullscreen view can replace the scene at a time. Terrain Shadows
provides fullscreen grayscale views of its sampled shadow term and raw heightmap. Inverse Square
Lighting provides a live left-vanilla/right-configured comparison. Exponential Height Fog exposes
the final fog factor as pre-colour-mix greyscale.

Skylighting's raw and normalized depth previews share one still snapshot. Switching between them
does not recapture; **Refresh snapshot** updates both from the next completed occlusion map.
Normalization reads the saved depth copy, not the live producer. The lighting algorithm continues
sampling normally while both previews stay still.

Upscaling and Frame Generation previews follow the same frozen-snapshot contract. Selecting a
preview captures the next completed render-stage resource into a plugin-owned texture; **Refresh
snapshot** requests one later capture while the previous image remains available. DearModdingUI
never receives a writable engine or vendor-owned temporal resource.

Feature configuration lives directly under `Data\F4SE\Plugins\FO4CommunityShaders\`; supporting
assets live in subdirectories beneath it:

| Directory | Contents |
|---|---|
| `Presets\` | Cross-feature setting presets |

---

## DearModdingUI

Community Shaders is a forwarding-only DearModdingUI client. At startup it uses the official
header-only client to discover and preflight a compatible host, then registers Home, Advanced,
and Presets under General, alongside per-feature settings and the Performance Overlay. The host owns the window,
navigation, rendering backend, fonts, theme, input, hotkey editor, dialogs, notifications, and
overlay placement.

Community Shaders does not compile or link Dear ImGui and never creates or shares an ImGui context.
Forwarding avoids shared ImGui layouts, but the official client still verifies the generated
ImGui forwarding binary version and the required forwarding surface. If the host is
missing, disabled, incompatible, or cannot provide every required native service, Community
Shaders continues running its shader features, presets, TOML configuration, telemetry, and
fullscreen debug selection headless. It deliberately provides no fallback menu, overlay,
notification UI, or diagnostic hotkeys in that state.

Settings pages use the host's native section, settings-table, settings-row, reset, search, status
color, dialog, and link services. Feature widgets remain live and persist with their existing
per-feature rules inside those native rows.

Home shows startup loading results. Advanced has the sole **Load on startup** list, where checked
means loaded on the next launch; changes require a restart. A loaded feature's **Enabled** control
toggles its effect live. Shader ownership, cache, and logging settings are also in Advanced.
Folder actions resolve the backing file before opening Explorer, rather than passing it an
MO2 USVFS path that exists only inside the game.

Developer details live in [`src/Host/README.md`](src/Host/README.md).

---

## Building from source

**Prerequisites:** Visual Studio 2026 (Desktop C++), CMake ≥ 4.2, [vcpkg](https://vcpkg.io)
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

- **ENB is not supported.** Every feature deactivates when ENB is loaded.
- **Upscaling and Frame Generation** engine anchors are proven for the NG and AE runtimes only;
  both features refuse to load on OG (1.10.163). FSR 3 super-resolution requires D3D11 feature
  level 11.1. Loaded Upscaling sessions request that level regardless of the initial provider,
  retaining lower-level device fallbacks but not admitting FSR on those devices.
  DLSS super-resolution needs the staged Streamline runtime DLLs.
  If an admitted external super-resolution evaluation or publication fails after
  reduced-resolution rendering commits, the plugin performs its own display-sized linear spatial
  resolve from the retained engine input before continuing to UI. The live `DrawWorld::Render_UI`
  wrapper also validates and reads its RIP-relative effects-path gate at entry; Gamma-only calls
  that bypass the normal `+0xC5` seam inspect the resulting viewport, preserve full-size output
  through a private passthrough, or spatially resolve a committed render subrect before publication.
  Provider-only recovery switches to native TAA on the next frame. Engine or shared-runtime
  failures disable the affected temporal consumers until restart; their requested selections remain
  visible separately from effective state. Recovery resources and shaders are preflighted before
  reduced-resolution state is committed.
  AMD FSR 3 frame generation needs the staged FidelityFX 3.1.4 DX12 DLLs, windowed or
  borderless SDR `R8G8B8A8_UNORM` output, and a restart after startup-policy changes. It is
  independent of the selected super-resolution method and defaults off in pause, main, loading,
  and Pip-Boy menus. Its current UI strategy captures HUD-less post-imagespace color before the
  engine composites UI. DLSS-G uses the same Streamline D3D12 session and frame token as
  DLSS-SR, while XeSS-FG connects XeLL before its swap-chain initialization. Both use the
  captured HUD-less image with the intercepted final backbuffer; neither fabricates a UI-alpha
  layer.
  The validated normal-loop hooks track sleep, simulation, render-submit, and proxy Present
  attempt ordering without treating worker completion or auxiliary Swap callers as frame boundaries.
  The active DLSS-G or XeSS-FG provider alone receives the matching Reflex/PCL or XeLL sleep and
  marker sequence. The observed SR input is post-tonemap gamma-2.2 output with the artistic LUT
  already applied; XeSS decodes that transfer into FP16 linear color, evaluates at exposure 1.0,
  and re-encodes gamma-2.2 before publication without reversing the LUT. HDR and ENB are unsupported.
  The two feature panels own configuration and diagnostics only. Core-owned temporal rendering,
  input capture, recovery, provider execution, and presentation remain available independently of
  either panel's lifecycle; an unloaded Upscaling panel does not own Frame Generation's resources.
- **Motion Vector Fixes** installs its player-transform hook on every runtime, but the
  animation-sequence correction is unproven on OG (1.10.163) and is skipped there.
- **Screen Space GI** temporal reprojection reads the RT 29 motion-vector target, which carries
  render-resolution motion in the upper-left sub-rect while upscaling is active.
- **Terrain Shadows** needs a worldspace heightmap on disk; the plugin generates none. Drop an
  FO4 xLODGen beta 132 export at
  `Data\Textures\Terrain\<Worldspace>\<Worldspace>.Terrain.HeightMap.<W>.<S>.<E>.<N>.<minZ>.<maxZ>.dds`,
  or an upstream-style custom map at
  `Data\Textures\HeightMaps\<Worldspace>.HeightMap.<W>.<S>.<E>.<N>.<zBlack>.<zWhite>.<minZ>.<maxZ>.dds`,
  which takes precedence. Worldspaces without a map render unchanged.
- **RenderDoc** requires an external `renderdoc.dll` exposing API 1.7.0. It initializes at startup,
  so enabling it requires a restart. Capturing an FSR3 dispatch can destabilise that dispatch;
  disable capture before diagnosing FSR3 crashes.
- A successful build or launch does **not** prove a rendering path is visually correct - in-game
  validation is still required.

---

## License

Licensed under **GPL-3.0-or-later** with the project
[Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full text.

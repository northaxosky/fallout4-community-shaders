# FO4 Community Shaders

An experimental [F4SE](https://f4se.silverlock.org/) plugin that ports ideas from
[Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)
to Fallout 4. It hooks the DirectX renderer to provide upscaling, frame generation,
post-processing, diagnostics, and shader-development tools.

> [!WARNING]
> Version `0.2.0` is active work in progress. Features are incomplete, unstable,
> unavailable on some systems, or require a restart after settings change. The
> repository does not currently publish a packaged release; source builds are intended
> for development and testing.

## Compatibility

The current plugin metadata targets Fallout 4 runtime **1.11.221**. Older `1.10.x`
runtime-specific code paths remain in the project, but they are not currently
advertised or validated as supported.

[Fallout 4 Script Extender](https://f4se.silverlock.org/) and
[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327) are required.

## Current features

Being listed here means the feature is compiled into the plugin, not that it has been
fully validated in game.

| Feature | Current implementation | Packaged state |
|---|---|---|
| Motion Vector Fixes | Repairs player, weapon, menu, animated-object, and LOD motion-vector paths | Inactive |
| Upscaling | DLSS and FSR3 upscaling or native AA, with quality modes and sharpening controls. XeSS upscaling is not implemented | Inactive |
| Frame Generation | FSR3-FG, DLSS-G, and XeSS-FG through D3D11/D3D12 interop | Inactive |
| Screen Space Shadows | Bend screen-space contact/sun shadows via depth raymarch, multiplied into the deferred directional light | Inactive |
| Screen Space GI | XeGTAO screen-space ambient occlusion plus a spherical-harmonic indirect diffuse bounce injected into the ambient/IBL pass | Inactive |
| Imagespace | Tonemapping, exposure, bloom, 32^3 LUT grading, vignette, chromatic aberration, sharpening, lens effects, depth of field, and weather profiles | Inactive |
| Performance Overlay | FPS, frame-time, latency, and backend metrics with configurable layout and graphs | Inactive |
| RenderDoc | In-game frame-capture controls for an external RenderDoc runtime | Inactive |

### Developer tools

| Feature | Current implementation | Packaged state |
|---|---|---|
| Shader Catalog | Records D3D11 shader creation and known `BSShader` attribution to SQLite | Inactive |
| Shader Replacement | Replaces selected pixel shaders with reconstructed HLSL for validation | Inactive |

## Feature activation

Every feature is opt-in. Add its exact key to
`Data\F4SE\Plugins\FO4CommunityShaders\FO4CommunityShaders.User.toml`:

```toml
[features.Imagespace]
load = true
```

Activation is evaluated once during startup. The shipped `FO4CommunityShaders.toml`
contains documented defaults and is never modified by the plugin. User and in-game changes
are written atomically to `FO4CommunityShaders.User.toml`. Restart the game after changing
activation. The in-game menu can update each registered feature's requested load state for
the next launch and shows its runtime state, but it does not hot-load or unload features.

A feature loads only when its `[features.<Name>].load` value is true. A malformed Default
file disables all features; a malformed User file is ignored. Presets configure only
features that were already activated; they cannot activate Imagespace or another feature.

## Controls

| Key | Action |
|---|---|
| **End** | Open or close the settings menu |
| **F10** | Toggle the Performance Overlay when it is enabled |
| **F11** | Capture one frame when RenderDoc is enabled |
| **Shift+F11** | Capture multiple frames when RenderDoc is enabled |

Feature hotkeys are configurable in the unified TOML: `toggle_hotkey` for the overlay,
`capture_hotkey` and `multi_capture_hotkey` for RenderDoc. Set a key to `"none"` to unbind it.

Feature configuration files are stored directly under
`Data\F4SE\Plugins\FO4CommunityShaders\`. Supporting assets such as Imagespace LUTs,
shaders, and presets live in subdirectories beneath that path.

## Compatibility notes

- Frame generation requires a D3D12 feature-level 12.0 device and the runtime files
  for the selected backend. Backend availability depends on the device and SDK.
- Changing the frame-generation backend or multiplier requires a restart. RenderDoc,
  Shader Catalog, and Shader Replacement are also initialized at startup.
- ENB handling is feature-specific. Imagespace yields to ENB unless explicitly forced,
  Upscaling is limited to Native AA, and DLSS-G is not used under ENB. Frame generation
  may fall back to FSR3-FG when that runtime is available.
- Imagespace does not run CAS sharpening while chromatic aberration is active.
- RenderDoc requires an external `renderdoc.dll` exposing API 1.7.0 and is incompatible
  with DLSS-G for that session.
- A successful build or launch does not prove that a rendering path is visually correct.
  In-game validation is still required.

## License

Licensed under GPL-3.0-or-later with the project
[Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full
license text.

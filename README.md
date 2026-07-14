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

The current plugin metadata targets Fallout 4 runtime **1.11.191**. Older `1.10.x`
runtime-specific code paths remain in the project, but they are not currently
advertised or validated as supported.

[Fallout 4 Script Extender](https://f4se.silverlock.org/) and
[Address Library for F4SE](https://www.nexusmods.com/fallout4/mods/47327) are required.

## Current features

Being listed here means the feature is compiled into the plugin, not that it has been
fully validated in game.

| Feature | Current implementation | Initial state |
|---|---|---|
| Motion Vector Fixes | Repairs player, weapon, menu, animated-object, and LOD motion-vector paths | Always active |
| Upscaling | DLSS and FSR3 upscaling or native AA, with quality modes and sharpening controls. XeSS upscaling is not implemented | DLSS Quality |
| Frame Generation | FSR3-FG, DLSS-G, and XeSS-FG through D3D11/D3D12 interop | FSR3-FG, 2x |
| Imagespace | Tonemapping, exposure, bloom, 32^3 LUT grading, vignette, chromatic aberration, sharpening, lens effects, depth of field, and weather profiles | Enabled, Standard style |
| Performance Overlay | FPS, frame-time, latency, and backend metrics with configurable layout and graphs | Disabled |
| RenderDoc | In-game frame-capture controls for an external RenderDoc runtime | Disabled |

### Developer tools

| Feature | Current implementation | Initial state |
|---|---|---|
| Shader Catalog | Records D3D11 shader creation and known `BSShader` attribution to SQLite | Disabled; restart required |
| Shader Replacement | Replaces selected pixel shaders with reconstructed HLSL for validation | Disabled; restart required |

## Controls

| Key | Action |
|---|---|
| **End** | Open or close the settings menu |
| **Shift+F11** | Toggle the Performance Overlay when it is enabled |
| **F11** | Capture one frame when RenderDoc is enabled |
| **Shift+F11** | Capture multiple frames when RenderDoc is enabled; this takes priority over the overlay shortcut |

Feature configuration files are stored directly under
`Data\F4SE\Plugins\FO4CommunityShaders\`. Supporting assets such as Imagespace LUTs,
shaders, and presets live in subdirectories beneath that path.

## Presets

The preset framework currently captures **Imagespace only**. Five read-only builtins
ship under `Presets\Builtin\`: `Default`, `Cinematic-Night`, `Neutral-Realistic`,
`Reactor-Inspired`, and `Vivid-Daylight`. User presets are stored directly under
`Presets\`.

Presets are managed from the header at the top of the in-game menu. A selected preset
is restored on the next launch only when **Auto-load on boot** is enabled. See
[Preset documentation](docs/Presets.md) for paths, controls, and the TOML schema.

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

## Building and testing

There is no install or archive target yet. See [CONTRIBUTING.md](CONTRIBUTING.md) for
the supported toolchains, recursive clone and Git LFS setup, build and test commands,
runtime SDK staging, and optional devkit deployment.

## License

Licensed under GPL-3.0-or-later with the project
[Modding and Linking Exceptions](EXCEPTIONS.md). See [LICENSE](LICENSE) for the full
license text.

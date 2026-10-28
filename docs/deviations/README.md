# Deviations from upstream

Differences between the FO4 port and [Skyrim Community Shaders][upstream], one file per feature.
Only output, user-visible behavior and unported or N/A upstream functionality are recorded.
Classification rules live in [FEATURES](../FEATURES.md#classification).

| Kind | Meaning |
|---|---|
| Forced | A FO4 engine difference leaves no alternative. |
| Divergence | FO4-specific design or behavior upstream lacks. |
| Pending | Upstream functionality not ported yet. |
| N/A | Upstream functionality with no FO4 counterpart. |

## Cross-cutting

| Kind | Upstream | FO4 | Why |
|---|---|---|---|
| Forced | World shading in forward `Lighting` passes | Deferred prepass, light, composite and tiled families (`DFPrepass`, `DFLight`, `DFComposite`, `DFTiledLighting`) host the consumers | FO4 world draws emit no forward BSLighting passes |
| Forced | FrameBuffer at b12 | FrameBuffer binds at b4 | Reconstructed engine shaders own b12 |
| Forced | Single perspective depth | One R32_FLOAT canonical world-projection depth at t17 | Native depth combines first-person and world projections |
| Forced | Ambient SH projects sRGB-decoded DALC axis colors | Axis colors decode with power 2.2; vanilla ambient replays from b7 DALC rows | Native lighting evaluates DALC with power 2.2 |
| Forced | Linear Lighting is optional | Shaders always compile with `ENABLE_LL`; b6 publishes identity diffuse decode and no pi on sun radiance | FO4 deferred HDR and sun inputs are already linear |
| Divergence | b7 ISL block; zero `cubemapCreatorSettings` semantics | One FO4-only 112-byte b7 holds debug-view state, `DeltaTime`, per-feature enable flags and DALC rows; b6 `cubemapCreatorSettings` stays zero | Single-substrate ABI |
| Forced | `Util::IsInterior()`: interior cell, or `kNoSky`/`kFixedDimensions` worldspace | `engine::IsInterior()`: interior cell, `kNoSky`, or `Sky::mode != kFull`; shared by Skylighting, Dynamic Cubemaps | FO4 sets `kFixedDimensions` on open-sky worldspaces such as Diamond City |
| Pending | FrameParams, celestial/HDR/map/shadow fields | Zero or absent | No FO4 source is consumed yet |
| Divergence | Water refraction samples the offset texel | Shore blend falls back to the mask-accepted sample above the water plane | Stock reads the unmasked texel, fringing first-person hands and weapons into water |
| Forced | Effect lighting reads `input.WorldPosition` and `ExtraFlags` | Position rebuilt from `SV_POSITION` and depth; menu and Interface3D effects are not separated | FO4 `BSEffect` PS has no world position or per-draw flags |
| Forced | Skyrim point-light term in `Effect.hlsl` | FO4's four-light term stays | FO4 `PerGeometry` layout |
| Pending | Effect-lighting `EffectRadius` shadow filter, `CLOUD_SHADOWS`, `LIGHT_LIMIT_FIX`, Interior Sun | Compiled out | Features not ported |
| Divergence | Stock shader families | Effect and DistantTree are replaced for feature consumers; Utility, Sky, Particle, BloodSplatter and Lighting stay stock | No current feature consumes the stock ones |
| Forced | Startup shader compilation blocks the main thread at `kDataLoaded` | `Main::GetCanLoad` is deferred so the main menu keeps Continue, Load, and New Game queued behind its own spinner until compilation ends; loads that bypass the menu are not gated | FO4 presents on the main thread, so blocking it freezes the window |
| Divergence | `D3D11CreateDeviceAndSwapChain` requests `{ 11_1 }` only | Native creation requests `{ 11_1, 11_0 }`; the D3D12 proxy still requires `11_1` | Feature-level 11.0 GPUs crashed at startup |

## Features

- [Frame Annotations](FrameAnnotations.md)
- [InverseSquareLighting](InverseSquareLighting.md)
- [WaterEffects](WaterEffects.md)
- [Unified Water](UnifiedWater.md)
- [Performance Overlay](PerformanceOverlay.md)
- [Terrain Shadows](TerrainShadows.md)
- [LOD Blending](LODBlending.md)
- [Terrain Variation](TerrainVariation.md)
- [Terrain Blending](TerrainBlending.md)
- [Extended Materials](ExtendedMaterials.md)
- [Dynamic Cubemaps](DynamicCubemaps.md)
- [Image Based Lighting](IBL.md)
- [RenderDoc](RenderDoc.md)
- [Remote Control](RemoteControl.md)
- [Upscaling](Upscaling.md)
- [FrameGeneration](FrameGeneration.md)
- [MotionVectorFixes](MotionVectorFixes.md)
- [Screen Space GI](ScreenSpaceGI.md)
- [Screen Space Shadows](ScreenSpaceShadows.md)
- [Skylighting](Skylighting.md)
- [Subsurface Scattering](SubsurfaceScattering.md)
- [Volumetric Shadows](VolumetricShadows.md)
- [CS Editor](CSEditor.md)

[upstream]: https://github.com/community-shaders/skyrim-community-shaders

## Upstream bugs

- `Common/FrameBuffer.hlsli` clamp helpers bound X to a texel-edge clamp but Y to the raw ratio, which may read past the bottom edge of reduced-resolution allocations; needs investigation.

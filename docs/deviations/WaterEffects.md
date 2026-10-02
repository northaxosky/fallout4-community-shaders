# WaterEffects

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d` (shared pin `6f81ebc2512da5564f37e728a65037b4c45e2a67`). `WaterCaustics.hlsli` and
`watercaustics.dds` are staged unchanged through `xmake/shared.lua`; the FO4 caustics
kernel and CPU shader mirror are deleted. There are no caustics quality knobs.
Feature classification: **mixed** ([FEATURES](../FEATURES.md)). Caustics are core; water parallax needs FO4
normal-alpha height assets and is an extension slice.
Camera-cell `WaterSystemHeight` and fullscreen diagnostics are minor FO4 adjustments.

## Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Skyrim cell lookup and water form | Loaded FO4 `GridCellArray` cells populate the eye-centred 5×5, 4096-unit b5 grid; `GetExteriorWaterHeight` resolves inherited heights; `GetWaterType` supplies averaged shallow/deep RGB times sky water multiplier | fallout4-re engine-facts Water height, forms, and camera-underwater state / Water Address Library table: cell bits +0x40, height +0x60, worldspace +0xC8; accessor IDs `{1457825,2200267,2200267}`; form/material packer data +0xB0. No reference/player-height query | `src/World/Water.cpp`, `WaterData.h`, `SharedData.cpp` `BuildSharedData` |
| Forced | Camera-relative world position and water height | b4 `CameraViewInverse` reconstructs positions relative to `CameraPosAdjust`; b5 heights subtract the same anchor Z; unchanged kernel adds anchor XY once | engine-facts Camera, matrices & world offsets / Per-frame buffer sources; native light positions are view-space | `FO4/WaterEffectsConsumer.hlsli`, `SharedData.cpp` |
| Forced | `SampColorSampler` and t65 | Linear-wrap s14 in BSDFLight, persistent t65; composite reads the isolated diagnostic result through host t61 and b7 | Native composite s14 is occupied by scene colour (`DFComposite.hlsl` `g_sLitScene`); light has no native s14 contract. Draw scopes restore the low sampler; debug production preserves its context | `WaterEffects.cpp`, `FO4/WaterEffectsConsumer.hlsli`, `FO4/WaterEffects/Debug.hlsl`, `ScopedContextState.h` |
| Forced | RGB caustics multiply directional light colour in Lighting.hlsl | RGB direct diffuse/specular and wet coat in all directional BSDFLight families; shadow-only RGB retains independent alpha; ambient/local light is unaffected | engine-facts Raster light accumulation / Base composite equation: FO4 accumulates direct light separately in RGB targets rather than Skyrim's Lighting.hlsl | `DFLight.hlsl` |
| Forced | Shore consumers read b5 water data | Wetness material production reads the unchanged shared per-cell lookup with camera-relative position | Removing WaterEffects b7 requires its shore reader to use the shared per-cell source; native positions/anchor are described in engine-facts Per-frame buffer sources | `FO4/WetnessMaterial.hlsli` `PrepareMaterial` |
| Tweak | Pinned State.cpp leaves `WaterSystemHeight` absent | Publish the camera cell's resolved plane relative to b4, or -FLT_MAX; this is not a water-mesh intersection query | Simplified data source; engine-facts Exterior cell height establishes the value, not arbitrary-position water intersections | `Water.cpp` `FillWaterData` |
| Framework | Always-loaded upstream feature | Preserve FO4 activation, load/ownership/readiness guards, persisted live enabled toggle and cached water telemetry | Repository activation, TOML persistence, shader ownership/identity, telemetry and fail-closed contracts; disabled/unready texture yields the identity multiplier | `WaterEffects.{h,cpp}`, `WaterEffectsSettings.h`, `FO4/WaterEffectsConsumer.hlsli` |
| Tweak | No upstream fullscreen caustics/submersion views | Optional fullscreen diagnostics execute the same kernel/filter in an isolated pass, not manual level-zero sampling | Diagnostic presentation, not a different water algorithm; sampler isolation is the Forced binding translation above | `WaterEffects.{h,cpp}`, `FO4/WaterEffectsConsumer.hlsli`, `FO4/WaterEffects/Debug.hlsl` |

## Not supported

| Kind | Upstream | Why / evidence | Where |
|---|---|---|---|
| Forced | WaterParallax three normal-alpha height layers, including FLOWMAP variants | Vanilla normals have no height alpha. fallout4-re `docs/bswater-promotion.md` Shore Effects runtime evidence contract records the absent height signal; native `Water.hlsl` `normalSlope` reads only XY. No height assets are invented and the water-parallax path remains stock | `Water.hlsl`; upstream `WaterParallax.hlsli` is not staged |
| Forced | Interior cell water height | FO4's exterior-height accessor explicitly rejects interior cells; engine-facts Exterior cell height. The table retains -FLT_MAX rather than borrowing the player's cached plane. Placed-water mesh intersections are outside the upstream cell-height approximation | `Water.cpp` `CellWaterData` |

## Pending

| Kind | Upstream | Notes |
|---|---|---|
| Pending | Secondary mode-0/21 BSLighting caustics | World and first-person accumulators issue no BSLighting passes (engine-facts BSLighting forward-pass source), so their caustics are deferred. Secondary-view BSLighting execution and camera/resource lifetime are not validated or integrated; do not classify that missing adapter as an engine limitation |


# WaterEffects

Rules, Kind legend and cross-cutting records: [README](README.md).

`WaterCaustics.hlsli` and
`watercaustics.dds` are staged unchanged through `xmake/shared.lua`; the FO4 caustics
kernel and CPU shader mirror are deleted. There are no caustics quality knobs.
Feature classification: **core** ([FEATURES](../FEATURES.md)). Water parallax reads height that water texture
mods ship in the normal alpha; vanilla alpha-less normals stay flat (upstream #2837/#2838, in the pin).
The consumer passes `perMaterial[9]` as the `normalsAmplitude` argument. FLOWMAP variants have no FO4 flowmap path.
Camera-cell `WaterSystemHeight` and fullscreen diagnostics are minor FO4 adjustments.

## Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Skyrim cell lookup and water form | Loaded FO4 `GridCellArray` cells populate the eye-centred 5×5, 4096-unit b5 grid; `GetExteriorWaterHeight` resolves inherited heights; `GetWaterType` supplies averaged shallow/deep RGB times sky water multiplier | fallout4-re engine-facts Water height, forms, and camera-underwater state / Water Address Library table: cell bits +0x40, height +0x60, worldspace +0xC8; accessor IDs `{1457825,2200267,2200267}`; form/material packer data +0xB0. No reference/player-height query | `src/World/Water.cpp`, `WaterData.h`, `SharedData.cpp` `BuildSharedData` |
| Forced | Camera-relative world position and water height | b4 `CameraViewInverse` reconstructs positions relative to `CameraPosAdjust`; b5 heights subtract the same anchor Z; unchanged kernel adds anchor XY once | engine-facts Camera, matrices & world offsets / Per-frame buffer sources; native light positions are view-space | `FO4/WaterEffectsConsumer.hlsli`, `SharedData.cpp` |
| Forced | `SampColorSampler` and t65 | Linear-wrap s14 in BSDFLight, persistent t65; composite reads the isolated diagnostic result through host t61 and b7 | Native composite s14 is occupied by scene colour (`DFComposite.hlsl` `g_sLitScene`); light has no native s14 contract. Draw scopes restore the low sampler; debug production preserves its context | `WaterEffects.cpp`, `FO4/WaterEffectsConsumer.hlsli`, `FO4/WaterEffects/Debug.hlsl`, `ScopedContextState.h` |
| Forced | RGB caustics multiply directional light colour in Lighting.hlsl | RGB direct diffuse/specular in all directional BSDFLight families; shadow-only RGB retains independent alpha; ambient/local light is unaffected | engine-facts Raster light accumulation / Base composite equation: FO4 accumulates direct light separately in RGB targets rather than Skyrim's Lighting.hlsl | `DFLight.hlsl` |
| Tweak | Pinned State.cpp leaves `WaterSystemHeight` absent | Publish the camera cell's resolved plane relative to b4, or -FLT_MAX; this is not a water-mesh intersection query | Simplified data source; engine-facts Exterior cell height establishes the value, not arbitrary-position water intersections | `Water.cpp` `FillWaterData` |
| Framework | Always-loaded upstream feature | Preserve FO4 activation, load/ownership/readiness guards, persisted live enabled toggle and cached water telemetry | Repository activation, TOML persistence, shader ownership/identity, telemetry and fail-closed contracts; disabled/unready texture yields the identity multiplier | `WaterEffects.{h,cpp}`, `WaterEffectsSettings.h`, `FO4/WaterEffectsConsumer.hlsli` |
| Forced | `NormalsScale` in the water pixel shader | The vertex shader forwards its b1 `NormalsScale` as upstream's `TEXCOORD8`; BSWater VS and PS are replaced as a pair | The FO4 water PS has no `NormalsScale` constant (VS b1 only), and a host-added interpolant cannot pair with a native stage | `Water.hlsl`, `ShaderDefines.h` |
| Translation | Upstream water input and resource names | `FO4/WaterParallaxConsumer.hlsli` maps `TexCoord1/2`, `WPosition` (`eyeVector`, camera-relative), `HPosition`, `Normals0{1,2,3}Tex/Sampler` (t4/t5/t6, s4/s5/s6) and `NormalsAmplitude` (PS b1 c9) onto the reconstructed PS, then includes the staged upstream `WaterParallax.hlsli` unchanged | Same algorithm under FO4 binding and field names | `FO4/WaterParallaxConsumer.hlsli`, `Water.hlsl` |
| Translation | Upstream applies one offset in its shaded-surface normal path | `surfaceNormal` computes the offset once per entry point for SPECULAR, surface, UNDERWATER and the SSLR ray passes, which rebuild the normal and so receive the same offset; LOD, VC, STENCIL and FOG skip it | FO4 SSLR ray passes are FO4-only and must trace the normal that is shaded. Specular VS drops the unread `TEXCOORD3` under this define to keep the pair's registers aligned | `Water.hlsl` |
| Framework | Parallax has no setting upstream | The live `enabled` toggle gates parallax with caustics through b7 `EnabledWaterParallax`; disabled skips the march and the offset is zero | Repository live-toggle and fail-closed contracts, as `EnabledDynamicCubemaps` | `WaterEffects.cpp`, `FeatureBuffer.h`, `SharedData.cpp`, `FO4SharedData.hlsli` |
| Tweak | No upstream fullscreen caustics/submersion views | Optional fullscreen diagnostics execute the same kernel/filter in an isolated pass, not manual level-zero sampling | Diagnostic presentation, not a different water algorithm; sampler isolation is the Forced binding translation above | `WaterEffects.{h,cpp}`, `FO4/WaterEffectsConsumer.hlsli`, `FO4/WaterEffects/Debug.hlsl` |

## Not supported

| Kind | Upstream | Why / evidence | Where |
|---|---|---|---|
| Forced | WaterParallax FLOWMAP variants | Reconstructed FO4 `Water.hlsl` has no FLOWMAP path or flow-field input | `Water.hlsl`; upstream `WaterParallax.hlsli` FLOWMAP branches |
| Forced | Interior cell water height | FO4's exterior-height accessor explicitly rejects interior cells; engine-facts Exterior cell height. The table retains -FLT_MAX rather than borrowing the player's cached plane. Placed-water mesh intersections are outside the upstream cell-height approximation | `Water.cpp` `CellWaterData` |

## Pending

| Kind | Upstream | Notes |
|---|---|---|
| Pending | Secondary mode-0/21 BSLighting caustics | World and first-person accumulators issue no BSLighting passes (engine-facts BSLighting forward-pass source), so their caustics are deferred. Secondary-view BSLighting execution and camera/resource lifetime are not validated or integrated; do not classify that missing adapter as an engine limitation |

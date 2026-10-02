# WetnessEffects

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d`; shared pin: `6f81ebc2512da5564f37e728a65037b4c45e2a67`.
Both upstream `WetnessEffects/*.hlsli` files and the required Common lighting includes
are staged unchanged. The copied FO4 implementation is deleted. The exact upstream
192-byte settings block is published in b6, with no wetness block in b7.
Feature classification: **mixed** ([FEATURES](../FEATURES.md)). Weather, wet albedo, shore and water-surface rain
are core; the MRT6 film transport (and the ground ripples riding it) is an extension slice.
Pending rows prevent a claim of complete parity.

## Translations

| Kind | Difference | Evidence / reason | Where |
|---|---|---|---|
| Forced | Produce darkened albedo, film normal and roughness in the deferred material prepass; evaluate unchanged upstream direct/indirect wetness functions in raster/tiled lighting and composite | FO4 separates material production from lighting; engine-facts Deferred prepass MRT layout and Raster light accumulation. Later passes lack the original skinned/model-space material inputs. The consumer mirrors upstream Lighting.hlsl wet material orchestration without replacing an upstream path | `DFPrepass.hlsl`, `FO4/WetnessMaterial.hlsli`, `FO4/WetnessEffectsConsumer.hlsli`, `DFLight.hlsl`, `DFTiledLighting.hlsl`, `DFComposite.hlsl` |
| Forced | Add an RGBA16F film target at checked-unused MRT6 and sample it at t71; preserve all six native targets and their blend state | Engine-facts Deferred prepass MRT layout: all native channels have material/emissive/velocity consumers; MRT2.z is environment strength, not roughness, and no universal spare film-normal channel is established. Film stores upstream octahedral world normal, roughness and validity | `WetnessEffects.cpp` `BeginPrepass`, `BindFilmOutput`; `DFPrepass.hlsl` |
| Forced | Retain film precision and clear uncovered pixels | RGBA8 reduces normal/roughness precision; its roughness step is 1/255 versus FP16's 1/32768 near the 0.05 minimum. R10G10B10A2 rounds valid blend coverage near the native 4/255 clip threshold to zero. Film inherits MRT1 blending and is not written by rejected producers or sky, so presence alone cannot reject stale previous-frame pixels without a clear | `DFPrepass.hlsl` BLEND output/clip, `FO4/WetnessMaterial.hlsli`, `FO4/WetnessEffectsConsumer.hlsli` `ReadSurface`, `WetnessEffects.cpp` `BindFilmOutput`, `BeginPrepass` |
| Forced | Set native global wetness g to zero only in ready, supported owned material variants | Engine-facts Native material wetness: ShadowSceneNode+0x2F8 is b12[30].x. Keeping it would apply native darkening/specular modification before the upstream film. Unsupported/unready/disabled variants retain native behavior | `DFPrepass.hlsl` `native_global_fade`, `wetnessOwned` |
| Forced | Grass, `TREE_ANIM`, `EYE` and instanced LOD-object (not LOD-land) prepass variants write a dry film with native g suppressed; grass pixel routes receive a `GRASS` define from their descriptor | Upstream compiles grass (`RunGrass.hlsl`), `TREE_ANIM`, `EYE` and `LOD` without `WETNESS_EFFECTS`. FO4 draws them through the shared prepass, whose film persists under later draws; grass pixel blobs are stock-identical to non-grass blobs, so only the descriptor identifies them | `DFPrepass.hlsl`, `ShaderFamilyDescriptor.cpp` |
| Forced | Supply original model position and authored geometry normal through paired non-tessellated VS/PS interpolators | Reconstructed prepass VS skins the model position and MODELSPACENORMALS replaces the normal basis with model axes before native PS interpolation. Upstream GetRainDrops uses original model position on skinned materials and puddles use geometry normal rather than the mapped normal | `DFPrepass.hlsl` `wetModelPosition`, `wetGeometryNormal` |
| Forced | Environment porosity uses the native environment-enabled bit and decoded dry environment strength | Engine-facts Ordinary environment-strength encoding / Prepass texture and envmap constants: slot-map byte108 supplies enabled and dry scale E; MRT2.z encodes sqrt(E×0.02) when g=0. The FO4 envmap loader does not load a separate per-pixel mask, so no mask texture is invented | `DFPrepass.hlsl` `environmentMapped`, `environmentMask`; `FO4/WetnessMaterial.hlsli` |
| Forced | Read weather flags/fade bytes, full-sky state, precipitation geometry and SPGD type/density; publish the native occlusion matrix and bind precipitation DS8 at t70 | Engine-facts Precipitation/native wetness: DATA rainy=0x04, snow=0x08, SPGD indices 9/11, strict native rain type0, intensity min(density/3,1). Occlusion matrix RVAs OG 0x6732B30, NG 0x3CB5C30, AE 0x3E71540; BSEffect negates row1. The unchanged upstream shader declares t70 but does not sample it | `World/Weather.cpp`, `WetnessEffects.cpp` `GetCommonBufferData`, `BindFilmOutput` |
| Forced | Read shore heights from b5 WaterData using the same camera-relative anchor as b4 | Engine-facts Exterior cell height and Per-frame buffer sources; WaterEffects already owns the upstream per-cell grid. No player-plane or b7 fallback | `FO4/WetnessMaterial.hlsli` `PrepareMaterial` |
| Forced | Suppress native ripple geometry after ToggleWaterRipples while preserving its logical enabled flag | Engine-facts ToggleWaterRipples `{1074671,2213956,2213956}`: water objects +0x18/count +0x28, ripple geometry +0x28, active flag +0xBF. Passing false into the FO4 native function also changes the wetness predicate; upstream's observable visual suppression therefore requires the geometry-only filter | `World/Water.cpp`, `WetnessEffects.cpp` ripple predicate |
| Forced | Normalize traditional light color by Color::PBRLightingScale before unchanged EvaluateWetnessLighting | Upstream LightingEval multiplies traditional wet specular by PI×0.65; reconstructed FO4 BSDF light accumulation uses PI without Skyrim's material-brightness scale. The boundary removes that host scale without changing the upstream BRDF | `FO4/WetnessEffectsConsumer.hlsli` `ApplyDirectCoat` |
| Tweak | While Dynamic Cubemaps is unavailable or disabled, publish `EnableWetnessEffects=0` and keep vanilla ripples, so FO4's native wetness renders instead of the upstream film | Upstream wet reflections exist only through Dynamic Cubemaps; without it upstream renders darkening and direct highlights only. Skyrim has no native wetness, but FO4 does, and a reflection-less film looked drier than vanilla rain. Rule 2 fallback | `FeatureBuffer.cpp` `GetFeatureBufferData`, `WetnessEffects.cpp` `GetCommonBufferData` |
| Framework | Preserve load=false activation, existing snake_case TOML keys, forwarding-only UI, live enable, debug views, ownership and telemetry | Repository host contracts; settings/defaults/climates retain upstream values. Debug uses host b7. Null t71 makes consumers dry; a non-aliasing t71 presence marker preserves native wetness on rejected producer draws, without b8 | `WetnessEffects.{h,cpp}`, `DFPrepass.hlsl`, `WetnessMath.h`, `FeatureBuffer.h`, `SharedDataLayout.h` |
| Framework | Reject stale/unavailable substrate or unexpected MRTs; restore OM/blend state and require an atomic VS/PS replacement pair | Repository fail-closed/state-preservation contract. Feature interpolators cannot use a replacement PS with a stock VS while asynchronous compilation is incomplete; film SRVs publish after the producer pass | `SharedData.cpp` `IsSharedDataCurrent`, `ShaderInjection.{h,cpp}`, `WetnessEffects.cpp` |

## Pending

| Kind | Upstream behavior | Remaining work |
|---|---|---|
| Pending | Original inputs for tessellated skinned/model-space materials and model-space variants without an authored normal in the native VS signature | Hull/domain stages are not injectable through the current stage interface. Add their typed ownership/routing and preserve original inputs, and reconstruct missing vertex attributes. These variants deliberately retain native g and produce no upstream film; missing adapters are not engine limitations |
| Pending | Secondary forward BSLighting material consumers and optional upstream Skin/TruePBR integrations | World/first-person accumulation is deferred (engine-facts BSLighting forward-pass source). Secondary-view consumers and corresponding optional feature inputs are not integrated; no absence of execution or parity is claimed |
| Pending | Rain ripples/splashes on the water surface (`Water.hlsl` `WETNESS_EFFECTS` `GetRainDrops`) | Reconstructed `Water.hlsl` has no wetness consumer; add it to the existing water normal calculation (no film target) |
| Pending | Runtime proof of material producer, weather, resources and consumer coverage | Confirm current camera cache freshness during prepass draws, MRT6 format/blends/restoration, original inputs and paired compilation fallback in RenderDoc |

## Batched in-game checks

- Rain start/end, clear/rain/snow/radstorm transitions, full-sky versus interior,
  overrides/climates, pause/freeze and shoreline cell-height changes.
- Ordinary, skin/face/hair, model-space normal, eye, tree, terrain, blend and
  tessellated draws; original geometry normals/model positions; native g applied once.
- Film MRT6 validity, depth/order/blends and exact state restoration; raster/tiled
  local/directional coat and indirect reduction; existing wet cubemap sampling unchanged.
- Live disable, unavailable resources/camera, asynchronous VS/PS readiness,
  dynamic resolution and camera motion; vanilla ripple suppression with +0xBF unchanged.


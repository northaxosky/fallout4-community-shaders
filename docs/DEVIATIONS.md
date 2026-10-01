# Deviations from upstream

Upstream [Skyrim Community Shaders][upstream] is the specification for ported features (see `AGENTS.md`).
This file lists every place a feature knowingly differs from its pinned upstream revision, the reason,
and where it lives. Each code site also carries a one-line FO4 marker comment.

- **Translation**: same upstream behavior, re-expressed for a Fallout 4 engine difference.
- **Not supported**: upstream behavior that has no Fallout 4 equivalent without new FO4-only machinery.
- **Pending**: upstream behavior not ported yet.

Each row has a Kind, which decides whether a feature ships as core or as an extension:

- **Forced**: a Fallout 4 engine difference leaves no alternative. Cite the engine evidence.
- **Fix**: a deliberate correction of an upstream bug, listed as an upstream PR candidate.
- **Framework**: a repository-wide host contract (activation, TOML persistence, forwarding-only UI, shader ownership, telemetry), not feature behavior.
- **Pending**: upstream behavior not ported yet.
- **Chosen**: FO4's own design, behavior upstream lacks, or an adaptation whose necessity is unproven.

A feature is **core** when it has no Chosen rows.

## Shared seam edits

Shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`, based on `d330bf12d`.
FO4 consumes unchanged files through `xmake\shared.lua`; no upstream path can be replaced.

| Kind | File | SHA | Why | Upstream PR status |
|---|---|---|---|---|
| Chosen | `src/Features/PerformanceOverlay.h`, `src/Features/PerformanceOverlay/{CircularBuffer,DrawCallRow}.h`, `src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.{h,cpp}` | `6fd72a4a5` | Split portable history/timing rows from the Skyrim feature header so hosts can consume them without its engine dependencies; Phase 3 consumes these rows | In the shared fork; no upstream PR recorded |
| Forced | `package/Shaders/Common/FrameBuffer.hlsli` | `e305ed0a4` | `FRAMEBUFFER_REGISTER` defaults to b12 and permits host binding at b4; FO4 engine shaders already bind the native per-frame buffer at b12 (`package/Shaders/BSWaterShader.hlsl:3`, `cbuffer PerFrame : register(b12)`) | In the shared fork; no upstream PR recorded |

## Shared consumption boundary

Shared consumption includes byte-identical SSS shaders, RCAS, shader licenses, default cubemap, SSGI noise and water
caustics assets from the shared pin, plus all seven ExponentialHeightFog shaders and their unchanged
Random, Color, Shading, IBL and Skylighting includes. Bend's CPU header is identical modulo comments; its
existing SSS consumer uses the unchanged shared header. PerformanceOverlay uses shared QPC/FPS
helpers, the profiler and the A/B aggregator. `src/Shared/PerfUtils.h` supplies Windows declarations
and scopes MSVC C4267 suppression for the upstream vector mean; the global PCH is unchanged.

| Kind | Difference | Where |
|---|---|---|
| Chosen | Windows declarations and a scoped C4267 suppression adapt the unchanged portable header to FO4's `/W4 /WX` build | `src/Shared/PerfUtils.h` |

Differing shader implementations remain FO4-owned. Upscaling's in-house shaders are retained,
not scheduled for upstream conversion. The following are
path-only relocations, with include and runtime source references updated; shader behavior is unchanged.
Paths below are relative to the staged `Shaders` root. The renderer-specific reasons remain in the
feature tables; this namespace separation is a Chosen ownership policy, not an engine limitation.
Retired rows preserve the original Kind and identify replacements consumed unchanged.

| Kind | Upstream destination | FO4-owned destination |
|---|---|---|
| Chosen | `Common/Random.hlsli` | `FO4/Common/Random.hlsli` |
| Chosen | `Common/Shading.hlsli` | `FO4/Common/Shading.hlsli` |
| Chosen | `DynamicCubemaps/BC6HEncodeCS.hlsl` | `FO4/DynamicCubemaps/BC6HEncodeCS.hlsl` |
| Chosen | `DynamicCubemaps/CaptureCommon.hlsli` | `FO4/DynamicCubemaps/CaptureCommon.hlsli` |
| Chosen | `DynamicCubemaps/DetectCaptureLightingCS.hlsl` | `FO4/DynamicCubemaps/DetectCaptureLightingCS.hlsl` |
| Chosen | `DynamicCubemaps/DynamicCubemaps.hlsli` | `FO4/DynamicCubemaps/DynamicCubemaps.hlsli` |
| Chosen | `DynamicCubemaps/InferCubemapCS.hlsl` | `FO4/DynamicCubemaps/InferCubemapCS.hlsl` |
| Chosen | `DynamicCubemaps/SpecularIrradianceCS.hlsl` | `FO4/DynamicCubemaps/SpecularIrradianceCS.hlsl` |
| Chosen | `DynamicCubemaps/UpdateCubemapCS.hlsl` | `FO4/DynamicCubemaps/UpdateCubemapCS.hlsl` |
| Chosen | `ExponentialHeightFog/ExponentialHeightFog.hlsli` | `FO4/ExponentialHeightFog/ExponentialHeightFog.hlsli` (retired; the unchanged upstream include and four compute passes are consumed directly) |
| Chosen | `InverseSquareLighting/InverseSquareLighting.hlsli` | `FO4/InverseSquareLighting/InverseSquareLighting.hlsli` (retired; the unchanged upstream include is consumed directly) |
| Chosen | `ScreenSpaceShadows/RaymarchCS.hlsl` | `FO4/ScreenSpaceShadows/RaymarchCS.hlsl` (retired; the unchanged upstream shader is consumed directly) |
| Chosen | `ScreenSpaceShadows/ScreenSpaceShadows.hlsli` | `FO4/ScreenSpaceShadows/ScreenSpaceShadows.hlsli` (retired; the unchanged upstream include is consumed directly) |
| Chosen | `ScreenSpaceShadows/bend_sss_gpu.hlsli` | `FO4/ScreenSpaceShadows/bend_sss_gpu.hlsli` (retired; the unchanged upstream include is consumed directly) |
| Chosen | `TerrainShadows/ShadowUpdate.cs.hlsl` | `FO4/TerrainShadows/ShadowUpdate.cs.hlsl` (retired; the unchanged upstream shader is consumed directly) |
| Chosen | `TerrainShadows/TerrainShadows.hlsli` | `FO4/TerrainShadows/TerrainShadows.hlsli` (retired; the unchanged upstream include is consumed directly) |
| Chosen | `Upscaling/DepthRefractionUpscalePS.hlsl` | `FO4/Upscaling/DepthRefractionUpscalePS.hlsl` |
| Chosen | `Upscaling/EncodeTexturesCS.hlsl` | `FO4/Upscaling/EncodeTexturesCS.hlsl` |
| Chosen | `Upscaling/UpscaleVS.hlsl` | `FO4/Upscaling/UpscaleVS.hlsl` |
| Chosen | `WaterEffects/WaterCaustics.hlsli` | `FO4/WaterEffects/WaterCaustics.hlsli` (retired; the unchanged upstream include is consumed directly) |
| Chosen | `WetnessEffects/WetnessEffects.hlsli` | `FO4/WetnessEffects/WetnessEffects.hlsli` (copied implementation retired; this path only forwards to the FO4 consumer of the unchanged upstream include) |

## Substrate

Shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`. FrameBuffer, SharedData,
SphericalHarmonics and its Math dependency are staged byte-for-byte. The pinned b6 ABI contains
**20** blocks, including HorizonFixSettings; all 20 are mirrored in upstream order, and absent
features leave zero blocks. The relocated `FO4/Common/SharedData.hlsli` is deleted.

Engine evidence below refers to fallout4-re `docs\engine-facts.md`.

| Kind | Difference | Evidence / reason | Where |
|---|---|---|---|
| Forced | Current world+jitter cache record supplies every rendering camera; b12 Map/Unmap is only a telemetry cross-check | Camera, matrices & world offsets: cache ownership and main preparation; cache +0x140, stride +0x250, keys +0x238/+0x240 on OG/NG/AE. AE proof: 5,614 prepass-record/b12 comparisons, maximum relative difference 0. Cache growth requires reacquiring and copying each call | `FrameBuffer.cpp` `GetWorldCameraRecord`, camera consumers, `Telemetry.cpp` |
| Forced | Engine row-vector matrices are transposed into upstream's `row_major mul(Matrix,v)` b4 contract | Per-frame buffer sources: native forward/inverse upload transposes; registers 37–40 are unjittered, not the jittered VP | `SharedDataLayout.h` `PackFrameData`, `FrameBufferTests.cpp` projection-equivalence test |
| Forced | FrameBuffer binds at b4 instead of upstream's default b12 | FO4 reconstructed shaders own b12 (`BSWaterShader.hlsl` native PerFrame); the shared register seam leaves their bytecode unchanged. b4/b7 are unused by reconstructed/native injection targets; stock DXBC identity, feature-off reflection and slot-clash tests enforce this | `SubstrateSlots.h`, utility compiler, injection compile request and cache recipe |
| Forced | One R32_FLOAT boundary pass publishes canonical world-projection depth at t17; near pixels reproject through shadow +0x8A0, world pixels use `mad(d,1.01,-0.01)`, sky uses 1 | Depth & units / Per-frame buffer sources: FO4 combines first-person and world projections; prepass OG/NG/AE writes transpose(inverse(first-person jittered projection)) at shadow +0x8A0. Native targets use t0–t15, not t17 | `CanonicalDepth.cpp`, `FO4/CanonicalDepthCS.hlsl`, `FO4/Depth.hlsli`, `Engine.h` near accessor |
| Chosen | Preserve upstream's scalar X clamp offset and Y clamp-to-ratio; snapshot current/previous ratios once per substrate update | Dynamic-resolution history: native clamp is `r−0.5/size` on NG/AE and `(trunc(size·r)−1)/size` on OG, size from logical target 1. Main clamped both axes, but upstream has one offset and clamps Y to ratio; parity takes precedence over a local fork. Substrate history is per-frame rather than effect-update history | `SharedData.cpp`, unchanged `Common/FrameBuffer.hlsli` |
| Forced | Pack world-channel DALC into pre-power SH, then apply FO4's 2.2 power once at linear consumer boundaries | Directional ambient transform/evaluation rows: native world-channel columns include transform scale and bias; native lighting evaluates power 2.2. SH is `(b/Y00,−ay/Y1,az/Y1,−ax/Y1)` with Y00=0.2820948, Y1=0.4886025. Upstream State.cpp does not gamma-convert before packing; unchanged GetAmbient is pre-power | `Engine.h` `TryGetDirectionalAmbientRows`, `SharedDataLayout.h` `PackAmbientSH`, `FO4/FO4ShaderData.hlsli` `GetAmbientLinear` |
| Chosen | Keep one FO4-only b7 for unmatched modes, debug settings, delta time and player-cell water plane; move equivalent fields to upstream b4/b5/b6 | Retired single-plane design: WaterEffects now publishes the 25-tile grid in b5 and its b7 block is removed. This row retains the original design's Kind for audit | `SharedDataLayout.h`, `FO4/FO4SharedData.hlsli`, `SharedData.cpp` `PackFeatures` |
| Framework | Keep one FO4-only b7 for unmatched modes, debug settings and delta time; move equivalent fields to upstream b4/b5/b6 | Repository-wide single-substrate ABI. WaterEffects publishes the 25-tile grid in b5 and has no b7 block. DR and NDC-to-view equivalents use b4, terrain/wetness/cubemap enable fields use b6 | `SharedDataLayout.h`, `FO4/FO4SharedData.hlsli`, `SharedData.cpp` `PackFeatures` |
| Chosen | FrameParams is zero; unvalidated celestial/HDR/map/shadow fields retain upstream absent values | No validated FO4 inverse-gamma/frame-flag or corresponding celestial/HDR source is consumed. SunDirection uses toward-light direction, while SunColor remains absent rather than inventing a sky-disc colour; FrameCount follows main's temporal method and AlwaysActive follows engine frame count | `SharedData.cpp` `BuildSharedData`, `SharedDataLayout.h` |

SSS reads canonical depth directly: first-person geometry casts as upstream does but never receives
SSS. Its settings live only in its raymarch cbuffer, not b6 or b7. Feature-off engine variants include
no substrate, and the binder runs only for contributed stages; native b12 remains owned by the engine.

## Upstream PR candidates

- `src/Features/InverseSquareLighting.cpp:76–95`: balanced intensity/cutoff/size
  (`intensity=1`, `cutoff=0.5`, `size=2`) gives radius zero rather than NaN.
  `ProcessLight` then divides by zero for `invRadius`/`fadeZone`, and gameplay
  smoothstep has a zero denominator. Clamp nonpositive/nonfinite radius to a
  documented positive minimum upstream. FO4 retains the pinned zero-radius
  behavior; no upstream PR or local correction is recorded.

- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ScreenSpaceShadows.hlsli:7`:
  adds 0.5 before truncating pixel-centered `SV_POSITION`, reading the next mask texel in both axes.
  Bend writes `floor(write_xy)` and the upstream Lighting/DistantTree callers pass `SV_POSITION`
  unchanged. Investigate removing the extra offset upstream; FO4 retains main's corrected address
  by subtracting 0.5 in the consumer include before calling the unchanged upstream sampler.
- `src/Utils/PerfUtils.h:41`: `Mean` implicitly converts `size_t` to float, raising C4267
  under FO4's `/W4 /WX`; an explicit float conversion preserves its current arithmetic.
  FO4 scopes the warning in `src/Shared/PerfUtils.h`, without changing shared behavior.
- `features/Screen Space GI/Shaders/ScreenSpaceGI/blur.cs.hlsl:104`: the center normal lookup
  needs `frameScale`. Main's correction remains in `features/ScreenSpaceGI/Shaders/ScreenSpaceGI/XeGTAO/blur.cs.hlsl`;
  upstream PR is community-shaders/skyrim-community-shaders#2795.
- `package/Shaders/Common/FrameBuffer.hlsli:53,119`: clamp helpers bound X to a texel-edge
  clamp but Y to the raw ratio. An axis-specific clamp ABI could avoid bottom-edge reads on
  reduced-resolution allocations. This is a candidate for upstream investigation, not a proven
  defect; FO4 preserves the pinned behavior and checks edges in runtime validation.
- `package/Shaders/Common/SharedData.hlsli:423–430`: `GetWaterData` assumes coordinates
  become positive after adding 64 cells; `modf` returns a negative fraction west/south
  of -262144 units. At an exact camera cell border, `round` also receives half-integers
  and can select adjacent tiles by ties-to-even. Use `floor(absolutePosition / 4096)`
  minus `floor(CameraPosAdjust.xy / 4096)` plus 2. The shared file remains unchanged.
- `src/Features/TerrainShadows.cpp:332–336,365–380`: readiness compares the child worldspace
  editor ID, but loading resolves inherited land to the parent. Resolve the same land identity in
  both paths. FO4 retains main's parent-readiness correction; no upstream PR recorded.
- `src/Features/WetnessEffects.cpp:557–558,940` and
  `features/Wetness Effects/Shaders/WetnessEffects/WetnessEffects.hlsli`: the UI permits
  zero ripple breadth/lifetime, but the CPU/shader takes their reciprocals. Define the
  zero-value meaning upstream or use positive bounds; FO4 preserves the pinned behavior.
- `src/Features/WetnessEffects.cpp:505,571` and `Common/SharedData.hlsli`:
  `RaindropFxRange` and `WeatherTransitionSpeed` are exposed and published, but the pinned
  wetness shader/material computation does not read them. Wire the intended controls
  upstream or remove dead settings there; FO4 retains their defaults, persistence and UI.

## InverseSquareLighting

Upstream pin: `d330bf12d`; shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`.
ISL, `LightLimitFix/Common.hlsli`, `Common/Game.hlsli` and `Common/Color.hlsli`
are staged unchanged. The global replacement, global settings, comparison
view and b7 ISL block are deleted. CPU radius/luminance math matches the pin,
including its zero-radius edge case.

Feature classification: **core**. No active Chosen rows. Kinds are
Forced / Fix / Framework / Pending / Chosen; no Fix is applied to the retained
upstream radius defect. Offline gates are not native-hook or rendering proof.

### Translations

Engine evidence refers to fallout4-re `docs\engine-facts.md`, **Local lights**
and its OG/NG/AE Address Library table.

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Forced | LIGH DATA bits14/15 and falloff/FOV authoring | External `Lights\*.toml` identifies plugin-local LIGH/reference IDs and retains inverse-square/linear/cutoff/size semantics. DATA 0x4000 is nonshadow spot and 0x8000 disables specular; TESForm bits14/15 are temporary/visible-distant; spot FOV/exponent are consumed. No free bits or universally spare float exists | `LightAuthoring.{h,cpp}`, feature README |
| Forced | Skyrim runtime memory overlay | Typed NiLight-owned sidecar keeps preconversion diffuse, animated dimmer, authored cutoff/size, original radius and form/reference identity without overlaying incompatible native storage | `InverseSquareLightingData.{h,cpp}`, `LocalLights.{h,cpp}`; LIGH source offsets / CPU radius/color publication |
| Forced | CreatePointLight form hook | GenDynamic `{30546,2198256,2198256}` and AddLight `{1109421,2317457,2317457}` capture form/reference/NiLight and actual shadow wrapper; FO4 creates NiPointLight even for spot shape6 | `InverseSquareLighting.cpp` `CreateLight`, `AddLight`; Placed LIGH creation |
| Forced | LLF ProcessLight radius before clustering | Update `{1022957,2198261,2198261}` refreshes animated dimmer; TestFrustumCull `{1440624,2318414,2318414}` updates all radius channels before native list admission and invalidates the cached spot cone when radius changes | `UpdateLight`, `CullLight`, `LocalLights.cpp` `InvalidateSpotCone`; Flicker and pulse / CPU frustum sphere and fade |
| Forced | Runtime extension lifetime | Reference RemoveLight `{162205,2200909,2200909}`, scene RemoveLight `{1410391,2317464,2317464}` and native orphan pruning release sidecar ownership and restore original radius. The extra owner must not defeat native sole-owner pruning | `RemoveLight`, `RemoveSceneLight`, `TiledCallback`; Light removal seam / Tiled producer and eligibility; v1 `5c2a9f95` feature README records OG/NG/AE PDB/disassembly verification and NiLight-removal tail call `{1158402,2317463,2317463}` |
| Forced | LLF light buffer/list publication | Tiled callback `{999390,2317525,2317525}` captures provenance; AddLight `{1250844,2318542,2318542}` writes t8 at the exact t6 dense append index/side; UpdateStructuredBuffer `{402301,2276904,2276904}` uploads the corresponding snapshot, not a guessed pointer/form index | `TiledCallback`, `AppendLight`, `UploadLights`, `DFTiledLighting.hlsl`; Tiled index identity / stride48 / Structured upload and bind |
| Forced | Lighting.hlsl clustered and strict light consumers | BSDFLight SetupGeometry `{976849,2319150,2319150}` supplies b11 for all native local raster families, including shadow/spot/gobo/attenuation-only. Tiled and raster call unchanged ISL and upstream PointLight color; unflagged lights retain native radial/color arithmetic and directional light stays unchanged | `SetupGeometry`, `FO4/InverseSquareLighting{Consumer,Raster}.hlsli`, `BSDFLightShader.hlsl`; Raster local constants / CPU radius/color publication |
| Forced | Upstream 256-entry cluster list | Native 127-index tile appends are guarded when ISL is contributed across dimensions10–25. Expanded radii cannot write into adjacent tiles; stock compilation remains byte-identical without the feature | `DFTiledLighting/TileCullCS.hlsli`, WARP overflow test; AE tile culling and capacity |
| Forced | Skyrim BSLight_GetLuminance | FO4 `{170662,2318428,2318428}` calls the same portable attenuation with mean preconversion diffuse × dimmer ×4, preserving ignored/disabled light exclusions, native shape masks and cached luminance write, but not render-only currentFade | `Luminance`, `LocalLights.cpp` `ShapeAttenuation`; Gameplay point attenuation / exclusions / Detection and script consumers |
| Framework | Always-loaded feature and JSON host UI | Repository `load=false`, TOML activation, ownership/hash/identity gates, resource readiness, startup compute compilation barrier, exact PS/CS binding restoration, cached telemetry and quarantine radius restoration | `InverseSquareLighting.{h,cpp}`, `ShaderInjection.{h,cpp}`, `FeatureConfigTests.cpp` |

### Not supported

No additional engine limitation is claimed beyond the evidenced translations.
Skyrim's literal record/runtime overlay is replaced, not partially emulated.

### Pending

| Kind | Upstream | Notes / where |
|---|---|---|
| Pending | CSEditor live per-light overrides, hide-regular/hide-ISL diagnostics and Light Placer runtime override publication | TOML supplies authored base/reference semantics, not Skyrim's interactive editor or external runtime placement API. `LightAuthoring` and `DrawSettings` document restart-only authoring; these integrations are not declared equivalent |

## WaterEffects

Upstream pin: `d330bf12d` (shared pin `e305ed0a4`). `WaterCaustics.hlsli` and
`watercaustics.dds` are staged unchanged through `xmake/shared.lua`; the FO4 caustics
kernel and CPU shader mirror are deleted. There are no caustics quality knobs.
Feature classification: **extension candidate**. Chosen rows: camera-cell
`WaterSystemHeight` publication; fullscreen caustics/submersion diagnostics.

### Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Skyrim cell lookup and water form | Loaded FO4 `GridCellArray` cells populate the eye-centred 5×5, 4096-unit b5 grid; `GetExteriorWaterHeight` resolves inherited heights; `GetWaterType` supplies averaged shallow/deep RGB times sky water multiplier | fallout4-re engine-facts Water height, forms, and camera-underwater state / Water Address Library table: cell bits +0x40, height +0x60, worldspace +0xC8; accessor IDs `{1457825,2200267,2200267}`; form/material packer data +0xB0. No reference/player-height query | `src/World/Water.cpp`, `WaterData.h`, `SharedData.cpp` `BuildSharedData` |
| Forced | Camera-relative world position and water height | b4 `CameraViewInverse` reconstructs positions relative to `CameraPosAdjust`; b5 heights subtract the same anchor Z; unchanged kernel adds anchor XY once | engine-facts Camera, matrices & world offsets / Per-frame buffer sources; native light positions are view-space | `FO4/WaterEffectsConsumer.hlsli`, `SharedData.cpp` |
| Forced | `SampColorSampler` and t65 | Linear-wrap s14 in BSDFLight, unchanged t65; composite only reads an isolated diagnostic texture at t33 | Native composite s14 is occupied by scene colour (`BSDFCompositeShader.hlsl` `g_sLitScene`); terrain owns s13. State scopes restore exact SRV/sampler/context bindings | `WaterEffects.cpp`, `FO4/WaterEffectsConsumer.hlsli`, `FO4/WaterEffects/Debug.hlsl`, `ScopedContextState.h` |
| Forced | RGB caustics multiply directional light colour in Lighting.hlsl | RGB direct diffuse/specular and wet coat in all directional BSDFLight families; shadow-only RGB retains independent alpha; ambient/local light is unaffected | engine-facts Raster light accumulation / Base composite equation: FO4 accumulates direct light separately in RGB targets rather than Skyrim's Lighting.hlsl | `BSDFLightShader.hlsl` |
| Forced | Shore consumers read b5 water data | Wetness material production reads the unchanged shared per-cell lookup with camera-relative position | Removing WaterEffects b7 requires its shore reader to use the shared per-cell source; native positions/anchor are described in engine-facts Per-frame buffer sources | `FO4/WetnessMaterial.hlsli` `PrepareMaterial` |
| Chosen | Pinned State.cpp leaves `WaterSystemHeight` absent | Publish the camera cell's resolved plane relative to b4, or -FLT_MAX; this is not a water-mesh intersection query | Requested host input; engine-facts Exterior cell height establishes the value, not arbitrary-position water intersections | `Water.cpp` `FillWaterData` |
| Framework | Always-loaded upstream feature | Preserve FO4 activation, load/ownership/readiness guards, persisted live enabled toggle and cached water telemetry | Repository activation, TOML persistence, shader ownership/identity, telemetry and fail-closed contracts; disabled/unready texture yields the identity multiplier | `WaterEffects.{h,cpp}`, `WaterEffectsMath.h`, `FO4/WaterEffectsConsumer.hlsli` |
| Chosen | No upstream fullscreen caustics/submersion views | Optional fullscreen diagnostics execute the same kernel/filter in an isolated pass, not manual level-zero sampling | Feature-specific visualization beyond upstream behavior; sampler isolation is the Forced binding translation above | `WaterEffects.{h,cpp}`, `FO4/WaterEffectsConsumer.hlsli`, `FO4/WaterEffects/Debug.hlsl` |

### Not supported

| Kind | Upstream | Why / evidence | Where |
|---|---|---|---|
| Forced | WaterParallax three normal-alpha height layers, including FLOWMAP variants | v1 `ba1bad9f` finding, recorded in `INGAME-CHECKLIST-v1.md` WaterEffects: vanilla normals have no height alpha. fallout4-re `docs/bswater-promotion.md` Shore Effects runtime evidence contract records the absent height signal; native `BSWaterShader.hlsl` `normalSlope` reads only XY. No height assets are invented and the BSWater consumer remains stock. Archive channel evidence was not replayed in this task | `BSWaterShader.hlsl`; upstream `WaterParallax.hlsli` is not staged |
| Forced | Interior cell water height | FO4's exterior-height accessor explicitly rejects interior cells; engine-facts Exterior cell height. The table retains -FLT_MAX rather than borrowing the player's cached plane. Placed-water mesh intersections are outside the upstream cell-height approximation | `Water.cpp` `CellWaterData` |

### Pending

| Kind | Upstream | Notes |
|---|---|---|
| Pending | Secondary mode-0/21 BSLighting caustics | World and first-person accumulators issue no BSLighting passes (engine-facts BSLighting forward-pass source), so their caustics are deferred. Secondary-view BSLighting execution and camera/resource lifetime are not validated or integrated; do not classify that missing adapter as an engine limitation |

## WetnessEffects

Upstream pin: `d330bf12d`; shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`.
Both upstream `WetnessEffects/*.hlsli` files and the required Common lighting includes
are staged unchanged. The copied FO4 implementation is deleted. The exact upstream
192-byte settings block is published in b6, with no wetness block in b7.
Feature classification: **core**. There are no Chosen rows; Pending rows prevent a
claim of complete parity.

### Translations

| Kind | Difference | Evidence / reason | Where |
|---|---|---|---|
| Forced | Produce darkened albedo, film normal and roughness in the deferred material prepass; evaluate unchanged upstream direct/indirect wetness functions in raster/tiled lighting and composite | FO4 separates material production from lighting; engine-facts Deferred prepass MRT layout and Raster light accumulation. Later passes lack the original skinned/model-space material inputs. The consumer mirrors upstream Lighting.hlsl wet material orchestration without replacing an upstream path | `BSDFPrePass.hlsl`, `FO4/WetnessMaterial.hlsli`, `FO4/WetnessEffectsConsumer.hlsli`, `BSDFLightShader.hlsl`, `DFTiledLighting.hlsl`, `BSDFCompositeShader.hlsl` |
| Forced | Add an RGBA16F film target at checked-unused MRT6 and sample it at t71; preserve all six native targets and their blend state | Engine-facts Deferred prepass MRT layout: all native channels have material/emissive/velocity consumers; MRT2.z is environment strength, not roughness, and no universal spare film-normal channel is established. Film stores upstream octahedral world normal, roughness and validity | `WetnessEffects.cpp` `BeginPrepass`, `BindFilmOutput`; `BSDFPrePass.hlsl` |
| Forced | Set native global wetness g to zero only in ready, supported owned material variants | Engine-facts Native material wetness: ShadowSceneNode+0x2F8 is b12[30].x. Keeping it would apply native darkening/specular modification before the upstream film. Unsupported/unready/disabled variants retain native behavior | `BSDFPrePass.hlsl` `native_global_fade`, `wetnessOwned` |
| Forced | Supply original model position and authored geometry normal through paired non-tessellated VS/PS interpolators | Reconstructed prepass VS skins the model position and MODELSPACENORMALS replaces the normal basis with model axes before native PS interpolation. Upstream GetRainDrops uses original model position on skinned materials and puddles use geometry normal rather than the mapped normal | `BSDFPrePass.hlsl` `wetModelPosition`, `wetGeometryNormal` |
| Forced | Environment porosity uses the native environment-enabled bit and decoded dry environment strength | Engine-facts Ordinary environment-strength encoding / Prepass texture and envmap constants: slot-map byte108 supplies enabled and dry scale E; MRT2.z encodes sqrt(E×0.02) when g=0. The FO4 envmap loader does not load a separate per-pixel mask, so no mask texture is invented | `BSDFPrePass.hlsl` `environmentMapped`, `environmentMask`; `FO4/WetnessMaterial.hlsli` |
| Forced | Read weather flags/fade bytes, full-sky state, precipitation geometry and SPGD type/density; publish the native occlusion matrix and bind precipitation DS8 at t70 | Engine-facts Precipitation/native wetness: DATA rainy=0x04, snow=0x08, SPGD indices 9/11, strict native rain type0, intensity min(density/3,1). Occlusion matrix RVAs OG 0x6732B30, NG 0x3CB5C30, AE 0x3E71540; BSEffect negates row1. The unchanged upstream shader declares t70 but does not sample it | `World/Weather.cpp`, `WetnessEffects.cpp` `GetCommonBufferData`, `BindFilmOutput` |
| Forced | Read shore heights from b5 WaterData using the same camera-relative anchor as b4 | Engine-facts Exterior cell height and Per-frame buffer sources; WaterEffects already owns the upstream per-cell grid. No player-plane or b7 fallback | `FO4/WetnessMaterial.hlsli` `PrepareMaterial` |
| Forced | Suppress native ripple geometry after ToggleWaterRipples while preserving its logical enabled flag | Engine-facts ToggleWaterRipples `{1074671,2213956,2213956}`: water objects +0x18/count +0x28, ripple geometry +0x28, active flag +0xBF. Passing false into the FO4 native function also changes the wetness predicate; upstream's observable visual suppression therefore requires the geometry-only filter | `World/Water.cpp`, `WetnessEffects.cpp` ripple predicate |
| Forced | Normalize traditional light color by Color::PBRLightingScale before unchanged EvaluateWetnessLighting | Upstream LightingEval multiplies traditional wet specular by PI×0.65; reconstructed FO4 BSDF light accumulation uses PI without Skyrim's material-brightness scale. The boundary removes that host scale without changing the upstream BRDF | `FO4/WetnessEffectsConsumer.hlsli` `ApplyDirectCoat` |
| Framework | Preserve load=false activation, existing snake_case TOML keys, forwarding-only UI, live enable, debug views, ownership and telemetry | Repository host contracts; settings/defaults/climates retain upstream values. Debug/readiness use host b8 rather than a wetness b7 block | `WetnessEffects.{h,cpp}`, `WetnessMath.h`, `FeatureBuffer.h`, `SharedDataLayout.h` |
| Framework | Reject stale/unavailable substrate or unexpected MRTs; restore exact OM/blend/SRV/buffer state and require an atomic VS/PS replacement pair | Repository fail-closed/state-preservation contract. Feature interpolators cannot use a replacement PS with a stock VS while asynchronous compilation is incomplete | `SharedData.cpp` `IsSharedDataCurrent`, `ShaderInjection.{h,cpp}`, `WetnessEffects.cpp` |

### Pending

| Kind | Upstream behavior | Remaining work |
|---|---|---|
| Pending | Original inputs for tessellated skinned/model-space materials and model-space variants without an authored normal in the native VS signature | Hull/domain stages are not injectable through the current stage interface. Add their typed ownership/routing and preserve original inputs, and reconstruct missing vertex attributes. These variants deliberately retain native g and produce no upstream film; missing adapters are not engine limitations |
| Pending | Secondary forward BSLighting material consumers and optional upstream Skin/TruePBR integrations | World/first-person accumulation is deferred (engine-facts BSLighting forward-pass source). Secondary-view consumers and corresponding optional feature inputs are not integrated; no absence of execution or parity is claimed |
| Pending | Runtime proof of material producer, weather, resources and consumer coverage | No deployment/game launch is authorized for this task. Confirm current camera cache freshness during prepass draws, MRT6 format/blends/restoration, original inputs and paired compilation fallback in RenderDoc |

### Batched in-game checks

- Rain start/end, clear/rain/snow/radstorm transitions, full-sky versus interior,
  overrides/climates, pause/freeze and shoreline cell-height changes.
- Ordinary, skin/face/hair, model-space normal, eye, tree, terrain, blend and
  tessellated draws; original geometry normals/model positions; native g applied once.
- Film MRT6 validity, depth/order/blends and exact state restoration; raster/tiled
  local/directional coat and indirect reduction; existing wet cubemap sampling unchanged.
- Live disable, unavailable resources/camera, asynchronous VS/PS readiness,
  dynamic resolution and camera motion; vanilla ripple suppression with +0xBF unchanged.

## ExponentialHeightFog

Upstream pin: `d330bf12d`; shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`.
All seven `ExponentialHeightFog` shaders, plus their Random, Color, Shading, IBL and Skylighting
includes, are staged unchanged. The ramp-derived FO4 kernel and its b7 block are deleted.
The 192-byte settings block occupies upstream b6 offset 992; feature-owned b0 volume constants
are 400 bytes. RGBA16F material/scattering/history/integration volumes, R32_FLOAT depth histories,
four dispatches, Halton bases 2/3/5, slice mapping, history weights and sampling retain the pin.

Feature classification: **core**. No Chosen or Fix rows. Pending rows are unfinished porting
or verification work, not engine incompatibilities or a claim of full enabled-feature parity.

### Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Skyrim camera/depth input and compute b12 | Fresh copied world-camera records populate feature-owned b4; canonical t17 supplies world-projection depth; native b12 remains engine-owned | fallout4-re engine-facts Camera-cache ownership, Main camera preparation, Native composite depth partition; native first-person depth uses a different projection | `VolumetricFog.cpp` `UpdateCamera`, `FO4/ExponentialHeightFogConsumer.hlsli`, `CanonicalDepth.cpp` |
| Forced | Deferred prepass builds the fog volume before lighting | Prepare settings before shared b6 publication; dispatch after canonical depth and terrain preparation, before native deferred lights | FO4 deferred order and world-camera availability in engine-facts; native light/composite consumers need current-frame volume data | `ExponentialHeightFog.cpp` `Load`, `PrepareFrame`, `RenderFrame` |
| Forced | Screen-space composite and transparent fog consumers | Replace six reconstructed native fog body families once; effect alpha/add/multiply blends, water surface/LOD and distant trees call unchanged upstream fog functions | `BSDFCompositeShader.hlsl` native color/opacity blends; `BSEffectShader.hlsl`, `BSWaterShader.hlsl`, `BSDistantTreeShader.hlsl` native forward fog equations; static family attribution in fallout4-re `composite-2d-fog-family-attribution.md` is not a live-draw proof | Named engine shaders; `FO4/ExponentialHeightFogConsumer.hlsli` |
| Forced | Fog sees the already-composed main-view sky | Exclude sky depth from composite EHF; fog the final logical MainTemp sky once after native forward sky, preserving geometry and inactive allocation pixels | FO4 sky group follows deferred composite; `RenderHooks.cpp` post-forward-sky boundary and existing DynamicCubemaps capture at logical MainTemp. Per-layer affine fog cannot preserve additive sun/stars or mask composition | `VolumetricFog.cpp` `PrepareSky`, `CompositeSky`; `FO4/ExponentialHeightFog/SkyCompositeCS.hlsl` |
| Forced | Sunlight attenuation in upstream lighting consumers | Attenuate all directional BSDFLight families, separate wet-coat sun lobes and three BSLighting families; leave ambient/local light and independent shadow alpha untouched | engine-facts Raster light accumulation / Base composite equation: FO4 stores direct light separately from ambient and surface composition | `BSDFLightShader.hlsl`, `BSLightingShader.hlsl`, forward consumers |
| Forced | Native fog color passed to upstream `originalFogColorAmount` | Pass reconstructed near/far, low/high native color before sun/grayscale coloration | Six native composite bodies explicitly evaluate these colors; this proves the shader boundary, not the TESWeather-to-b12 uploader | `BSDFCompositeShader.hlsl`, water/effect/tree consumers |
| Forced | Upstream `SampColorSampler` and volume t19 | Composite uses linear-clamp s13; forward consumers use s15; volume stays t19; CS high-slot snapshots preserve all providers | Native composite s14 is occupied by scene color; water s14 can hold caustics. Terrain's s13 descriptor is identical and can share an immutable sampler contract | `ExponentialHeightFog.cpp` `Load`, `VolumetricFog.cpp` `ScatteringInputs`; consumer include |
| Forced | Skyrim weather form identity/transition | FO4 `Sky.currentWeather`, `lastWeather`, `currentWeatherPct` select normalized plugin-local weather profiles | Typed CommonLibF4 `Sky` fields and existing `SnapshotWeather`; light plugins use 12-bit local IDs and ordinary plugins use 24-bit local IDs | `ExponentialHeightFog.cpp` `PrepareFrame`, `World/Weather.cpp` |
| Framework | Upstream JSON, ImGui and always-loaded lifecycle | Retain load=false, ownership/freeze validation, TOML deltas, live settings, forwarding-only UI, fog-factor debug view and telemetry | Repository activation, persistence, UI, measured stock identity and fail-closed contracts; colors extend the existing typed float-array mechanism | `ExponentialHeightFog.{h,cpp}`, `ExponentialHeightFogSettings.h`, settings schema/registry, shader sampler ledger |
| Framework | Weather variable registry integration | TOML weather open-map uses the upstream 23 variable names, opt-in `__enabled`, float/RGBA interpolation, integer switching above 0.5 and missing-key user-setting fallback | Repository typed TOML contract replaces upstream JSON; declared settings/defaults/edit ranges remain unchanged | `World/WeatherVariableRegistry.h`, `ExponentialHeightFogSettings.h`, `SettingsRegistry.h` |
| Framework | Failure handling | Camera, substrate, sky-resource or nonfinite dispatch-input failure keeps native fog; failed volume allocation can retain analytic fog; frame-count gaps and resource changes invalidate volume history | Repository runtime-safety contract; compute detaches/restores OM and restores b4–b7/t17 plus owned high slots. The pinned slice formula is unchanged; its singularity is an upstream PR candidate | `ExponentialHeightFog.cpp`, `VolumetricFog.cpp`, `ComputeOMScope`, `ScopedComputeSharedDataBinding` |

### Not supported

| Kind | Upstream | Why / evidence | Where |
|---|---|---|---|
| Framework | Effects11/ENB compatibility suppression | ENB is forbidden by the repository compatibility contract; no ENB provider or compatibility setting is added | `AGENTS.md`; existing host admission |

### Pending

| Kind | Upstream | Notes / code boundary |
|---|---|---|
| Pending | Directional-shadow in-scattering | Shadow SRV lifetime, world-to-shadow transforms and complete 1/2/3-cascade adaptation need proof. `VolumetricFog.cpp` leaves the directional-shadow flag unset; no screen-space mask substitutes for a froxel shadow. See fallout4-re `skylighting-shadow-cascade-contract.md` and `godrays-volumetric-lighting.md` |
| Pending | Native weather fog uploader and fade brightness | TESWeather fog arrays do not prove b12 producer semantics. Native color is mapped at the verified shader boundary; no ramp-to-density fit or invented fade is used. `respectVanillaFogFade` is persisted but has no validated FO4 fade input; sky original-fog color also lacks a validated per-frame source |
| Pending | Analytic DynamicCubemaps input | Existing FO4 cubemap helpers/providers are not the converted upstream consumer contract. The fog include temporarily excludes `DYNAMIC_CUBEMAPS` and restores the surrounding engine feature define; `useDynamicCubemaps`, tint and mip settings retain upstream values without inventing a substitute |
| Pending | IBL, Skylighting, CloudShadows and clustered local lights | Includes are unchanged, but absent host providers stay unbound and their flags/defines stay off. World-point irradiance, SH probe visibility, cloud transmittance and volumetric light clusters cannot be replaced by surface diffuse/AO/tile buffers. Terrain's validated t60 provider is connected |
| Pending | Weather editor/reset and full weather-system lifecycle | Profiles are authored in TOML; upstream weather-editor reset actions and per-weather UI are not ported. Registry reset defaults for density, original color and vanilla suppression differ from Settings defaults; user-setting fallback uses the latter |
| Pending | Map/reflection/secondary-view and forward first-person coverage | Main-view reconstructed fog bodies are covered; secondary-view camera/resource lifetime and upstream map-menu suppression are not yet validated. BSLighting sun attenuation alone does not establish full reflection fog parity. Forward raw near depth retains native fog until a first-person-to-world camera adapter exists; deferred near depth already uses canonical t17 |
| Pending | Runtime route pairing, histories and sky output | No game was launched. Live native fog suppression, MainTemp destination consumption, sky/additive layers, previous-frame origins, dynamic-resolution edges, allocation/dispatch timing and disabled-feature behavior need a build-pinned RenderDoc capture |
| Pending | Native godray coexistence | Native NVIDIA godrays execute later and are not the upstream four-pass volume pipeline. Visual/light-energy overlap and an explicit coexistence policy need runtime evidence; native godrays are not silently disabled |

### Upstream PR candidates

- `src/Features/ExponentialHeightFog.cpp:653–668,709–718`: weather-variable reset defaults
  (`originalFogColorAmount=1`, `fogDensity=0.02`, `disableVanillaFog=false`) disagree with
  `ExponentialHeightFog.h` Settings defaults (`0`, `0.005`, `true`). Confirm intent upstream
  and align reset behavior or document the distinction. No shared code is changed.
- `src/Features/ExponentialHeightFog.cpp:457–465`: the slice denominator is zero when
  `volumetricFogDistance = volumetricFogStartDistance + 9.5` above the camera near plane
  (for example, start 990.5 and distance 1000, both accepted settings). Clamp the far
  boundary beyond the offset near plane upstream to avoid singular/reversed intervals.
  FO4 retains the formula and rejects nonfinite dispatch data through its safety contract.

## Performance Overlay

Upstream pin: `d330bf12d`; shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`.
`Profiler.{h,cpp}`, `CircularBuffer`, `DrawCallRow` and `ABTestAggregator` are consumed
unchanged through `xmake\shared.lua`, including the three-frame query ring, 128 timers,
300-sample pass histories, 60-frame retirement, 600-sample default frame histories,
EMA coefficients, graph thresholds and A/B outlier/statistical rules.

Feature classification: **extension candidate** — visibility-independent sampling and
explicit USER baseline capture are Chosen rows. Offline validation is not runtime parity.
The existing Shared seam edits row for `6fd72a4a5` covers the portable row/history split;
this implementation makes no additional shared edits.

### Translations

| Kind | Difference | Evidence / reason | Where |
|---|---|---|---|
| Forced | Native shader-family IDs and names replace Skyrim's enum; family 4 is labeled `DFPrePass / DFLight` | fallout4-re `docs\engine-facts.md`, “Batch index is shader type”: constructors store `BSShader+0x18`; both deferred subclasses store 4, while geometry groups use another namespace. Native `SetDirtyStates(bool,bool)` supplies the post-state-submission timing boundary on OG/NG/AE, not Skyrim's one-argument entry | `ShaderSubclassHooks.cpp` `BeginTechniqueHook`, `RenderHooks.cpp` `DrawProfiling_Hook`, `FrameProfiler.cpp` `SetShaderFamily` |
| Framework | DearModdingUI renders tables, graphs, metric tones, managed placement and hotkeys instead of local ImGui/theme windows | Forwarding-only host contract; settings retain upstream names/defaults/ranges, with the repository's suggested `toggle_hotkey` binding | `PerformanceOverlay.cpp` `DrawOverlay`, `DrawSettings`, `ManagedOverlayOptions`; `HostClient.cpp` |
| Framework | TOML/schema persistence replaces JSON; `Position` is a finite two-number array; activation remains `load = false` while `ShowInOverlay = true` | Generated defaults use `SettingsRegistry.h` activation metadata. Main fixes `4be23af11` and `ccf7f66e3` require visibility by default once loaded; this also matches upstream | `PerformanceOverlaySettings.h`, `SettingsSchema.h` `Float2Field`, `FeatureConfig.cpp` `FormatValue`, `SettingsRegistry.h` |
| Framework | A/B snapshots contain only healthy, schema-declared live-effect fields; prepare/swap/finalize restores TEST, rolls back failures and quarantines failing publishers | Activation, ownership, restart-only fields, overlay state and diagnostic controls never enter the comparison. Transient variants cannot be persisted or saved/applied as presets; a failed restore keeps the persistence lock and exposes retry | `LiveSettings.h`, `Feature.h`, per-feature `Configure` bindings, `PerformanceOverlay.cpp` `ApplySettings`/`AbortTest`, `SettingsPersistence.h`, `PresetManager.cpp`, `HostClient.cpp` |
| Framework | Timing instrumentation does not toggle shader ownership or change replacement identity | Existing measured-stock ownership gate and feature-off StockShaderIdentity remain mandatory; D3D11 query results are labeled separately from shader-family CPU interval attribution and exclude D3D12 provider execution | `FrameProfiler.cpp`, `Annotation.{h,cpp}`, pass scopes in DynamicCubemaps/ScreenSpaceGI/TemporalResolve, `PerformanceOverlay.cpp` `DrawDrawCalls`/`DrawPasses` |
| Framework | Telemetry and fail-closed availability replace upstream's whole-overlay early return on missing VRAM adapter | A failed DearModdingUI VRAM query displays “unavailable”, not zero usage; lifecycle quarantine disables profiling and attempts TEST restoration. Device/context ownership stays in the existing D3D11 bootstrap/FrameProfiler adapter | `HostClient.cpp` `ObserveFrame`, `PerformanceOverlay.cpp` `CollectTelemetry`/`OnRuntimeQuarantined`, `FrameProfiler.cpp` |
| Chosen | Sampling continues while the host overlay is hidden; scene profiling rotates at post-composite while frame history/A/B use host observers | Upstream samples through visible overlay drawing. DearModdingUI documents render-thread observers, but native-frame cadence under FG, secondary views and menus still needs runtime correlation; necessity of this schedule is unproven | `PerformanceOverlay.cpp` `TickHostFrame`, `Telemetry.cpp` `Install`, `FrameProfiler.cpp` `MarkEngineFrame` |
| Chosen | Startup live settings or an explicitly captured USER baseline replace reloading the last saved USER configuration | TOML edits persist through the normal host UI, so explicit capture provides a stable in-memory baseline; this is a different user workflow, not an engine restriction | `PerformanceOverlay.cpp` `OnDataLoaded`, `CaptureSettings`, `DrawSettings`, `SetTestInterval` |

### Pending

| Kind | Upstream behavior | Status / boundary | Where |
|---|---|---|---|
| Pending | Manual shader-family/Total toggles and captured TEST columns in the ordinary draw-call table | Not ported. Family instrumentation is read-only; a live ownership bypass is not acceptable. A safe implementation still needs to retain the ownership/identity contract | upstream `PerformanceOverlay.cpp` `HandleShaderToggle`/`HandleTotalRowToggle`/`BuildDrawCallTableColumns`; FO4 `DrawDrawCalls` |
| Pending | Full profiling-renderer presentation, including expandable per-pass history plots | Sorted CPU/GPU Avg/P95/P99/percent feature and leaf-pass tables are present; upstream history interaction is not ported | upstream `Menu/ProfilingRenderer.cpp`; FO4 `PerformanceOverlay.cpp` `DrawPasses` |

### Upstream PR candidates

- `src/Features/PerformanceOverlay.cpp:1989–2001`: the measured-FG branch requires
  `!IsFrameGenerationActive()` inside an already-active block. Stable active FG always
  takes the 2x fallback. Test timing availability/provider capability instead of the same
  active predicate. FO4 preserves and explicitly labels the pinned calculated fallback;
  no local correction or measured-display-cadence claim.
- `src/Utils/Format.cpp:215–226`, `FormatDeltaWithPercent(float,float,float)`: decreasing timings
  render `(+−N%)` because a literal plus precedes an already signed value. Format the
  sign once. FO4 preserves the pinned formatting; no local correction.
- `src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.cpp:91` and
  `src/Utils/PerfUtils.h:41`: implicit `size_t` division raises MSVC C4267. Use an explicit
  float divisor; FO4 scopes warning suppression to unchanged shared sources.
- `src/Profiler.cpp:47–54`, `Initialize`: query-creation HRESULTs are ignored before query
  pointers are passed to the context. Check creation and release partial allocation
  before admitting profiling. The shared implementation remains unchanged.
- `src/Features/PerformanceOverlay.cpp:663–666`: the validity badge checks combined
  frame count/duration without requiring samples from both A and B. A single completed
  10-second TEST interval can be labeled valid before USER has any samples. Require
  coverage of both variants; FO4 preserves the pinned badge thresholds.

### Batched in-game checks

- Load=false startup, load=true default visibility, host unavailable/hotkey/placement,
  settings reset, history sizes 120/600/1800, update interval and TOML round trips.
- Correlate host callbacks, native scene-frame sequence and accepted/generated presents
  with FG off/on, menus, secondary views and resize. Confirm calculated post-FG labels
  never imply measured display cadence.
- Compare family draw counts and family-4 labeling to a RenderDoc capture; verify
  paired, nonnested leaf-pass queries, retirement after disabling effects, and no double
  counting of outer SSGI/upscaling scopes.
- Capture USER, edit TEST, alternate variants, change interval, stop and restore TEST.
  Verify CPU/GPU publishers and SSGI history resets, locked persistence/presets, and
  failure/quarantine/restoration retry without modifying activation or ownership.
- Verify VRAM adapter/local-segment identity and failed-query display against the host.
  Runtime cadence, coverage, GPU state and failure recovery remain unverified here;
  deployment and game launch are outside this worktree task.
## Terrain Shadows

Upstream pin: `d330bf12d` (shared fork `e305ed0a4`). Both `TerrainShadows/ShadowUpdate.cs.hlsl`
and `TerrainShadows/TerrainShadows.hlsli` are staged unchanged. Native DDS dimensions, R16G16_UNORM
shadow heights, 128-thread scans, componentwise penumbra maxima, one-degree softening, half-texel
offsets, bounded UV, ZBlur, weight-1 full sweeps and weight-0.5 ordinary slices match the pin.
Settings use `EnableTerrainShadow`; no downsampling setting or resize pass remains.

Classification: **core** — no Chosen rows; Pending rows remain unfinished and do not establish upstream parity.

### Translations

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Forced | Camera-relative consumer position plus caller origin | Absolute world position from unchanged b4 FrameBuffer; native b12 is engine-owned (`BSWaterShader.hlsl`, `cbuffer PerFrame : register(b12)`) | `FO4/TerrainShadowsConsumer.hlsli` |
| Forced | Skyrim light / worldspace accessors | Typed FO4 Sky propagation vector and inherited-land worldspace identity; engine-facts Sun light orientation, `TESWorldSpace::GetParentWorld(kLand)` | `TerrainShadows.cpp`, `World/Sky.cpp` |
| Forced | Skyrim native directional consumers | Reconstructed BSDFLight, BSLighting, BSDistantTree, BSWater and lit BSEffect directional terms; point/ambient terms stay separate. FO4's deferred and forward shaders are different programs | `package/Shaders/{BSDFLight,BSLighting,BSDistantTree,BSWater,BSEffect}Shader.hlsl` |
| Forced | Direct world position in forward consumers | Screen/depth reconstruction uses FO4's depth partition, translated at the boundary with canonical t17 and b4 inverse projection; native depth uses `d <= 0.01` / `mad(d,1.01,-0.01)` (`BSDFCompositeShader.hlsl`) | `FO4/TerrainShadowsConsumer.hlsli` |
| Forced | Engine-owned render-state lifecycle | Bind t60 and caller s13 at the post-dirty DrawTriShape boundary, including OG/NG/AE. Native SetDirtyStates resubmits s0–s15 and otherwise overwrites the caller sampler (engine-facts Shader slots & bindings, Draw state flush call) | `RenderHooks.cpp`, `ShaderInjection.{h,cpp}`, `TerrainShadows.cpp` |
| Framework | Host activation, settings, UI and shader ownership | Preserve load=false activation, TOML persistence, forwarding-only DearModdingUI, configured ownership and the stock identity gate; unavailable resources publish identity. Restore claimed bindings and unbind high-slot inputs around UAV writes without widening compute cleanup | `TerrainShadows.cpp`, `Feature.h`, `ShaderInjection.{h,cpp}` |
| Framework | Upstream buffer viewer | Retain the repository's fullscreen debug-view and telemetry contracts for shadow/heightmap views and sampled field statistics; diagnostic b8/t61 is separate from production b6/t60 and removed from b7 | `TerrainShadows.cpp`, `FO4/TerrainShadowsConsumer.hlsli`, `ShadowStatistics.cs.hlsl` |
| Fix | Child readiness checks its own editor ID | Check readiness against the resolved inherited-land parent, correcting the upstream loading/readiness mismatch listed under Upstream PR candidates | `TerrainShadows.cpp` `ResolveWorldspaceEditorId`, `EnsureLiveResources` |

### Not supported

| Kind | Upstream | Evidence / boundary | Where |
|---|---|---|---|
| Forced | Skyrim RunGrass shader family | FO4 has no distinct RunGrass target; grass receives the terrain multiplier through the owned deferred directional BSDFLight routes (`ShaderInjectionTargets.h`) | `BSDFLightShader.hlsl` |

### Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | Console/Papyrus GameHour hooks, fast-travel event and completed celestial generation | FO4 wait/sleep/load/interior-exit events request a full refresh, retained hour-jump polling covers large console/script/travel changes, and refresh waits for Sky's consumed hour. Exact small forward-hour edits and active-light versus Sky transition equivalence still need host hooks/evidence; not an accepted parity exception | `TerrainShadows.cpp` `OnDataLoaded`, `PollGameHourJump`, `OnPostDeferredPrePass` |
| Pending | Particle and volumetric sunlight | Native FO4 particles currently contain only texture × vertex color × ColorScale (`BSParticleShader.hlsl`); upstream reconstructs particle sunlight/ambient. Complete that lighting input boundary rather than shadowing emissive color. The image-space ownership catalog currently contains SSLR, not volumetric generation; reconstruct/add that consumer | `BSParticleShader.hlsl`, `ShaderInjectionTargets.h` |
| Pending | Reflections and other secondary views | Forward consumers are wired, but b4 currently publishes the main world camera. Validate secondary-view camera publication and exclusions before claiming reflections/menu parity | `SharedData.cpp`, `FO4/TerrainShadowsConsumer.hlsli` |
| Pending | Host input/runtime proof | Verify xLODGen orientation/altitude against landscape, active directional light equivalence at transitions, and t60/s13 execution/restoration for every consumer in an authorized batched runtime session; static shader/claim tests alone are insufficient | `TerrainShadows.cpp`, reconstructed consumers |

## Dynamic Cubemaps

Upstream pin: `d330bf12d`. Code: `features\DynamicCubemaps`, consumers in `package\Shaders\BSWaterShader.hlsl`,
`package\Shaders\BSDFCompositeShader.hlsl`, `package\Shaders\BSDFLightShader.hlsl` and
`package\Shaders\DFTiledLighting.hlsl`.

### Translations

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Forced | Capture before the deferred composite | Capture and publication run after the Forward cloud group (`RegisterPostForwardSky`) | FO4 draws the sky inside `DrawWorld::Forward`, after the composite; engine-facts Secondary scene views | `DynamicCubemaps.cpp` `Load` |
| Forced | Capture the main color target | Geometry radiance is rebuilt as `3 · albedo · (diffuse A + diffuse B) + emissive`; sky pixels come from scene color | FO4 has no diffuse-only target; engine-facts Deferred composition | `CaptureCommon.hlsli` |
| Chosen | Sky depth reconstructs a finite far-plane position | Sky depth `1.0` is placed on the camera far-plane direction | Explicit sky handling bypasses near/world partition reconstruction | `CaptureCommon.hlsli` `SampleCapture` |
| Forced | `FrameBuffer::WorldToView(-s)` with `z < 0` | View-space test `z > 0` on `s` | FO4 views down +Z; engine-facts Camera matrix builder | `CaptureCommon.hlsli` `SampleCapture` |
| Forced | Skyrim frame-buffer camera | Copied current world+jitter cache record and its inverse projection | Engine-facts Camera cache ownership / Per-frame buffer sources; b12 is diagnostic only | `FrameBuffer.cpp`, `DynamicCubemaps.cpp` `UpdateData` |
| Chosen | `IrradianceToLinear`/`IrradianceToGamma`, `ReflectionNormalisationScale` | Upstream's linear-lighting branch: identity, scale `1.0` | Preserve main's linear HDR consumer policy | `CubemapCommon.hlsli` |
| Forced | `Color::Ambient(SharedData::GetAmbient(R))` | Upstream pre-power GetAmbient, followed by FO4 linear boundary conversion | Engine-facts Directional ambient evaluation power 2.2; see Substrate | `FO4ShaderData.hlsli` `GetAmbientLinear`, `Engine.h` `TryGetDirectionalAmbientRows` |
| Forced | Lighting-change detection from Skyrim directional light | SharedData publishes FO4 sun radiance as the deferred sun pass receives it | Engine-facts Sun/light sources | `DetectCaptureLightingCS.hlsl` |
| Chosen | `activeReflections` from Skyrim's reflections prepass | Exterior water always uses the reflections variant | Retain main's exterior-water technique policy | `DynamicCubemaps.cpp` `ResolveReflectionMode` |
| Chosen | Active variant infers uncaptured directions from the engine reflection cube | Without the engine cube, retain scene sky with fake-variant persistence | Engine cube is optional (`bUseCubeMapReflections`); fallback policy is FO4-owned | `DynamicCubemaps.cpp` `UpdateShader`, `InferShader` |
| Forced | Water blends the dynamic cube with `CubeMapTex` | Blends with water's sky-gradient reflection color | FO4 reconstructed reflection permutations shade a sky gradient | `BSWaterShader.hlsl` `surfaceColor` |
| Forced | `WATER` permutation define | Defined locally for contributed Dynamic Cubemaps | FO4 reconstructed water compiles without it | `BSWaterShader.hlsl` |
| Forced | Deferred cubes at CS t5–t7 with LinearSampler | PS t34–t35 with native probe samplers | FO4 composite declarations occupy lower slots and samplers | `Composite.hlsli`, `DynamicCubemaps.cpp` |
| Forced | Compile-time `INTERIOR` | Runtime `SharedData::InInterior` | FO4 reconstructed permutations serve both cell types | `Composite.hlsli` |
| Forced | Wet reflectance written to G-buffer | Composite evaluates film weight and irradiance | FO4 G-buffer has no reflectance channel; engine-facts Render targets | `Composite.hlsli` `GetWetnessReflection` |
| Forced | Wet indirect-diffuse reduction in material pass | Applied in BSDFLight and DFTiledLighting | FO4 reconstructed light passes evaluate indirect diffuse | `WetnessEffects.hlsli` `GetIndirectDiffuseWeight` |
| Chosen | Always-on feature | Live enabled toggle gates wet diffuse/reflection | Repository live-toggle contract | `DynamicCubemapsSettings.h`, `WetnessEffects.hlsli`, `Composite.hlsli` |
| Chosen | Loaded DC defines ENABLESSR, labeled for water | Live enabled_ssr gates all SSLR when DC is enabled; baseline remains stock | Main's global toggle avoids recompilation; SSLR feeds surfaces and water in reconstructed shaders | `DynamicCubemaps.cpp`, `FO4SharedData.hlsli`, `Imagespace/SSLRRaytracing.hlsl` |

### Not supported

| Upstream | Why |
|---|---|
| Dynamic reflections on deferred materials through sentinel cubes, TruePBR and complex materials (`Reflectance` target) | FO4's deferred cube array rejects cubes narrower than 128 px, so 1×1 sentinels never reach the composite; the G-buffer has no F0/reflectance channel. Needs new prepass machinery and a render target |
| Forward `Lighting.hlsl` sentinel path | FO4 world and first-person accumulators emit no forward BSLighting passes |
| Dynamic Cubemap Creator (sentinel DDS export) | Nothing in FO4 can consume sentinel cubes |

## RenderDoc

Upstream pin: `d330bf12d`. Code: `features\RenderDoc`; host services: `src\Host\HostClient.cpp`,
`src\Menu\Menu.{h,cpp}`. Feature classification: **extension candidate**.
Chosen rows: installed-runtime/capture-path policy; explicit D3D11/D3D12 target selection;
absolute capture timestamps.

The pinned `src/Features/RenderDoc.{h,cpp}` couples capture operations to Skyrim's Feature,
globals, path helpers and ImGui (`.h:3,29`, `.cpp:3–33,879–918`); there is no separately compilable
service. `CaptureService.{h,cpp}` is a faithful portable port, not an upstream-file replacement.
The shared manifest and pin are unchanged. Upstream source citations below use that pin.

| Portable contract | Upstream source | FO4 implementation |
|---|---|---|
| Unified count, default 1, range 1–120; single versus multi API dispatch | `RenderDoc.h:125–126,140–145`; `RenderDoc.cpp:564–587,664–716,743–755` | `RenderDocSettings.h`, `CaptureService.h` limits, `CaptureService.cpp` `Trigger`; both existing host actions use the configured count |
| Free-space budget `max(100 MiB, 256 MiB × clamped frames)`; unavailable directory fails closed | `RenderDoc.cpp:757–779` | `CaptureService.h` `RequiredSpaceBytes`, `CaptureService.cpp` `HasSufficientDiskSpace`, `RenderDoc.cpp` `CheckCaptureDiskSpace` |
| Automatic runtime/plugin metadata, alphabetically sorted loaded features; optional user comments on first new capture only | `RenderDoc.cpp:801–825,879–955` | `RenderDoc.cpp` `BuildAutomaticCaptureComments`, `CaptureService.cpp` `Poll` |
| All top-level regular files count toward disk usage and deletion; deletion failures remain visible | `RenderDoc.cpp:476–537,594–643,968–972` | `CaptureService.cpp` `DiskUsageBytes`, `ClearCaptures`, `Inventory` |
| Newest-first inventory, five-second cache, explicit refresh clears deletion errors, completion invalidates inventory | `RenderDoc.cpp:594–690` | `CaptureService.cpp` `Inventory`, `Trigger`, `Poll` |

### Translations

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Framework | Engine/UI-coupled RenderDoc feature owns capture operations | Portable `CaptureService` with host-driven completion polling | The repository owns lifecycle and forwarding UI; no separable service exists at the pinned revision, so the host boundary keeps one reusable capture mechanism | `CaptureService.{h,cpp}`, `RenderDoc.cpp` `TickHostFrame` |
| Framework | `Enable RenderDoc Capture` defaults false and controls library loading (`RenderDoc.cpp:36–43,555–562,583–587`) | `features.RenderDoc.load` is the sole startup switch; no redundant enable key | Repository-wide load=false activation; preserve main's load-with-feature implementation `d5b754f93` | `RenderDoc.cpp` `Load`, `RenderDocSettings.h`, `src/Host/README.md` startup policy |
| Chosen | Fixed Data/Renderdoc runtime and CommunityShaders/Captures paths (`RenderDoc.cpp:539–547`) | Installed-runtime registry discovery, explicit UTF-8/env-expanded DLL override, F4SE Documents capture directory | Preserve main's installed-runtime fix `d5b754f93` and deterministic host path policy; loader options/API remain unchanged | `RenderDoc.cpp` path helpers, `TryLoadRuntime`, `ApplyCapturePath` |
| Framework | Skyrim runtime/version filename, plugin version and per-feature versions (`RenderDoc.cpp:112–118,879–918`) | Fallout4 OG/NG/AE filename and metadata; every built-in feature uses the plugin version | Host identity and the feature registry supply metadata; repository features share one versioned DLL | `RenderDoc.cpp` `RuntimeName`, `ApplyCapturePath`, `BuildAutomaticCaptureComments` |
| Chosen | Global trigger without explicit device/window (`RenderDoc.cpp:664–690`) | Explicit D3D11/D3D12 target selection | Choosing the captured API is additional behavior upstream lacks; unavailable targets still obey the Framework fail-closed contract | `RenderDoc.cpp` binding/request methods; existing temporal target publishers are unchanged |
| Fix | Global capture requests depend on presentation by the selected device (`RenderDoc.cpp:664–690`) | Explicit D3D11 Start/EndFrameCapture behind the temporal proxy | Preserve main correction `17bd6de7`: the proxy presents through D3D12, so global capture requests miss the game D3D11 frame. Both games have interop chains; this is a capture-boundary bug, not a forced FO4 engine difference | `RenderDoc.cpp` `FramesEngineCaptureManually`, `OnGameFramePresented` |
| Framework | ImGui UI, OS hotkey polling and shell opening (`RenderDoc.cpp:144–468,718–739`) | DearModdingUI forwarding, contextual F12/PrintScreen suggestions, host external opening and existing native confirmation dialog | Repository host ownership; saved hotkey overrides remain authoritative. The pinned UI API lacks ImGui sort specs/double-click queries: header buttons select a single sort column, filenames open on click, and path copy is a button | `RenderDoc.cpp` `DrawSettings`, `DrawCaptureFiles`, `OpenCaptureLocation`; `HostClient.cpp` hotkey labels; `Menu.cpp` capture-delete operation |
| Chosen | Created column shows relative age (`RenderDoc.cpp:440–443`) | Created column shows an absolute date/time | Presentation choice, not required by the forwarding-only UI contract | `RenderDoc.cpp` `DrawCaptureFiles` |
| Framework | JSON settings; filesystem failures are logged/swallowed (`RenderDoc.cpp:476–537,561–580,594–643`) | Typed TOML uses the upstream `Capture Frame Count` key and clamps integer counts before schema validation; filesystem failures surface in host UI/dialogs | Existing persistence, telemetry and dialog-result contracts remain authoritative. Obsolete `multi_frame_count`/`min_free_disk_gib` keys no longer control capture policy; capture count telemetry measures completed captures rather than requests | `RenderDocSettings.h`, `RenderDoc.cpp` `Configure`/`DrawSettings`/`CollectTelemetry`, `CaptureService.cpp`, `Menu.cpp` capture-delete operation |
| Framework | Enable toggle forces/restores Skyrim frame annotations (`RenderDoc.cpp:150–158`) | Existing D3D11/D3D12 annotation services emit markers whenever the capture tool is attached | Repository annotation services have no separate enable state; no new toggle or renderer path is added | `src/Render/Annotation.cpp` `ScopedEvent`/`SetMarker`, existing annotated consumers |

### Not supported

None identified in the portable capture service.

### Pending

None identified in the portable capture service.

### Runtime validation

Implementation is present but live capture, comments, disk-management and UI behavior remain
unverified: this worktree has no deployment/game authorization. Batched validation must verify
ordinary D3D11, D3D11 behind temporal presentation, temporal D3D12, multi-frame comments and
directory/delete workflows on the intended runtime. This is a validation gap, not unported behavior.

### Upstream PR candidates

- Extract the capture service from `src/Features/RenderDoc.{h,cpp}` so both hosts can compile it
  unchanged; add explicit target binding.
- `RenderDoc.cpp:664–690`: global capture requests miss the game D3D11 frame when an interop
  proxy presents through D3D12. Expose explicit frame boundaries for the game device; FO4 retains
  main's correction `17bd6de7` in `RenderDoc.cpp` `OnGameFramePresented`.
- `RenderDoc.cpp:792–798`: `IsCapturing()` reports enabled/API availability, not recording.
  Use `IsFrameCapturing()` for recording telemetry or rename the availability query. FO4 does not
  expose this misleading recording status.
- `RenderDoc.cpp:476–537,594–643`: disk usage/inventory/deletion include every regular file, not
  only `.rdc`. Consider filtering captures upstream. FO4 preserves this policy and explicitly
  warns before confirmed deletion.
- `RenderDoc.cpp:938–955`: an unavailable first new capture path consumes pending user comments
  without applying them. Preserve pending comments until a successful path/comment submission.
  FO4 retains the pinned first-index policy rather than silently correcting it.

## Upscaling

Upstream pin: `d330bf12d`. Consumer: `package\Shaders\Imagespace\SSLRRaytracing.hlsl`.
Feature classification: **core (doodlum FO4 release lineage)**.

Upscaling, FrameGeneration and MotionVectorFixes are maintained in-house. These comparisons
describe main's existing implementation, not an upstream-conversion plan or parity claim.
The FO4 shader copies, settings keys, SDK inputs, render scheduling and presentation are retained.

### Differences from pinned upstream

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Forced | FrameBuffer-adjusted current/previous samples in ISReflectionsRayTracing | Unchanged upstream b4 current-frame clamp plus scaled pixel dithering/Hi-Z counts; snapshot survives proxy composites | FO4 integer Hi-Z loads use full-target cb0 sizes and top-left active region with no previous-frame reflection sample; engine-facts Composite pass order / Native SSLR production | `SSLRRaytracing.hlsl`, `SharedData.cpp`, `UpscalingAnchors.h`, `TemporalRenderHooks.cpp` |
| Chosen | Upstream encode/depth/fullscreen shaders | Retain FO4-owned `EncodeTexturesCS.hlsl`, `DepthRefractionUpscalePS.hlsl` and `UpscaleVS.hlsl` under `FO4/Upscaling`; use `FO4ShaderData.hlsli` where needed | In-house maintenance and ownership policy; not an engine requirement to replace upstream files | `features/Upscaling/Shaders/FO4/Upscaling`, `TemporalRendererInternals.h`, `TemporalRenderResources.cpp` |
| Forced | Skyrim camera/state inputs | Current copied world+jitter camera record supplies provider matrices, basis, origins and FOV | engine-facts Camera-cache ownership / Main camera preparation / First-person renderer camera; Skyrim globals and buffer layout cannot identify the FO4 camera | `FrameBuffer.cpp` `GetWorldCameraRecord`, `TemporalPipeline.cpp`, `Streamline.cpp` camera constants |
| Forced | Single non-inverted perspective depth | FO4 native scene DSV combines first-person and world projections | engine-facts Native composite depth partition / b12 near reprojection: near depth ≤0.01 and world `mad(d,1.01,-0.01)` use different inverses | `Engine.h`, `FO4/Depth.hlsli`, native depth accessors |
| Pending | SDK world-projection depth | SR encoder copies native depth values into typed shared R32_FLOAT; SR/FG do not consume the substrate's canonical t17 depth | Equivalent world-projection SDK input is not implemented; typed storage does not reconcile projection partitions. Compatibility is not established merely by resource format | `TemporalResolve.cpp` `Upscale`, `TemporalFrameGenerationInputs.cpp`, `FO4/Upscaling/EncodeTexturesCS.hlsl` `DEPTH_OUTPUT` |
| Forced | Depth/refraction fullscreen resolve with SAOCameraZ MRT1 | FO4 copy samples native depth/refraction normals with shared DR/jitter math and writes RefractionNormals plus SV_Depth, without SAOCameraZ | Native post-processing consumes the scene DSV; FO4 has no Skyrim SAOCameraZ attachment. engine-facts Scene-depth binding identity / Refraction-normal and depth-pyramid identities | `FO4/Upscaling/DepthRefractionUpscalePS.hlsl`, `TemporalResolve.cpp` `UpscaleDepth` |
| Forced | Jitter and dynamic-resolution publication into Skyrim viewport | Publish equivalent offsets and RenderTargetManager ratios after FO4's native UpdateTemporalData; preserve top-left active-region proxies and engine-image-space family routing | engine-facts Dynamic-resolution history / Main camera preparation; FO4's state/target-manager layout and native updater are different | `TemporalRenderer.cpp`, `TemporalRenderHooks.cpp`, `DynamicResolution.cpp` |
| Chosen | Pre-tonemap SR with upstream exposure/HDR policy | Post-tonemap/LUT gamma-2.2 R8G8B8A8_UNORM SR and automatic exposure | Main's explicit color contract; no engine evidence proves pre-tonemap integration impossible | `TemporalResolve.cpp` `SuperResolutionRequest::color`, `TemporalFrameGenerationInputs.cpp` frozen color |
| Chosen | Direct upstream FidelityFX FSR3 dispatch | Streamline FSR3/FSR4 D3D12 providers and typed shared input transport; FO4 encoder's FSR branch writes undilated motion to u2 | In-house SDK/backend architecture; upstream instead passes native motion directly and binds u2 only for DLSS | `Streamline.{h,cpp}`, `SuperResolutionProviders.cpp`, `DX12SwapChain.cpp`, `FO4/Upscaling/EncodeTexturesCS.hlsl` |
| Chosen | FSR quality ratio used for all providers | Preserve SDK render-size queries, quality values 0–4 and provider-specific sizing; retain upstream jitter phase/sequence and projection signs | Upstream `Upscaling.cpp:854–882,911–925`: phase=int(8·(displayWidth/renderWidth)²), Halton bases 2/3 minus 0.5, offsets −2x/w,+2y/h. Upstream uses `ffxFsr3GetUpscaleRatioFromQualityMode`; FO4 queries DLSS/FSR optimal extents | `TemporalRendererInternals.h` jitter helpers, `TemporalRenderer.cpp` `PrepareRenderSize`, `Streamline.cpp` size queries |
| Framework | CamelCase JSON keys and combined SR/FG settings | Keep TOML `upscale_method`, `quality_mode`, `streamline_log_level`, `preset_dlss`, `sharpness_fsr`, `sharpness_enabled_dlss`, `sharpness_dlss`; FG controls stay under FrameGeneration | Repository schema/persistence, live admission and fail-closed missing-GPU contracts. Equivalent SR member defaults and quality/sharpness ranges match upstream `Upscaling.h:51–69`; provider-selection differences are listed separately | `TemporalRenderSettings.h`, `Upscaling.cpp`, `FrameGenerationSettings.h` |
| Chosen | Upstream provider selection and `upscaleMethodNoDLSS` preference | Include FSR4 in the method values and keep one persisted SR preference | In-house provider/settings design beyond the repository-wide persistence contract | `TemporalRenderSettings.h`, `Upscaling.cpp` |
| Chosen | RCAS D3D11 dispatch and upstream DLSS GPU/model defaults | D3D12 RCAS on SDR output and main's explicit/nondefault DLSS preset policy | RCAS shader is consumed unchanged and sharpness exp2 math matches; backend, color stage and preset-default selection are FO4 choices | `RCAS/RCAS.cpp`, `Streamline.cpp` DLSS options |
| Pending | Production reactive/transparency masks from TAA xy and water normal.z | Encoder retains `taa.x * 0.1 + taa.y` and normal.z math, but null t0 and FO4's two-channel normal input yield zero masks | Production mask inputs are not ported; normal decoding cannot supply absent water VdotN. This difference is not established as engine-impossible | `TemporalResolve.cpp` encoder bindings, `FO4/Upscaling/EncodeTexturesCS.hlsl`, engine-facts Prepass attachment roster |
| Pending | Upstream underwater-mask upscale and water consumer chain | Main's depth/refraction publication and linear-depth regeneration, without upstream `UnderwaterMaskUpscalePS.hlsl` | Equivalent upstream underwater producer/consumer coverage is not ported | `DynamicResolution.cpp`, `TemporalResolve.cpp`, upstream `features/Upscaling/Shaders/Upscaling/UnderwaterMaskUpscalePS.hlsl` |
| Framework | Upstream load-reset scheduling | FO4 frame-discontinuity, frozen-frame, provider-transition and explicit reset reasons | Repository-wide lifecycle/recovery contracts; offline tests do not establish equivalent history behavior in game | `TemporalResolve.cpp`, `TemporalPipeline.cpp`, `TemporalRenderHooks.cpp` |

### DX12 / presentation provenance

These identify port lineage versus the current in-house implementation, not consumption of
unchanged upstream C++. This reference does not change SDK or presentation behavior.

| Kind | Component | Upstream-derived portion | FO4-authored portion |
|---|---|---|---|
| Chosen | `Streamline.{h,cpp}` | Upstream Upscaling Streamline integration: resource tags, DLSS options/evaluation, camera constants and Reflex/FG SDK operations | Typed provider requests/results, camera translation, D3D12 dispatch, FSR3/4 plugin integration, capability and failure contracts |
| Chosen | `DX12SwapChain.{h,cpp}` | Upstream Upscaling's D3D11/D3D12 shared-handle/fence and proxy-presentation model | Current transport, frame-slot ownership/retirement, ordered producer/output dependencies, input publication and provider scheduling |
| Chosen | `RCAS/RCAS.{h,cpp}` | Upstream unchanged RCAS shader and sharpness conversion | D3D12 root signature, descriptors, PSO, barriers and SDR publication |
| Chosen | Direct `FidelityFX.{h,cpp}` | Upstream implementation exists in the shared checkout | Not consumed; FO4 FSR evaluation is through `StreamlineFidelityFXContract.h` and the Streamline SDK fork |
| Framework | `AgilityBootstrap`, `DXGISwapChainProxy`, `DXGISwapChainFacadeContract` | D3D12/DXGI mechanisms, not shared upstream files | Repository SDK bootstrap and swapchain facade/interception |
| Framework | `SuperResolutionProviders`, `FrameGeneration/PresentationProviders`, provider contracts | SDK operations ultimately derive from upstream Upscaling integrations | Repository typed adapters, capability admission and completion ownership |
| Framework | `Render/SwapChainHook`, `TemporalPipeline`, `TemporalPresentation`, `FrameGenerationOrchestration` | No consumed upstream Upscaling C++ | Repository host creation hooks, frame-boundary mode transitions, frozen packets, UI admission, recovery/quarantine and retirement |
| Chosen | `TemporalRenderer*`, `TemporalResolve`, `TemporalFrameGenerationInputs`, `DynamicResolution`, `SamplerBias`, `UpscalingAnchors`, `UpscalingPublication`, `ProviderOutputPreview` | Upstream jitter/encode/resolve algorithms with the local differences listed above | FO4 render scheduling, native target proxies, sampler edits, publication, capture diagnostics and engine-callsite anchors |

## FrameGeneration

Upstream pin: `d330bf12d`, upstream `src/Features/Upscaling` FG implementation.
Feature classification: **core (doodlum FO4 release lineage)**.

### Differences from pinned upstream

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Pending | World-projection non-inverted SDK depth | Copy native partitioned depth into shared R32_FLOAT; ordinary capture preserves raw values and pads inactive pixels with sky=1 | Equivalent world-projection SDK input is not implemented; the forced engine partition is documented under Upscaling, but retaining it at this boundary is not proven necessary | `TemporalFrameGenerationInputs.cpp`, `CopyDepthForFrameGenerationCS.hlsl` |
| Chosen | FG settings within Upscaling | Separate FrameGeneration feature and SR/FG provider lifetimes | In-house packaging and orchestration design, not an engine limitation; host persistence is a Framework contract | `FrameGeneration.{h,cpp}`, `FrameGenerationSettings.h`, `PresentationProviders.cpp`, `TemporalPipeline.cpp` |
| Framework | Upstream activation, persistence and failure handling | Preserve load=false activation, typed TOML, forwarding-only UI, capability admission and recovery/quarantine | Repository-wide lifecycle, persistence, UI and fail-closed contracts | `FrameGeneration.cpp`, `FrameGenerationSettings.h`, `TemporalPipeline.cpp`, `HostClient.cpp` |
| Chosen | Upstream separate premultiplied UI texture | Pre-UI SDR HUD-less plus final-color packet and current presentation providers | Main's capture/composition architecture; upstream UI-redirection parity is not established | `TemporalFrameGenerationInputs.cpp` `CaptureHUDLessColor`, `DX12SwapChain.cpp`, `Streamline.cpp` FG tags |
| Pending | Upstream premultiplied UI-redirection producer/consumer coverage | No corresponding upstream UI texture chain is ported | The retained HUD-less/final-color packet architecture does not establish equivalent upstream UI coverage | `TemporalFrameGenerationInputs.cpp`, `DX12SwapChain.cpp`, `Streamline.cpp` |
| Chosen | Geometry-derived motion/depth on transparent first-person pixels | Pre/post-alpha color-difference×1000 blends motion toward zero and depth toward `min(depth,0.1)` | engine-facts Prepass motion encoding proves BLEND coverage gaps, not this heuristic's correctness or necessity | `CopyDepthForFrameGenerationCS.hlsl`, `TemporalFrameGenerationInputs.cpp` alpha stages |
| Chosen | Upstream FG frame-limit/menu/Reflex policy | Main's menu policy, fixed/dynamic generated-frame controls and Reflex policy | In-house presentation behavior, not asserted equivalent; capability admission and provider-switch/reset safety remain Framework contracts | `FrameGenerationSettings.h`, `Streamline.cpp`, `FrameGenerationOrchestration.h`, `TemporalPipeline.cpp` |

## MotionVectorFixes

Upstream pin: `d330bf12d`; no corresponding upstream feature.
Feature classification: **core (doodlum FO4 release lineage)**.

### Differences from pinned upstream

| Kind | Difference | Engine reason / evidence | Where |
|---|---|---|---|
| Chosen | FO4-only previous-transform correction for player updates, sequence positioning, frozen/menu and LOD/landscape draws | FO4 prepass projects current/previous camera-relative positions from world/previousWorld (`BSDFPrePass.hlsl:1326–1328` previous-world rows, `:1233–1246` current/previous projections); stale transform history feeds native motion. Hook policy corrects that input and skips LoadingMenu; the necessity/effectiveness of each correction still needs runtime transform evidence. It does not add missing BLEND motion outputs (engine-facts Prepass motion encoding) | `MotionVectorFixes.cpp` `OnIdle_UpdatePlayer`, `TESObjectREFR_SetSequencePosition`, `BSLightingShaderProperty_GetRenderPasses` |

### Verification limits

| Kind | Difference | Verification boundary |
|---|---|---|
| Pending | Hook effectiveness and per-runtime sequence anchor | Main's guarded `REL::ID({og,ng,ae})` sequence callsite retains the existing OG offset; independent OG proof and runtime transform/output evidence for each correction are absent from this audit. A failed anchor is logged. These are pending evidence limits, not confirmed defects or an upstream port |

## Temporal feature bug findings (no fixes)

No new confirmed FO4 bug was established by this source-only comparison. Raw SDK depth,
zero masks, alpha conditioning and unverified hook coverage are documented differences or
evidence limits, not relabeled as proven bugs. A suspected upstream typed-depth mismatch
(`Upscaling.cpp` encoder u3 binding versus `FidelityFX.cpp` depth dispatch and the
`EncodeTexturesCS.hlsl` R32_FLOAT comment) remains unverified without backend format evidence.

## Screen Space GI

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceGI`, consumers in `package\Shaders\BSDFCompositeShader.hlsl` and
`package\Shaders\BSDFPrePass.hlsl`.

### Translations

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Forced | Compose in DeferredCompositeCS | Compose in 2D accumulator, 2D fog and cube IBL families | FO4 reconstructed families form diffuse light independently; engine-facts Deferred composition | `BSDFCompositeShader.hlsl`, `ScreenSpaceGI.hlsli` `ComposeDiffuse` |
| Forced | Diffuse-target radiance | Rebuild `3 · albedo · (diffuse A + diffuse B) + emissive` | FO4 has no diffuse-only target; engine-facts Render targets | `radianceDisocc.cs.hlsl` |
| Forced | Color::Ambient(GetAmbient(N)) with Masks.z | Upstream GetAmbient followed by FO4 power boundary, multiplied by albedo and clamped to diffuse | Reconstructed light passes fold ambient into diffuse and write no mask; engine-facts Directional ambient evaluation | `FO4ShaderData.hlsli`, `ScreenSpaceGI.hlsli` `ComposeDiffuse` |
| Chosen | Masks2.x vertex AO | `1−vertexAO` in emissive target 31.a; blended hair writes 0 | Main's FO4 G-buffer allocation policy; reconstructed stock shaders leave 31.a unread | `BSDFPrePass.hlsl` |
| Forced | G-buffer normal | Sphere-map view normal converted to octahedral pyramid | FO4 reconstructed G-buffer uses a different encoding | `prefilterNormal.cs.hlsl`, `common.hlsli` |
| Forced | NDC depth reconstruction | FO4Depth decode with record inverse world projection and typed shadow +0x8A0 near inverse | Engine-facts Depth & units / Per-frame buffer sources; first-person partition | `common.hlsli`, `ScreenSpaceGI.cpp`, `Engine.h` |
| Forced | Skyrim frame-buffer camera | Current copied world+jitter camera record | Engine-facts Camera cache ownership; b12 is diagnostic only | `FrameBuffer.cpp`, `ScreenSpaceGI.cpp` |
| Chosen | Irradiance colour conversions | Identity linear-lighting branch | Preserve main's linear HDR consumer policy | `Common/Color.hlsli` |
| Forced | Skyrim SSAO toggle | Per-frame SAO_CS active +0x08 and applied +0x121; startup bSAOEnable snapshot | Engine-facts AO state: native DrawModel/console rewrite the composite's applied bit | `ScreenSpaceGI.cpp` `ApplyVanillaSSAO`, `Engine.h` |
| Chosen | AOPower default 1, range 0–6 | Default 4, range 0–12 | Main's lighting calibration for placed-light-dominated interiors | `ScreenSpaceGISettings.h` |

### Pending

| Kind | Upstream | Notes |
|---|---|---|
| Chosen | `EnableExperimentalSpecularGI` and specular IL in `SampleSSGISpecular` | Not ported |
| Chosen | IBL and Skylighting ambient branches | Port with those features |
| Chosen | Blur center normal lookup scaled by `frameScale` | Main's local correction is retained in `features/ScreenSpaceGI/Shaders/ScreenSpaceGI/XeGTAO/blur.cs.hlsl`; upstream fix is community-shaders/skyrim-community-shaders#2795 |

## Screen Space Shadows

Feature classification: **core**. Every behavior row is Forced, an upstream bug fix, or repository framework policy; distant-tree/alpha coverage is Pending porting work.

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceShadows`, consumers in
`package\Shaders\BSDFLightShader.hlsl` through `FO4/ScreenSpaceShadowConsumer.hlsli`.
All three upstream shaders and Bend's CPU header are consumed unchanged through `xmake\shared.lua`.
BendSettings names, keys, defaults and edit ranges match upstream; its own b1 dispatch constants
carry the settings. The output is upstream's R8G8_UNORM front/back visibility, cleared to white
each frame, with no history. Canonical t17 remains shared and includes reprojected near depth;
Bend reads it directly at CS t0, so first person casts as upstream does.

### Translations

| Kind | Upstream | Fallout 4 | Why / evidence | Where |
|---|---|---|---|---|
| Forced | Ordinary world-projection scene depth and its matching VP | Bind canonical R32_FLOAT (`GetCanonicalSceneDepthSRV()`, t17 content) directly to Bend's unchanged CS t0, using the copied world+jitter record's row-vector ViewProjection; reprojected first-person depth remains eligible to cast | fallout4-re engine-facts Native composite depth partition, b12 near reprojection and World record at PostDeferredPrePass: native world depth is `mad(raw,1.01,-0.01)` and near depth has a different projection. The R32 declaration is selected by upstream's `TERRAIN_BLENDING` define; no terrain feature is implied | `CanonicalDepth.cpp`, `FO4/CanonicalDepthCS.hlsl`, `ScreenSpaceShadows.cpp` `GetComputeRaymarch` / `OnPreDeferredLights` |
| Forced | First-person forward materials never receive SSS | Explicit receiver gate returns 1 for raw depth `<=0.01` in FO4's shared deferred light passes; first person casts as upstream does through canonical depth, with no caster exclusion | Upstream `Lighting.hlsl:2224–2231` guards the consumer with `SCREEN_SPACE_SHADOWS && DEFERRED`; `deferredPass` is cleared by `EndDeferred` (`Deferred.cpp:449`, called inside RenderWorld at `693–695`) before RenderFirstPersonView. Upstream's Z-prepass depth copy contains first-person depth and Bend has no caster exclusion (see `sss-first-person-caster.md` research evidence). FO4 lights world and first person in shared deferred passes (fallout4-re engine-facts Native composite depth partition, BSLighting forward-pass source), requiring this receiver gate | `FO4/ScreenSpaceShadowConsumer.hlsli`, WARP receiver-gate fixture in `ShaderCompileTests.cpp` |
| Fix | Consumer adds 0.5 to pixel-centered SV_POSITION before integer conversion | Retain main's corrected pixel address by subtracting 0.5 at the FO4 consumer boundary, then call unchanged upstream sampling | Upstream Lighting.hlsl and FO4 BSDFLightShader.hlsl both pass pixel-centered SV_POSITION; Bend writes floored pixel coordinates. There is no demonstrated rasterization difference, so this is a retained main bug fix, not a Forced translation; see Upstream PR candidates | `FO4/ScreenSpaceShadowConsumer.hlsli` `FO4ScreenSpaceShadowVisibility` |
| Forced | Normalize and negate the active sun light's propagation direction | Normalize FO4 sun world-rotation row zero and project its negative with w=0 | fallout4-re engine-facts Sun light orientation / Deferred sun constant: row zero is sun-to-scene; native BSDFLight negates that same worldDirection into view-space toward-light b2 c1 | `World/Sky.cpp` `TryGetSunDirectionWS`, `ScreenSpaceShadows.cpp` `OnPreDeferredLights` |
| Forced | Prepass before material lighting consumers | Clear/dispatch before DeferredLightsImpl, sample unchanged upstream t45 through an FO4 include in directional light and focused shadow families, release the owned binding afterward | fallout4-re engine-facts Sun light passes and BSLighting forward-pass source: FO4 ordinary world/first-person materials are prepass-drawn and lit in deferred light passes. Reconstructed BSDFLight owns native t0–t5, not t45; no consumer-slot translation is needed | `ScreenSpaceShadows.cpp` `Load` / `BindShadowMask` / `OnPostDeferredLights`, `BSDFLightShader.hlsl` directional and shadow-only families |
| Forced | Lighting.hlsl separates direct visibility x and transmission visibility x/y by facing | Directional split families apply x to their native shared shadow term when front-facing, y to back-facing wrap/transmission; the no-cascade family applies direct and transmission separately | Reconstructed `BSDFLightShader.hlsl` DIRSPLITS1/2/3 combines direct and transmission into finalDiffuse before multiplying the shared shadow, while UNSHADOWED has no shared shadow. Front transmission already receives x through that term, so the extra multiplier is back-facing only; no new shading model is introduced | `FO4/ScreenSpaceShadowConsumer.hlsli` `FO4BackTransmissionScreenSpaceShadow`, `BSDFLightShader.hlsl` `backfaceWrap` / `forwardBlend` and UNSHADOWED directional block |
| Framework | Skyrim feature lifecycle, JSON persistence and shader activation | FO4 load=false activation, upstream-cased TOML keys, forwarding-only UI, ownership/hash gate, full-extent white R8G8 fallback with allocation backoff, telemetry and mask preview | Repository lifecycle, persistence and fail-closed renderer policies are host architecture, not engine-imposed algorithm differences. `Enable` toggles generation live; disabled/non-full-sky frames stay white without a shared feature block | `ScreenSpaceShadowsSettings.h`, `ScreenSpaceShadows.cpp`, `SssMaskBinding.{h,cpp}` |

### Pending

| Kind | Upstream | Notes / where |
|---|---|---|
| Pending | DistantTree's 0.8 SSS strength and forward/alpha receiver coverage | Deferred material receivers have no identified distant-tree discriminator; do not invent one or bind the mask to unreached forward routes. fallout4-re engine-facts Stock forward-pass census leaves distant-tree relighting and alpha route coverage for capture proof; `BSDFLightShader.hlsl`, `BSDistantTreeShader.hlsl`, `BSLightingShader.hlsl` |

[upstream]: https://github.com/community-shaders/skyrim-community-shaders

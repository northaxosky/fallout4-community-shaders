# Deviations from upstream

Upstream [Skyrim Community Shaders][upstream] is the specification for ported features (see `AGENTS.md`).
This file lists every place a feature knowingly differs from its pinned upstream revision, why Fallout 4
forces the difference, and where it lives. Each code site also carries a one-line FO4 marker comment.

- **Translation**: same upstream behavior, re-expressed for a Fallout 4 engine difference.
- **Not supported**: upstream behavior that has no Fallout 4 equivalent without new FO4-only machinery.
- **Pending**: upstream behavior not ported yet.

Kinds distinguish **Forced** engine translations (with engine evidence) from **Chosen**
architecture or behavior differences and adaptations whose necessity is not yet proven.

## Shared seam edits

Shared pin: `e305ed0a4b0200e767dae05d46975808a33280cc`, based on `d330bf12d`.
FO4 consumes unchanged files through `xmake\shared.lua`; no upstream path can be replaced.

| Kind | File | SHA | Why | Upstream PR status |
|---|---|---|---|---|
| Chosen | `src/Features/PerformanceOverlay.h`, `src/Features/PerformanceOverlay/{CircularBuffer,DrawCallRow}.h`, `src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.{h,cpp}` | `6fd72a4a5` | Split portable history/timing rows from the Skyrim feature header so hosts can consume them without its engine dependencies; Phase 3 consumes these rows | In the shared fork; no upstream PR recorded |
| Forced | `package/Shaders/Common/FrameBuffer.hlsli` | `e305ed0a4` | `FRAMEBUFFER_REGISTER` defaults to b12 and permits host binding at b4; FO4 engine shaders already bind the native per-frame buffer at b12 (`package/Shaders/BSWaterShader.hlsl:3`, `cbuffer PerFrame : register(b12)`) | In the shared fork; no upstream PR recorded |

## Shared consumption boundary

Phase 2A consumes byte-identical RCAS, shader licenses, default cubemap, SSGI noise and water
caustics assets from the shared pin. Bend's CPU header is identical modulo comments; its
existing SSS consumer uses the unchanged shared header. PerformanceOverlay uses shared QPC/FPS
helpers, not the profiler or A/B subsystem. `src/Shared/PerfUtils.h` supplies Windows declarations
and scopes MSVC C4267 suppression for the upstream vector mean; the global PCH is unchanged.

| Kind | Difference | Where |
|---|---|---|
| Chosen | Windows declarations and a scoped C4267 suppression adapt the unchanged portable header to FO4's `/W4 /WX` build | `src/Shared/PerfUtils.h` |

Differing shader implementations remain FO4-owned pending Phase 3 conversion. The following are
path-only relocations, with include and runtime source references updated; shader behavior is unchanged.
Paths below are relative to the staged `Shaders` root. The renderer-specific reasons remain in the
feature tables; this namespace separation is a Chosen ownership policy, not an engine limitation.

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
| Chosen | `ExponentialHeightFog/ExponentialHeightFog.hlsli` | `FO4/ExponentialHeightFog/ExponentialHeightFog.hlsli` |
| Chosen | `InverseSquareLighting/InverseSquareLighting.hlsli` | `FO4/InverseSquareLighting/InverseSquareLighting.hlsli` |
| Chosen | `ScreenSpaceShadows/RaymarchCS.hlsl` | `FO4/ScreenSpaceShadows/RaymarchCS.hlsl` |
| Chosen | `ScreenSpaceShadows/ScreenSpaceShadows.hlsli` | `FO4/ScreenSpaceShadows/ScreenSpaceShadows.hlsli` |
| Chosen | `ScreenSpaceShadows/bend_sss_gpu.hlsli` | `FO4/ScreenSpaceShadows/bend_sss_gpu.hlsli` |
| Chosen | `TerrainShadows/ShadowUpdate.cs.hlsl` | `FO4/TerrainShadows/ShadowUpdate.cs.hlsl` |
| Chosen | `TerrainShadows/TerrainShadows.hlsli` | `FO4/TerrainShadows/TerrainShadows.hlsli` |
| Chosen | `Upscaling/DepthRefractionUpscalePS.hlsl` | `FO4/Upscaling/DepthRefractionUpscalePS.hlsl` |
| Chosen | `Upscaling/EncodeTexturesCS.hlsl` | `FO4/Upscaling/EncodeTexturesCS.hlsl` |
| Chosen | `Upscaling/UpscaleVS.hlsl` | `FO4/Upscaling/UpscaleVS.hlsl` |
| Chosen | `WaterEffects/WaterCaustics.hlsli` | `FO4/WaterEffects/WaterCaustics.hlsli` |
| Chosen | `WetnessEffects/WetnessEffects.hlsli` | `FO4/WetnessEffects/WetnessEffects.hlsli` |

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
| Chosen | Keep one FO4-only b7 for unmatched modes, debug settings, delta time and player-cell water plane; move equivalent fields to upstream b4/b5/b6 | Main exposes one player-cell plane, not upstream's 25-tile water data. WaterData and WaterSystemHeight retain upstream absent sentinels; the absolute player-cell plane remains a FO4 consumer input. DR and NDC-to-view equivalents use b4, terrain/wetness/cubemap enable fields use b6 | `SharedDataLayout.h`, `FO4/FO4SharedData.hlsli`, `SharedData.cpp` `PackFeatures` |
| Chosen | FrameParams is zero; unvalidated celestial/HDR/map/shadow fields retain upstream absent values | No validated FO4 inverse-gamma/frame-flag or corresponding celestial/HDR source is consumed. SunDirection uses toward-light direction, while SunColor remains absent rather than inventing a sky-disc colour; FrameCount follows main's temporal method and AlwaysActive follows engine frame count | `SharedData.cpp` `BuildSharedData`, `SharedDataLayout.h` |

SSS still uses main's partitioned-depth exclusion: first-person geometry neither casts nor receives
world SSS. It does not switch to canonical depth in this phase. Feature-off engine variants include
no substrate, and the binder runs only for contributed stages; native b12 remains owned by the engine.

## Upstream PR candidates

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

## Upscaling

Upstream pin: `d330bf12d`. Consumer: `package\Shaders\Imagespace\SSLRRaytracing.hlsl`.
Feature classification: **core (doodlum FO4 release lineage)**.

### Translations

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Forced | FrameBuffer-adjusted current/previous samples in ISReflectionsRayTracing | Unchanged upstream b4 current-frame clamp plus scaled pixel dithering/Hi-Z counts; snapshot survives proxy composites | FO4 integer Hi-Z loads use full-target cb0 sizes and top-left active region with no previous-frame reflection sample; engine-facts Composite pass order / Native SSLR production | `SSLRRaytracing.hlsl`, `SharedData.cpp`, `UpscalingAnchors.h`, `TemporalRenderHooks.cpp` |

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

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceShadows`, consumers in
`package\Shaders\BSDFLightShader.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| World-only SSS with ordinary projection depth | Both Bend point samples map first-person depth (`raw <= 0.01`) to far depth `1`; world depth becomes `raw * 1.01 - 0.01` | FO4 merges first-person and world projections into deferred depth; the dispatch light coordinate uses the world projection | `RaymarchCS.hlsl` `GetWorldShadowDepth`, `bend_sss_gpu.hlsli` depth reads; shared classification/remap in `Common\DepthPartition.hlsli`, also used by DeferredPosition and SSGI |
| First-person forward lighting does not consume SSS | First-person pixels remain white in the cleared mask through Bend's far-depth return after its group barrier; consumers stay unchanged | FO4 first person uses deferred lighting, so it must neither receive nor cast world SSS | `bend_sss_gpu.hlsli` `WriteScreenSpaceShadow`, `ScreenSpaceShadows.cpp` white clear |

[upstream]: https://github.com/community-shaders/skyrim-community-shaders

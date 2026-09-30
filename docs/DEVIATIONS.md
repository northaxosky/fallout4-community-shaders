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
| Chosen | `Common/SharedData.hlsli` | `FO4/Common/SharedData.hlsli` |
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

## Upstream PR candidates

- `src/Utils/PerfUtils.h:41`: `Mean` implicitly converts `size_t` to float, raising C4267
  under FO4's `/W4 /WX`; an explicit float conversion preserves its current arithmetic.
  FO4 scopes the warning in `src/Shared/PerfUtils.h`, without changing shared behavior.
- `features/Screen Space GI/Shaders/ScreenSpaceGI/blur.cs.hlsl:104`: the center normal lookup
  needs `frameScale`. Main's correction remains in `features/ScreenSpaceGI/Shaders/ScreenSpaceGI/XeGTAO/blur.cs.hlsl`;
  upstream PR is community-shaders/skyrim-community-shaders#2795.

## Dynamic Cubemaps

Upstream pin: `d330bf12d`. Code: `features\DynamicCubemaps`, consumers in `package\Shaders\BSWaterShader.hlsl`,
`package\Shaders\BSDFCompositeShader.hlsl`, `package\Shaders\BSDFLightShader.hlsl` and
`package\Shaders\DFTiledLighting.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| Capture before the deferred composite | Capture and publication run after the Forward cloud group (`RegisterPostForwardSky`) | FO4 draws the sky inside `DrawWorld::Forward`, after the composite | `DynamicCubemaps.cpp` `Load` |
| Capture the main color target | Geometry radiance is rebuilt as `3 · albedo · (diffuse A + diffuse B) + emissive`; sky pixels come from scene color | FO4 has no diffuse-only target; scene color contains specular, probe and SSLR reflections, which made the cube view-dependent | `CaptureCommon.hlsli` |
| Sky depth reconstructs a finite far-plane position | Sky depth `1.0` is placed on the camera far plane | FO4's world projection has an infinite far plane | `CaptureCommon.hlsli` `SampleCapture` |
| `FrameBuffer::WorldToView(-s)` with `z < 0` | View-space test `z > 0` on `s` | FO4 views down +Z; both select the same screen texel | `CaptureCommon.hlsli` `SampleCapture` |
| Skyrim frame-buffer camera | Validated b12 world camera plus world-scene inverse projection | FO4 publishes the camera through b12 | `CaptureCommon.hlsli` `UpdateData` |
| `IrradianceToLinear`/`IrradianceToGamma`, `ReflectionNormalisationScale` | Upstream's linear-lighting branch: identity, scale `1.0` | FO4 lights in linear HDR | `CubemapCommon.hlsli` |
| `Color::Ambient(SharedData::GetAmbient(R))` | FO4 directional ambient transform, already linear | FO4 has no SH ambient; its directional-ambient transform lives in `BSShaderManager::State` (OG `+0xB8`, NG/AE `+0xC0`) | `SharedData.hlsli` `GetAmbient`, `Engine.h` `TryGetDirectionalAmbientRows` |
| Lighting-change detection from Skyrim directional light | SharedData publishes FO4 sun radiance as the deferred sun pass receives it | Different engine light source | `DetectCaptureLightingCS.hlsl` |
| `activeReflections` from Skyrim's reflections prepass | Exterior water always uses the reflections variant | FO4 exterior water always renders its REFLECTIONS technique | `DynamicCubemaps.cpp` `ResolveReflectionMode` |
| Active variant infers uncaptured directions from the engine reflection cube | Without the engine cube, sky is captured from the scene and kept with the fake variant's history persistence | FO4's engine reflection cube is off by default (`bUseCubeMapReflections`) | `DynamicCubemaps.cpp` `UpdateShader`, `InferShader` |
| Water blends the dynamic cube with `CubeMapTex` | Blends with the water's sky-gradient reflection color | FO4 reflection permutations shade a sky gradient instead of sampling a cube | `BSWaterShader.hlsl` `surfaceColor` |
| `WATER` permutation define | Defined locally when Dynamic Cubemaps is contributed | FO4 water compiles without it | `BSWaterShader.hlsl` |
| Deferred consumers read `ReflectanceTexture` and cubes at CS t5–t7 with `LinearSampler` | Cubes at PS t34–t35, sampled with each family's native probe sampler | FO4 composite texture slots below t34 and all sampler slots are occupied | `Composite.hlsli`, `DynamicCubemaps.cpp` |
| Compile-time `INTERIOR` | Runtime `SharedData::InInterior` | FO4 shares composite permutations across interiors and exteriors | `Composite.hlsli` |
| Wet reflectance written to a G-buffer target | The composite evaluates the film weight and irradiance itself | FO4 has no reflectance G-buffer | `Composite.hlsli` `GetWetnessReflection` |
| Wet indirect-diffuse reduction in the material pass | Applied to ambient diffuse in the BSDFLight and DFTiledLighting passes | FO4 evaluates indirect diffuse in light passes | `WetnessEffects.hlsli` `GetIndirectDiffuseWeight` |
| Always-on feature | `enabled` live toggle; wet diffuse reduction and wet reflection are both gated on it | Repository contract: effects toggle live | `DynamicCubemapsSettings.h`, `WetnessEffects.hlsli`, `Composite.hlsli` |
| `EnabledSSR = true`, `ENABLESSR` permits raymarching; labeled for water | `enabled_ssr = true`; the static `DYNAMIC_CUBEMAPS` contribution reads DC's live setting from the existing feature buffer and returns zero when DC is enabled and SSR is off; help text states it covers all screen-space reflections | FO4 stock has no SSR gate, and SSLR feeds the second composite (0x800, t14) for surfaces as well as water (t9/t10), so this toggle is global. Baseline and unloaded/disabled DC preserve stock SSR. The runtime gate avoids toggle-driven recompilation and stock fallback; upstream only defines `ENABLESSR` through loaded DC | `DynamicCubemaps.cpp`, `SharedData.hlsli`, `Imagespace\SSLRRaytracing.hlsl` |

### Not supported

| Upstream | Why |
|---|---|
| Dynamic reflections on deferred materials through sentinel cubes, TruePBR and complex materials (`Reflectance` target) | FO4's deferred cube array rejects cubes narrower than 128 px, so 1×1 sentinels never reach the composite; the G-buffer has no F0/reflectance channel. Needs new prepass machinery and a render target |
| Forward `Lighting.hlsl` sentinel path | FO4 world and first-person accumulators emit no forward BSLighting passes |
| Dynamic Cubemap Creator (sentinel DDS export) | Nothing in FO4 can consume sentinel cubes |

## Upscaling

Upstream pin: `d330bf12d`. Consumer: `package\Shaders\Imagespace\SSLRRaytracing.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| `FrameBuffer::GetDynamicResolutionAdjustedScreenPosition` and previous-frame samples in `ISReflectionsRayTracing` | SharedData-adjusted, clamped current-frame samples plus scaled pixel dithering and Hi-Z cell counts; b5 preserves the once-per-frame render-scale snapshot through proxy composites | FO4 raytracing uses integer Hi-Z loads and full-target cb0 sizes in a top-left render region, with no previous-frame reflection sample. Upscaling publishes ratios before the deferred prepass; b5 publishes afterward, and SSLR precedes the second composite's temporary ratio neutralization | `SSLRRaytracing.hlsl`, `SharedData.cpp`, `UpscalingAnchors.h`, `TemporalRenderHooks.cpp`; fallout4-re `docs\engine-facts.md` Composite pass order / Native SSLR production |

## Screen Space GI

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceGI`, consumers in `package\Shaders\BSDFCompositeShader.hlsl` and
`package\Shaders\BSDFPrePass.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| Compose in `DeferredCompositeCS` | Compose in the composite families that form diffuse light: 2D accumulator, 2D fog and cube IBL | FO4 has no single deferred composite; each family forms `3 · albedo · (diffuse A + diffuse B) + emissive` itself | `BSDFCompositeShader.hlsl`, `ScreenSpaceGI.hlsli` `ComposeDiffuse` |
| Radiance from the diffuse target | Rebuilt as `3 · albedo · (diffuse A + diffuse B) + emissive` | FO4 has no diffuse-only target | `radianceDisocc.cs.hlsl` |
| Directional ambient: `Color::Ambient(GetAmbient(N)) · albedo` with luma from `Masks.z` | `SharedData::GetAmbient(N) · albedo`, clamped to the diffuse term | FO4 light passes fold ambient into the diffuse accumulators and write no ambient mask | `ScreenSpaceGI.hlsli` `ComposeDiffuse` |
| Vertex AO in `Masks2.x`, written by `Lighting.hlsl` | `1 − vertexAO` in emissive target alpha (logical 31), written by the injected prepass; blended hair writes 0 | FO4 has no spare G-buffer channel; 31.a is unread by stock shaders | `BSDFPrePass.hlsl` |
| G-buffer normal | FO4 sphere-map view normal (RT20), encoded into upstream's octahedral pyramid | Different G-buffer encoding | `prefilterNormal.cs.hlsl`, `common.hlsli` |
| `ScreenToViewDepth` from the NDC depth buffer | Raw depth decoded with the composite's far/near reprojection rows | FO4 renders first person into a separate near depth partition | `common.hlsli` |
| Skyrim frame-buffer camera | Validated b12 world camera and reprojection rows | FO4 publishes the camera through b12 | `common.hlsli`, `ScreenSpaceGI.cpp` |
| `IrradianceToLinear`/`IrradianceToGamma` | Upstream's linear-lighting branch: identity | FO4 lights in linear HDR | `Common\Color.hlsli` |
| Skyrim SSAO toggle | Per frame after the deferred prepass, write SAO_CS active (`+0x08`) and applied (`+0x121`); applied comes from the startup `bSAOEnable` snapshot | The composite's AO bit reads SAO_CS `+0x121`, which DrawModel and console commands rewrite | `ScreenSpaceGI.cpp` `ApplyVanillaSSAO`, `Engine.h` `GetScalableAOComputeState` |
| `AOPower` default 1, range 0–6 | Default 4, range 0–12 | FO4 interiors get most of their light from placed lights, which receive only `sqrt(AO)`; directional ambient is about 7% of occluded diffuse in a measured interior | `ScreenSpaceGISettings.h` |

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

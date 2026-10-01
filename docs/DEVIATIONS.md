# Deviations from upstream

Upstream [Skyrim Community Shaders][upstream] is the specification for ported features (see `AGENTS.md`).
This file lists every place a feature knowingly differs from its pinned upstream revision, why Fallout 4
forces the difference, and where it lives. Each code site also carries a one-line FO4 marker comment.

- **Translation**: same upstream behavior, re-expressed for a Fallout 4 engine difference.
- **Not supported**: upstream behavior that has no Fallout 4 equivalent without new FO4-only machinery.
- **Pending**: upstream behavior not ported yet.

## Shader replacement

Upstream pin: `d330bf12d` (`ShaderCache`, `Hooks`, `State`, `AdvancedSettingsRenderer`).
Source paths follow the native fxp name; entry point is `main`, profile follows the stage,
and live type/master switches suppress the entire replacement, including feature contributions.

| Upstream | Fallout 4 translation | Engine evidence / boundary | Code |
|---|---|---|---|
| `BSShader::shaderType` identifies each shader class | Use CommonLibF4's `BSShaderManager::ShaderEnum`; disambiguate type 4 by exact `DFPrepass` / `DFLight` fxp name | fallout4-re `docs\engine-facts.md`, “Batch index is shader type”: both constructors write 4 at `BSShader+0x18`, although CommonLibF4's enum lists DFLight as 5. `harness\shaders\section_partition\native.py` records the exact names. Standalone compute uses its existing loader-recorded name and target | `ShaderInjection.cpp` `ResolveNativeShaderTarget` |
| Source presence determines ownership | The existing developer force-on root remains the effective source root | Repository developer override contract; no additional source lookup path | `ShaderInjection.cpp` `ResolveShaderRoot`, `ShaderFamilyDescriptor.cpp` `IsShaderSourceAvailable` |
| No stock reconstruction identity gate | FO4CS-only CI compares reconstructed routes against measured stock bytecode | Schema-2 fallout4-re export supplies native names; the gate shares runtime source-presence ownership and compilation with all type toggles on. Unnamed routes remain unowned; unhooked HS/DS routes are excluded | `StockShaderIdentityTests.cpp`, `generate_stock_shader_identity.py` |

Stage selection uses upstream `VSHADER` / `PSHADER` / `CSHADER` defines. SSLR supplies both
stages through upstream `Common\DummyVSTexCoord.hlsl`, as `ISReflectionsRayTracing` does.
The gate therefore owns 1,512 routes, including passthrough VS ordinal 3881
(`89e56423886dc05ed3b2d1445a70f386c651ac38`) and PS ordinal 3882. Compile failures remain
runtime stock fallbacks but fail the identity gate; they are not reclassified as unowned.

## Dynamic Cubemaps

Upstream pin: `d330bf12d`. Code: `features\DynamicCubemaps`, consumers in `package\Shaders\Water.hlsl`,
`package\Shaders\DFComposite.hlsl`, `package\Shaders\DFLight.hlsl` and
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
| Water blends the dynamic cube with `CubeMapTex` | Blends with the water's sky-gradient reflection color | FO4 reflection permutations shade a sky gradient instead of sampling a cube | `Water.hlsl` `surfaceColor` |
| `WATER` permutation define | Defined locally when Dynamic Cubemaps is contributed | FO4 water compiles without it | `Water.hlsl` |
| Deferred consumers read `ReflectanceTexture` and cubes at CS t5–t7 with `LinearSampler` | Cubes at PS t34–t35, sampled with each family's native probe sampler | FO4 composite texture slots below t34 and all sampler slots are occupied | `Composite.hlsli`, `DynamicCubemaps.cpp` |
| Compile-time `INTERIOR` | Runtime `SharedData::InInterior` | FO4 shares composite permutations across interiors and exteriors | `Composite.hlsli` |
| Wet reflectance written to a G-buffer target | The composite evaluates the film weight and irradiance itself | FO4 has no reflectance G-buffer | `Composite.hlsli` `GetWetnessReflection` |
| Wet indirect-diffuse reduction in the material pass | Applied to ambient diffuse in the BSDFLight and DFTiledLighting passes | FO4 evaluates indirect diffuse in light passes | `WetnessEffects.hlsli` `GetIndirectDiffuseWeight` |
| Always-on feature | `enabled` live toggle; wet diffuse reduction and wet reflection are both gated on it | Repository contract: effects toggle live | `DynamicCubemapsSettings.h`, `WetnessEffects.hlsli`, `Composite.hlsli` |
| `EnabledSSR = true`, `ENABLESSR` permits raymarching; labeled for water | `enabled_ssr = true`; the static `DYNAMIC_CUBEMAPS` contribution reads DC's live setting from the existing feature buffer and returns zero when DC is enabled and SSR is off; help text states it covers all screen-space reflections | FO4 stock has no SSR gate, and SSLR feeds the second composite (0x800, t14) for surfaces as well as water (t9/t10), so this toggle is global. Baseline and unloaded/disabled DC preserve stock SSR. The runtime gate avoids toggle-driven recompilation and stock fallback; upstream only defines `ENABLESSR` through loaded DC | `DynamicCubemaps.cpp`, `SharedData.hlsli`, `ISSSLRRaytracing.hlsl` |

### Not supported

| Upstream | Why |
|---|---|
| Dynamic reflections on deferred materials through sentinel cubes, TruePBR and complex materials (`Reflectance` target) | FO4's deferred cube array rejects cubes narrower than 128 px, so 1×1 sentinels never reach the composite; the G-buffer has no F0/reflectance channel. Needs new prepass machinery and a render target |
| Forward `Lighting.hlsl` sentinel path | FO4 world and first-person accumulators emit no forward BSLighting passes |
| Dynamic Cubemap Creator (sentinel DDS export) | Nothing in FO4 can consume sentinel cubes |

## Upscaling

Upstream pin: `d330bf12d`. Consumer: `package\Shaders\ISSSLRRaytracing.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| `FrameBuffer::GetDynamicResolutionAdjustedScreenPosition` and previous-frame samples in `ISReflectionsRayTracing` | SharedData-adjusted, clamped current-frame samples plus scaled pixel dithering and Hi-Z cell counts; b5 preserves the once-per-frame render-scale snapshot through proxy composites | FO4 raytracing uses integer Hi-Z loads and full-target cb0 sizes in a top-left render region, with no previous-frame reflection sample. Upscaling publishes ratios before the deferred prepass; b5 publishes afterward, and SSLR precedes the second composite's temporary ratio neutralization | `ISSSLRRaytracing.hlsl`, `SharedData.cpp`, `UpscalingAnchors.h`, `TemporalRenderHooks.cpp`; fallout4-re `docs\engine-facts.md` Composite pass order / Native SSLR production |

## Screen Space GI

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceGI`, consumers in `package\Shaders\DFComposite.hlsl` and
`package\Shaders\DFPrepass.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| Compose in `DeferredCompositeCS` | Compose in the composite families that form diffuse light: 2D accumulator, 2D fog and cube IBL | FO4 has no single deferred composite; each family forms `3 · albedo · (diffuse A + diffuse B) + emissive` itself | `DFComposite.hlsl`, `ScreenSpaceGI.hlsli` `ComposeDiffuse` |
| Radiance from the diffuse target | Rebuilt as `3 · albedo · (diffuse A + diffuse B) + emissive` | FO4 has no diffuse-only target | `radianceDisocc.cs.hlsl` |
| Directional ambient: `Color::Ambient(GetAmbient(N)) · albedo` with luma from `Masks.z` | `SharedData::GetAmbient(N) · albedo`, clamped to the diffuse term | FO4 light passes fold ambient into the diffuse accumulators and write no ambient mask | `ScreenSpaceGI.hlsli` `ComposeDiffuse` |
| Vertex AO in `Masks2.x`, written by `Lighting.hlsl` | `1 − vertexAO` in emissive target alpha (logical 31), written by the injected prepass; blended hair writes 0 | FO4 has no spare G-buffer channel; 31.a is unread by stock shaders | `DFPrepass.hlsl` |
| G-buffer normal | FO4 sphere-map view normal (RT20), encoded into upstream's octahedral pyramid | Different G-buffer encoding | `prefilterNormal.cs.hlsl`, `common.hlsli` |
| `ScreenToViewDepth` from the NDC depth buffer | Raw depth decoded with the composite's far/near reprojection rows | FO4 renders first person into a separate near depth partition | `common.hlsli` |
| Skyrim frame-buffer camera | Validated b12 world camera and reprojection rows | FO4 publishes the camera through b12 | `common.hlsli`, `ScreenSpaceGI.cpp` |
| `IrradianceToLinear`/`IrradianceToGamma` | Upstream's linear-lighting branch: identity | FO4 lights in linear HDR | `Common\Color.hlsli` |
| Skyrim SSAO toggle | Per frame after the deferred prepass, write SAO_CS active (`+0x08`) and applied (`+0x121`); applied comes from the startup `bSAOEnable` snapshot | The composite's AO bit reads SAO_CS `+0x121`, which DrawModel and console commands rewrite | `ScreenSpaceGI.cpp` `ApplyVanillaSSAO`, `Engine.h` `GetScalableAOComputeState` |
| `AOPower` default 1, range 0–6 | Default 4, range 0–12 | FO4 interiors get most of their light from placed lights, which receive only `sqrt(AO)`; directional ambient is about 7% of occluded diffuse in a measured interior | `ScreenSpaceGISettings.h` |

### Pending

| Upstream | Notes |
|---|---|
| `EnableExperimentalSpecularGI` and specular IL in `SampleSSGISpecular` | Not ported |
| IBL and Skylighting ambient branches | Port with those features |
| Blur center normal lookup scaled by `frameScale` | Applied locally; upstream fix is community-shaders/skyrim-community-shaders#2795 |

## Screen Space Shadows

Upstream pin: `d330bf12d`. Code: `features\ScreenSpaceShadows`, consumers in
`package\Shaders\DFLight.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| World-only SSS with ordinary projection depth | Both Bend point samples map first-person depth (`raw <= 0.01`) to far depth `1`; world depth becomes `raw * 1.01 - 0.01` | FO4 merges first-person and world projections into deferred depth; the dispatch light coordinate uses the world projection | `RaymarchCS.hlsl` `GetWorldShadowDepth`, `bend_sss_gpu.hlsli` depth reads; shared classification/remap in `Common\DepthPartition.hlsli`, also used by DeferredPosition and SSGI |
| First-person forward lighting does not consume SSS | First-person pixels remain white in the cleared mask through Bend's far-depth return after its group barrier; consumers stay unchanged | FO4 first person uses deferred lighting, so it must neither receive nor cast world SSS | `bend_sss_gpu.hlsli` `WriteScreenSpaceShadow`, `ScreenSpaceShadows.cpp` white clear |

[upstream]: https://github.com/community-shaders/skyrim-community-shaders

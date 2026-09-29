# Deviations from upstream

Upstream [Skyrim Community Shaders][upstream] is the specification for ported features (see `AGENTS.md`).
This file lists every place a feature knowingly differs from its pinned upstream revision, why Fallout 4
forces the difference, and where it lives. Each code site also carries a one-line FO4 marker comment.

- **Translation**: same upstream behavior, re-expressed for a Fallout 4 engine difference.
- **Not supported**: upstream behavior that has no Fallout 4 equivalent without new FO4-only machinery.
- **Pending**: upstream behavior not ported yet.

Engine evidence refers to rows in `fallout4-re` `docs\engine-facts.md`.

## Dynamic Cubemaps

Upstream pin: `d330bf12d`. Code: `features\DynamicCubemaps`, consumers in `package\Shaders\BSWaterShader.hlsl`,
`package\Shaders\BSDFCompositeShader.hlsl`, `package\Shaders\BSDFLightShader.hlsl` and
`package\Shaders\DFTiledLighting.hlsl`.

### Translations

| Upstream | Fallout 4 | Why | Where |
|---|---|---|---|
| Capture before the deferred composite | Capture and publication run after the Forward cloud group (`RegisterPostForwardSky`) | FO4 draws the sky inside `DrawWorld::Forward`, after the composite ("Render-hook anchors", "Sky and clouds") | `DynamicCubemaps.cpp` `Load` |
| Capture the main color target | Geometry radiance is rebuilt as `3 · albedo · (diffuse A + diffuse B) + emissive`; sky pixels come from scene color | FO4 has no diffuse-only target; scene color contains specular, probe and SSLR reflections, which made the cube view-dependent ("Render targets & engine state") | `CaptureCommon.hlsli` |
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

### Not supported

| Upstream | Why |
|---|---|
| Dynamic reflections on deferred materials through sentinel cubes, TruePBR and complex materials (`Reflectance` target) | FO4's deferred cube array rejects cubes narrower than 128 px, so 1×1 sentinels never reach the composite; the G-buffer has no F0/reflectance channel. Needs new prepass machinery and a render target |
| Forward `Lighting.hlsl` sentinel path | FO4 world and first-person accumulators emit no forward BSLighting passes |
| Dynamic Cubemap Creator (sentinel DDS export) | Nothing in FO4 can consume sentinel cubes |

### Pending

| Upstream | Notes |
|---|---|
| `EnabledSSR` setting and `ENABLESSR` gate | Needs the byte-identical `BSImagespaceShaderSSLRRaytracing` reconstruction to own the shader, replacing Upscaling's decompile and hook |

[upstream]: https://github.com/community-shaders/skyrim-community-shaders

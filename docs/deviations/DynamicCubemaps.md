# Dynamic Cubemaps

Rules, Kind legend and cross-cutting records: [README](README.md).

Upstream pin: `d330bf12d`. Code: `features\DynamicCubemaps`, consumers in `package\Shaders\Water.hlsl`,
`package\Shaders\DFComposite.hlsl`, `package\Shaders\DFLight.hlsl` and
`package\Shaders\DFTiledLighting.hlsl`.
Feature classification: **extension** (rule 3: material-selection Divergence).
Upstream's sentinel-cube material-selection design is not supported on FO4, so an
FO4-specific selection mechanism replaces it. Current water/wet-film routes are implemented;
the general authored-material selection design remains Pending.
Reflection availability, the SSLR toggle and preview are minor FO4 adjustments.

All files in upstream `features/Dynamic Cubemaps/Shaders/DynamicCubemaps` are staged unchanged
from the shared fork. Only `CaptureCommon.hlsli` and `DynamicCubemaps.hlsli` have the generic seams
listed in [shared seam edits](README.md#shared-seam-edits); accumulation, lighting-change detection, inference, 16-sample GGX and BC6H remain
shared. Prepared samples explicitly distinguish geometry/sky, so the default lighting detector
still excludes sky while reflection accumulation includes it. FO4 has no local kernel copies.
Inference/filter texel addressing, roughness × 8 on
seven-mip BC6H textures, and bilinear capture depth retain the pinned behavior; no new Fix is applied.

## Translations

| Kind | Upstream | Fallout 4 | Why | Where |
|---|---|---|---|---|
| Divergence | Authored 1×1 sentinel envmaps, Creator export and the Reflectance target select materials for dynamic reflections | Current FO4 selection uses the water REFLECTIONS route and positive wet-film reflectance in deferred composite, not authored sentinel cubes. General authored-material selection: **Pending**, replacement design not yet decided | Native cube-array registration rejects widths below 128 and the G-buffer has no reflectance/F0 channel (engine-facts Cubemap / Render targets). The FO4-specific selection replaces an unsupported upstream design; unchanged capture/filter kernels do not establish material-selection parity | `Water.hlsl` `surfaceColor`; `DynamicCubemaps.cpp` `ResolveReflectionMode`; `FO4/DynamicCubemaps/Composite.hlsli` `GetWetnessReflection`; `DFComposite.hlsl` wet-reflection consumers |
| Forced | Capture before the deferred composite | Capture and publication run after the Forward cloud group (`RegisterPostForwardSky`) | FO4 draws the sky inside `DrawWorld::Forward`, after the composite; engine-facts Secondary scene views | `DynamicCubemaps.cpp` `Load` |
| Forced | Capture the main color target | Preparation rebuilds geometry as `3 · albedo · (diffuse A + diffuse B) + emissive`; sky comes from scene color | FO4 has no diffuse-only target; engine-facts Deferred composition; reconstructed `DFComposite.hlsl` diffuse/emissive composition | `FO4/DynamicCubemaps/PrepareCaptureCS.hlsl` |
| Forced | Sky depth reconstructs a finite far-plane position | Preparation places depth `1.0` on the camera far-plane direction | FO4's world projection has an infinite far plane; inverse projection at 1 has zero homogeneous w. A finite history position preserves upstream sky capture; engine-facts Camera matrix builder and `Engine.h` `TryGetWorldSceneProjection` | `PrepareCaptureCS.hlsl` |
| Forced | Single world projection and near cutoff 16.5 | Decode native world partition, exclude first-person pixels, preserve 16.5 and bilinear sampling | Engine-facts Depth & units: first-person occupies the near partition; unchanged capture cannot interpret the two projections | `PrepareCaptureCS.hlsl`, `FO4/Depth.hlsli` |
| Forced | `FrameBuffer::WorldToView(-s)` with `z < 0` | Preparation tests `z > 0` on `s`, retaining native cube-face orientation | FO4 views down +Z; engine-facts Camera matrix builder | `PrepareCaptureCS.hlsl`, `FO4/DynamicCubemaps/CubemapCommon.hlsli` |
| Forced | Skyrim frame-buffer camera and capture origin | b4 world+jitter camera; preparation receives only the native inverse projection; prepared capture receives the eye origin and per-stream previous eye origin | Engine-facts Camera cache ownership / Per-frame buffer sources: camera origin includes inverse-view translation plus position-adjust anchor; b12 is diagnostic only | `FrameBuffer.cpp`, `DynamicCubemaps.cpp` `UpdateCubemapCapture`, shared `CaptureCommon.hlsli` prepared seam |
| Forced | `IrradianceToLinear`/`IrradianceToGamma`, `ReflectionNormalisationScale` | b6 selects unchanged Color's linear-lighting branch, identity conversions and scale `1.0` | FO4 native HDR is working-linear; engine-facts Deferred composition. Neutral host lighting inputs avoid a second gamma conversion | `SharedData.cpp` `PackFeatures`, unchanged `Common/Color.hlsli` |
| Forced | `Color::Ambient(SharedData::GetAmbient(R))` | b5 pre-power DALC SH plus b6 `ambientGamma=2.2`, `ambientMult=1` | Engine-facts Directional ambient evaluation power 2.2; no local GetAmbient rows or normalization implementation | `Engine.h` `TryGetDirectionalAmbientRows`, `SharedDataLayout.h` `PackAmbientSH`, `SharedData.cpp` |
| Forced | Lighting-change detection from Skyrim directional light | b5 FO4 sun radiance, b6 `isDirLightLinear=1`; unchanged upstream detection | Engine-facts Sun/light sources: the deferred sun input is already linear | `SharedData.cpp`, shared `DetectCaptureLightingCS.hlsl` |
| Tweak | `activeReflections` from Skyrim's reflections prepass | Exterior water always uses the reflections variant | Retain main's exterior-water technique policy | `DynamicCubemaps.cpp` `ResolveReflectionMode` |
| Forced | Active variant infers uncaptured directions from the engine reflection cube | Without the engine cube, retain scene sky with fake-variant persistence | fallout4-re `docs\engine-facts.md`, Cube-mode gate: `bUseCubeMapReflections` defaults to 0 in OG/NG/AE and no pinned INI enables it. The native cube is unavailable by default; the boundary uses the existing shared fallback, not a different inference algorithm | `DynamicCubemaps.cpp` `UpdateShader`, `InferShader` |
| Forced | Water blends the dynamic cube with `CubeMapTex` | Blends with water's sky-gradient reflection color | FO4 reconstructed reflection permutations shade a sky gradient | `Water.hlsl` `surfaceColor` |
| Forced | `WATER` permutation define | Defined locally for contributed Dynamic Cubemaps | FO4 reconstructed water compiles without it | `Water.hlsl` |
| Forced | Deferred cubes at CS t5–t7 with LinearSampler | Shared register seam: PS t34–t35, native probe samplers; water keeps t30–t31 | FO4 composite declarations occupy lower slots and samplers; reconstructed `DFComposite.hlsl` resource declarations | `FO4/DynamicCubemaps/Composite.hlsli`, `DynamicCubemaps.cpp` |
| Forced | Compile-time `INTERIOR` selects deferred base/reflection cube | Runtime b5 `InInterior` selects the cube, then calls shared normalized irradiance | FO4 reconstructed permutations serve both cell types | `FO4/DynamicCubemaps/Composite.hlsli` `GetFinalIrradiance` |
| Forced | Wet reflectance written to G-buffer | Existing composite film/view-space adapter calls upstream cubemap normalization | FO4 G-buffer has no reflectance channel; engine-facts Render targets. Wetness-specific code stays in the existing adapter | `FO4/DynamicCubemaps/Composite.hlsli` `GetWetnessReflection` |
| Forced | Wet indirect-diffuse reduction in material pass | Applied in BSDFLight and DFTiledLighting | FO4 reconstructed light passes evaluate indirect diffuse | `FO4/WetnessEffectsConsumer.hlsli` `GetIndirectDiffuseWeight` |
| Framework | Always-on feature with JSON/ImGui configuration | load=false, TOML/live enabled, forwarding-only UI, ownership/stock identity, telemetry and fail-closed GPU scopes | Repository activation, persistence and runtime-safety contracts; live enable gates wet diffuse/reflection | `DynamicCubemapsSettings.h`, `DynamicCubemaps.cpp`, `FO4/DynamicCubemaps/Composite.hlsli` |
| Tweak | Loaded DC defines ENABLESSR, labeled for water | Live enabled_ssr gates all SSLR when DC is enabled; baseline remains stock | Main's global toggle avoids recompilation; SSLR feeds surfaces and water in reconstructed shaders | `DynamicCubemaps.cpp`, `FO4SharedData.hlsli`, `ISSSLRRaytracing.hlsl` |
| Tweak | No equirectangular display preview shader | FO4-only capture/filtered Reinhard preview at a non-colliding path | Additional display-only diagnostic, not shared capture behavior | `FO4/DynamicCubemaps/CubemapPreviewCS.hlsl`, `DynamicCubemaps.cpp` `RenderCubemapPreview` |

## Not supported

| Kind | Upstream | Why / evidence | Where |
|---|---|---|---|
| Forced | Native deferred 1×1 sentinel-cube and Reflectance-target contract | FO4 cube-array registration rejects cubes narrower than 128 px and its G-buffer has no reflectance/F0 channel; fallout4-re `docs\engine-facts.md`, Cubemap / Render targets. Upstream's authored selection cannot use these native paths unchanged; the replacement selection is the Divergence above | `DFComposite.hlsl`, native cube-array registration and G-buffer layout |
| Forced | Forward `Lighting.hlsl` sentinel path | FO4 world and first-person accumulators emit no forward BSLighting passes; engine-facts DrawWorld pass ownership. No forward consumer is silently substituted | Native Lighting family, deferred consumer catalog |

## Pending

| Kind | Upstream behavior | Remaining work | Where |
|---|---|---|---|
| Pending | General authored-material selection and TruePBR/complex material reflectance | Decide and implement the FO4-specific selection/metadata design recorded as Divergence above. Existing water/wet-film routes do not provide general authored selection or complete optional material coverage | `DFComposite.hlsl`, `FO4/DynamicCubemaps/Composite.hlsli`, engine-facts Cubemap and Render targets |
| Pending | Material authoring/export workflow replacing Dynamic Cubemap Creator sentinel DDS export | Define the authoring workflow after choosing the FO4 selection contract; exporting upstream 1×1 sentinel cubes alone cannot select native deferred materials | `DynamicCubemaps.cpp`, upstream `DynamicCubemaps.cpp` creator/export path |
| Pending | IBL and Skylighting feature consumers | Host providers remain unimplemented; retain shared implementations and complete host inputs when those features are added | shared `DynamicCubemaps.hlsli`, b6 IBL/Skylighting blocks |
| Pending | Runtime equivalence | Authorized DevBench/RenderDoc batch must verify prepared radiance/positions, cube orientation under rotation/translation, sky/infinite-far handling, interior/base and exterior/reflection selection, dynamic resolution, wet reflections, reset/time transitions and native bindings/restoration | `PrepareCaptureCS.hlsl`, `DynamicCubemaps.cpp`, water/composite consumers |

## Upstream PR candidates

- `CaptureCommon.hlsli`: prepared per-face inputs and explicit capture origin allow different host
  cameras/radiance sources without duplicating accumulation or lighting-change detection. Generic
  seam only; Skyrim defaults are unchanged. No PR filed.
- `DynamicCubemaps.hlsli`: configurable cube registers and extracted explicit-sampler normalized
  irradiance preserve existing arithmetic while supporting native deferred consumers. No PR filed.
- `InferCubemapCS.hlsl:22`, `SpecularIrradianceCS.hlsl` `GetSamplingVector`: integer texel/extent
  addresses cube edges rather than centers. Retained exactly; candidate fix is adding 0.5.
- `DynamicCubemaps.hlsli` roughness × 8: compressed publication has seven mips, so high roughness
  clamps before the intended nine-mip filter chain. Retained; align BC6H publication and consumer LOD.
- `CaptureCommon.hlsli` `SampleCapture`: bilinear depth can blend foreground/sky or projection
  partitions into a nonexistent surface. Retained; evaluate point depth or geometry-aware sampling.


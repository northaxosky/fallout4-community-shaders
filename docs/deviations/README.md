# Deviations from upstream

Upstream [Skyrim Community Shaders][upstream] is the specification for ported features (see `AGENTS.md`).
These files list every place a feature knowingly differs from its pinned upstream revision, the reason,
and where it lives: cross-cutting records here, one file per feature below. Each code site also carries
a one-line FO4 marker comment.

- **Translation**: same upstream behavior, re-expressed for a Fallout 4 engine difference.
- **Not supported**: upstream behavior that has no Fallout 4 equivalent without new FO4-only machinery.
- **Pending**: upstream behavior not ported yet.

Core/extension classification and its rules live in [FEATURES](../FEATURES.md#classification); it judges
design per slice, not by counting row Kinds. A Forced row can still be an extension slice when it adds
machinery upstream lacks.

Each row has a Kind:

- **Forced**: a Fallout 4 engine difference leaves no alternative. Cite the engine evidence.
- **Fix**: a deliberate correction of an upstream bug, listed as an upstream PR candidate.
- **Framework**: a repository-wide host contract (activation, TOML persistence, forwarding-only UI, shader ownership, telemetry), not feature behavior.
- **Pending**: upstream behavior not ported yet.
- **Tweak**: a minor FO4 adjustment under rule 2; never affects classification.
- **Divergence**: FO4's own architecture/algorithm or substantive behavior upstream lacks (rule 3).

Core classification is not a claim of complete parity; Pending work remains unfinished.

## Shared seam edits

Shared pin: the `extern\community-shaders-shared` submodule commit; upstream base `d330bf12d`.
FO4 consumes unchanged files through `xmake\shared.lua`; shared include paths cannot be replaced.
The native entry-point naming boundary is documented under Shader replacement.

| Kind | File | SHA | Why | Upstream PR status |
|---|---|---|---|---|
| Framework | `src/Features/PerformanceOverlay.h`, `src/Features/PerformanceOverlay/{CircularBuffer,DrawCallRow}.h`, `src/Features/PerformanceOverlay/ABTesting/ABTestAggregator.{h,cpp}` | `6fd72a4a5` | Split portable history/timing rows from the Skyrim feature header so hosts can consume them without its engine dependencies | In the shared fork; upstream PR candidate, not filed |
| Forced | `package/Shaders/Common/FrameBuffer.hlsli` | `e305ed0a4` | `FRAMEBUFFER_REGISTER` defaults to b12 and permits host binding at b4; FO4 engine shaders already bind the native per-frame buffer at b12 (`package/Shaders/Water.hlsl:3`, `cbuffer PerFrame : register(b12)`) | In the shared fork; no upstream PR recorded |
| Fix | `features/Screen Space GI/Shaders/ScreenSpaceGI/blur.cs.hlsl:104` | `13d9d2e2d` | Scale the center-normal UV by `frameScale`, matching the neighbor lookups in half/quarter-resolution dynamic-resolution frames | community-shaders/skyrim-community-shaders#2795 |
| Fix | `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ScreenSpaceShadows.hlsli:7` | `6f81ebc25` | Remove the extra half-pixel offset from pixel-centered SV_POSITION before integer mask lookup | In the shared fork; upstream PR candidate, not filed |
| Fix | `features/Water Effects/Shaders/WaterEffects/WaterParallax.hlsli` | `eacfd1c6a` | Seed the parallax march with the real height at offset 0 instead of 1.0, so alpha-less normals give zero offset | community-shaders/skyrim-community-shaders#2837 |
| Fix | `features/Water Effects/Shaders/WaterEffects/WaterParallax.hlsli` | `d456e9c5` | Anchor the parallax march to the mean height from each normal map's smallest mip, so the mean surface sits on the water plane | community-shaders/skyrim-community-shaders#2838 |
| Forced | `features/Dynamic Cubemaps/Shaders/DynamicCubemaps/CaptureCommon.hlsli` | `83efe1ad9` | Optional prepared position/color/UV inputs, geometry/sky tags and capture origin isolate FO4's +Z, partitioned depth, infinite far plane and diffuse reconstruction; default Skyrim sampling/history are unchanged | In the shared fork; upstream PR candidate, not filed |
| Forced | `features/Dynamic Cubemaps/Shaders/DynamicCubemaps/DynamicCubemaps.hlsli` | `83efe1ad9` | Optional cube registers/custom-consumer guard and extracted explicit-sampler normalization let native FO4 deferred slots/samplers call shared arithmetic; default Skyrim registers/consumers are unchanged | In the shared fork; upstream PR candidate, not filed |

## Shared consumption boundary

Shared consumption includes byte-identical SSS shaders, RCAS, shader licenses, default cubemap, the
entire ScreenSpaceGI shader/noise directory and water caustics assets from the shared pin.
All seven ExponentialHeightFog shaders and the Color, FastMath, GBuffer, Shading, Random,
IBL and Skylighting dependencies are also staged unchanged.
Bend's CPU header is identical modulo comments; its
existing SSS consumer uses the unchanged shared header. PerformanceOverlay uses shared QPC/FPS
helpers, the profiler and the A/B aggregator. `src/Shared/PerfUtils.h` supplies Windows declarations
and scopes MSVC C4267 suppression for the upstream vector mean; the global PCH is unchanged.

| Kind | Difference | Where |
|---|---|---|
| Framework | Windows declarations and a scoped C4267 suppression adapt the unchanged portable header to FO4's `/W4 /WX` build | `src/Shared/PerfUtils.h` |

Remaining FO4-owned files are listed below; deleted copies consumed unchanged from upstream are
not deviations. Paths are relative to the staged `Shaders` root. Kinds describe each live file,
not its relocation. Upscaling's in-house shaders remain core by rule 4 and are not scheduled for
upstream conversion; renderer-specific reasons remain in the feature tables.

| Kind | Upstream destination | FO4-owned destination |
|---|---|---|
| Forced | `DynamicCubemaps/DynamicCubemaps.hlsli` | `FO4/DynamicCubemaps/DynamicCubemaps.hlsli` is a consumer include; the custom-consumer guard lets callers supply native samplers to shared functions |
| Divergence | `Upscaling/DepthRefractionUpscalePS.hlsl` | `FO4/Upscaling/DepthRefractionUpscalePS.hlsl` |
| Divergence | `Upscaling/EncodeTexturesCS.hlsl` | `FO4/Upscaling/EncodeTexturesCS.hlsl` |
| Divergence | `Upscaling/UpscaleVS.hlsl` | `FO4/Upscaling/UpscaleVS.hlsl` |

## Shader replacement

Upstream: `ShaderCache`, `Hooks`, `State`, `AdvancedSettingsRenderer`.
Source paths follow the native fxp name; entry point is `main`, profile follows the stage,
and live type/master switches suppress the entire replacement, including feature contributions.
Effect and DistantTree remain owned for upstream fog and terrain-shadow consumers.
Utility, Sky, Particle, BloodSplatter and Lighting remain stock because no current feature consumes them.

| Kind | Upstream | Fallout 4 translation | Engine evidence / boundary | Code |
|---|---|---|---|---|
| Forced | `BSShader::shaderType` identifies each shader class | Use CommonLibF4's `BSShaderManager::ShaderEnum`; disambiguate type 4 by exact `DFPrepass` / `DFLight` fxp name | fallout4-re `docs\engine-facts.md`, “Batch index is shader type”: both constructors write 4 at `BSShader+0x18`, although CommonLibF4's enum lists DFLight as 5. `harness\shaders\section_partition\native.py` records the exact names. Standalone compute uses its existing loader-recorded name and target | `ShaderInjection.cpp` `ResolveNativeShaderTarget` |
| Forced | Root-level native shader entry points | Stage FO4 Effect, DistantTree and Water under their engine names, despite Skyrim having identically named entry points | The schema-2 measured export identifies these exact fxp names; their reconstructed native registers, descriptors and stock bytecode differ from Skyrim. The engine-name ownership contract requires these root names. No shared include is overridden, and staging still rejects duplicate consumed destinations | `xmake\shaders.lua`, `ShaderFamilyDescriptor.cpp`, `StockShaderIdentityTests.cpp` |
| Framework | Source presence determines ownership | The existing developer force-on root remains the effective source root | Repository developer override contract; no additional source lookup path | `ShaderInjection.cpp` `ResolveShaderRoot`, `ShaderFamilyDescriptor.cpp` `IsShaderSourceAvailable` |
| Framework | No stock reconstruction identity gate | FO4CS-only CI compares reconstructed routes against measured stock bytecode | Schema-2 fallout4-re export supplies native names; the gate shares runtime source-presence ownership and compilation with all type toggles on. Unnamed routes remain unowned; unhooked HS/DS routes are excluded. A second table gates OG 1.10.163 with `OG=1` under its 6.3.9600.16384 proof compiler; rows the OG export proves under the AE compiler are byte-identical to AE and excluded as inherited | `StockShaderIdentityTests.cpp`, `generate_stock_shader_identity.py` |

Stage selection uses upstream `VSHADER` / `PSHADER` / `CSHADER` defines. SSLR supplies both
stages through unchanged shared `Common\DummyVSTexCoord.hlsl`, as `ISReflectionsRayTracing` does.
The gate checks every owned route, including passthrough VS ordinal 3881
(`89e56423886dc05ed3b2d1445a70f386c651ac38`) and PS ordinal 3882. Compile failures remain
runtime stock fallbacks but fail the identity gate; they are not reclassified as unowned.
The runtime compiles OG variants with the system compiler; only CI uses the OG-era compiler,
and OG output differs from native only in texture-sample scheduling on some routes.

## Shader contribution

Upstream: `Feature.h:60–77`, `ShaderCache.cpp` define builders,
`Deferred.cpp:211–296/730–740`, `State.cpp` `Draw`, and `ScreenSpaceShadows.cpp` `Prepass`.
Loaded features declare their define, options and consumer families. Live effect settings and GPU
readiness do not change those defines. Per-feature declarations also drive the offline variant sweep;
no second consumer-family list is maintained.

| Kind | Translation | Evidence / boundary | Code |
|---|---|---|---|
| Forced | FO4 deferred prepass/light/composite/tiled families replace Skyrim forward Lighting consumers | Native `DFPrepass`, `DFLight`, `DFComposite` and `DFTiledLighting` receipts; engine-facts shader-family/pass census | Per-feature `ShaderDefines.h`, `ShaderInjectionCompileRequest.cpp` |
| Forced | Bind persistent resources after Begin's ClearState, at the first world prepass; rebind after producer RTV/UAV use and compute cleanup | AE `Main::Swap` calls Begin at `0xC3328E`; engine-facts high-slot writer census finds no mid-frame engine PS/VS/CS t16+ or PS/CS b3–b11 writes. D3D11 removes SRVs aliasing output resources | `Feature::Prepass`, `SharedData.cpp`, feature producer callbacks, `ComputeScope.cpp` |
| Forced | Keep VS substrate at b4–b7; never use VS b10/b11 | Engine-facts VS constant-buffer census: b10/b11 are rewritten mid-frame | `SharedData.cpp`, `SubstrateSlots.h` |
| Forced | ISL PS b11 remains per light; CS t8 is bound/restored at native tiled dispatch | Raster geometry selects each light; the engine owns low CS inputs and rebuilds the dense tiled list | `InverseSquareLighting.cpp`, `ShaderInjection.cpp` compute bridge |
| Framework | Fullscreen-debug options apply only to the selected owner's composite consumers; selection changes invalidate only affected native target lookups and retain define-keyed compiled variants | Upstream LightLimitFix toggles LLFDEBUG through SetDefines and clears Lighting on visualization activation changes. Static declarations and runtime features share the same interface; offline sweeps explicitly select debug owners | `ShaderDefineProvider.h`, `Feature.cpp`, `ShaderInjection.cpp` |
| Framework | Stock identity, shader ownership and post-freeze delivery validation remain independent of define selection | Repository shader-delivery contract; a resource failure does not silently compile a different feature set | `ShaderInjection.cpp`, `Feature.cpp` |

Sampler verdicts are **forced per-draw binds**, not family-native reuse:

| Consumer | Slot / required mode | Native evidence |
|---|---|---|
| Fog composite | s13, linear clamp | `DFComposite.hlsl` declares native s0–s12/s14/s15, not s13 |
| Fog water/effect/distant tree | s15, linear clamp | `Water.hlsl` native samplers are s1–s7/s9/s10; `Effect.hlsl` s0–s2/s4–s7; `DistantTree.hlsl` s0 |
| Terrain light/composite/water/effect/distant tree | s13, linear clamp | None of these reconstructed families declares a native s13 |
| Water caustics, light | s14, linear wrap | `DFLight.hlsl` declares s0–s5/s7, not s14 |

Engine-facts “Invalidate dirties all PS slots” and “PS sampler shadow inputs” show why undeclared
samplers are not reliable defaults: AE `Invalidate` at `0x182B030` dirties s0–s15, and
`SetDirtyStates` at `0x18247D0` skips the sampler loop when the dirty mask is zero; otherwise
`0x18247E0–0x1824831` clears and submits each dirty slot through PSSetSamplers (`0x1824829`).
Direct overrides do not dirty the shadow state, so restoration is required before later native draws.
Compiled bytecode supplies a cached sampler-use mask: unused permutations never query or overwrite
samplers, matching native samplers need no restoration, and repeated identical writes within a draw
are suppressed. No cross-draw sampler cache, runtime slot claims or write batching remain.
The feature-on shader sweep checks overlapping shader registers.

No chosen shader-contribution behavior divergence remains. Producer compute scopes still preserve
their temporary inputs; these are not consumer-draw snapshots. `shader_injection` reports completed
`draw_frame`, `frame_binding_checks`, `frame_binding_lost`, `frame_binding_lost_slots` (for example
`ps_t45,vs_b4`) and cumulative `frame_binding_lost_total`. Verification samples the first consumer
per family/stage after each publication, never repairs state, and preserves failed-frame counts.
An authorized game/RenderDoc run must still verify zero losses, forward consumers, and
godrays/HBAO+ save/restore behavior.

## Feature loading

Upstream: `Feature.cpp` `Load`, `State.cpp` `Load`/`Setup`,
`Menu.cpp` `DrawDisableAtBootSettings`, and `ShaderCache.cpp` `ValidateDiskCache`.

| Kind | Upstream | Fallout 4 | Boundary / code |
|---|---|---|---|
| Framework | Installed/version-compatible features load unless “Disable at Boot” applies; changes take effect after restart | TOML `load` selects activation at startup; shipped defaults remain false and edits require restart | `FeatureManager::PrepareAll`/`ActivateAll`, `FeatureConfig.cpp`; no live load/unload operation is added |
| Framework | Loaded features use live settings for effect enable/disable, not compile-time readiness predicates | Live settings update shader/producer inputs without removing feature defines or resources; runtime health/quarantine remains separate from successful startup loading | `Feature.h`, `FeatureBuffer.cpp`, per-feature settings |
| Tweak | Plugin/feature validation in `ShaderCache/Info.ini` invalidates the entire disk cache | Recipe keys include effective defines and source dependencies; a different loaded set gets different keys without wiping unrelated entries | `ShaderVariantRecipe.cpp`, `Utils/ShaderCache/ShaderRecipe.cpp`; equivalent shader identity, different cache organization |
| Framework | Loaded features run `SetupResources` after renderer initialization; feature resources persist and are recreated as needed, not unloaded by live off | `OnD3D11ReadyAll` initializes GPU resources; feature-owned RAII resources handle replacement; live off does not uninstall hooks or unload features | `Feature.cpp`, `D3D11Bootstrap.cpp`; callback failures quarantine rather than tearing down partial hooks |
| Framework | JSON layering: Default → User → Overrides → User Overrides; settings are saved separately from feature installation | Canonical TOML deep-merges the sibling User TOML; live edits save deltas and preserve startup-only fields | `FeatureConfig.cpp`, `SettingsPersistence.h`, `LiveSettings.h` |
| Framework | Restart-field introspection and “available after restart” UI | `GetRestartSettings`, pending load markers and forwarding-only DearModdingUI rows expose the same restart boundary | `Feature.h`, `HostClient.cpp` |

The normal upstream settings workflow does not live-load features. Its diagnostic RemoteControl
bridge can flip `Feature::loaded`; that is not resource setup/teardown and is not a loading API to port.

## Substrate

FrameBuffer, SharedData,
SphericalHarmonics and its Math dependency are staged byte-for-byte. The pinned b6 ABI contains
**20** blocks, including HorizonFixSettings; all 20 are mirrored in upstream order, and absent
features leave zero blocks, except the host's neutral linear-color policy in the upstream
LinearLighting block. The relocated `FO4/Common/SharedData.hlsli` is deleted.

Engine evidence below refers to fallout4-re `docs\engine-facts.md`.

| Kind | Difference | Evidence / reason | Where |
|---|---|---|---|
| Forced | Current world+jitter cache record supplies every rendering camera; b12 Map/Unmap is only a telemetry cross-check | Camera, matrices & world offsets: cache ownership and main preparation; cache +0x140, stride +0x250, keys +0x238/+0x240 on OG/NG/AE. AE proof: 5,614 prepass-record/b12 comparisons, maximum relative difference 0. Cache growth requires reacquiring and copying each call | `FrameBuffer.cpp` `GetWorldCameraRecord`, camera consumers, `Telemetry.cpp` |
| Forced | Capture the world camera before deferred prepass draws; publish canonical depth and refresh feature data afterward | Engine-facts Main camera preparation: MainRenderSetup prepares world+jitter and advances history before DeferredPrePass on OG/NG/AE. Fog/terrain prepare b6 afterward without advancing timer/resolution history twice. Missing early snapshots remain retryable; the moved capture requires an in-game b12 comparison | `FrameBuffer.cpp` `CaptureWorldCamera`, `SharedData.cpp` `UpdateSharedData` |
| Forced | Engine row-vector matrices are transposed into upstream's `row_major mul(Matrix,v)` b4 contract | Per-frame buffer sources: native forward/inverse upload transposes; registers 37–40 are unjittered, not the jittered VP | `SharedDataLayout.h` `PackFrameData` |
| Forced | FrameBuffer binds at b4 instead of upstream's default b12 | FO4 reconstructed shaders own b12 (`Water.hlsl` native PerFrame); the shared register seam leaves their bytecode unchanged. b4/b7 are unused by reconstructed/native injection targets; stock DXBC identity, feature-off reflection and slot-clash tests enforce this | `SubstrateSlots.h`, utility compiler, injection compile request and cache recipe |
| Forced | One R32_FLOAT boundary pass publishes canonical world-projection depth at t17; near pixels reproject through shadow +0x8A0, world pixels use `mad(d,1.01,-0.01)`, sky uses 1 | Depth & units / Per-frame buffer sources: FO4 combines first-person and world projections; prepass OG/NG/AE writes transpose(inverse(first-person jittered projection)) at shadow +0x8A0. Native targets use t0–t15, not t17 | `CanonicalDepth.cpp`, `FO4/CanonicalDepthCS.hlsl`, `FO4/Depth.hlsli`, `Engine.h` near accessor |
| Tweak | Preserve upstream's scalar X clamp offset and Y clamp-to-ratio; snapshot current/previous ratios once per substrate update | Dynamic-resolution history: native clamp is `r−0.5/size` on NG/AE and `(trunc(size·r)−1)/size` on OG, size from logical target 1. These facts do not require the scalar clamp policy; it preserves upstream semantics rather than main's two-axis clamp. Substrate history is per-frame rather than effect-update history. OG publishes ratio 1: its deferred, water and tiled-lighting shaders read no dynamic-resolution constants | `SharedData.cpp`, unchanged `Common/FrameBuffer.hlsli` |
| Forced | Pack world-channel DALC into pre-power SH, then apply FO4's 2.2 power once at linear consumer boundaries | Directional ambient transform/evaluation rows: native world-channel columns include transform scale and bias; native lighting evaluates power 2.2. SH is `(b/Y00,−ay/Y1,az/Y1,−ax/Y1)` with Y00=0.2820948, Y1=0.4886025. Upstream State.cpp does not gamma-convert before packing; unchanged GetAmbient is pre-power | `Engine.h` `TryGetDirectionalAmbientRows`, `SharedDataLayout.h` `PackAmbientSH`, `FO4/FO4ShaderData.hlsli` `GetAmbientLinear` |
| Forced | b6 publishes neutral linear-lighting inputs, with native DALC power 2.2 and an already-linear sun | FO4 deferred HDR/sun inputs are linear; directional ambient alone is pre-power. These host inputs let unchanged Color/cubemap kernels consume the same encoding as the reconstructed consumers | `SharedData.cpp` `PackFeatures`, Dynamic Cubemaps translations |
| Framework | One 48-byte FO4-only b7 holds host fullscreen debug owner/mode/params, EnabledSSR, EnabledDynamicCubemaps and DeltaTime; one host PS t61 serves the selected debug texture | Repository-wide single-substrate ABI; debug contributors claim no slots. Water tiles use b5; DR and NDC-to-view use b4; terrain enable fields use b6. Upstream b6 cubemapCreatorSettings retains Creator-mode semantics and stays zero | `SharedDataLayout.h`, `FO4/FO4SharedData.hlsli`, `FO4/DebugViewOwners.h`, `SharedData.cpp` |
| Pending | FrameParams is zero; unvalidated celestial/HDR/map/shadow fields retain upstream absent values | No validated FO4 inverse-gamma/frame-flag or corresponding celestial/HDR source is consumed. SunDirection uses toward-light direction, while SunColor remains absent rather than inventing a sky-disc colour; FrameCount follows main's temporal method and AlwaysActive follows engine frame count | `SharedData.cpp` `BuildSharedData`, `SharedDataLayout.h` |

SSS reads canonical depth directly: first-person geometry casts as upstream does but never receives
SSS. Its settings live only in its raymarch cbuffer, not b6 or b7. Feature-off engine variants include
no substrate reads. The frame binder publishes b4–b7/t17 to VS/PS/CS and debug t61 to PS after
ClearState and producer boundaries; native b12 remains engine-owned. Only forced low-slot and OM
overrides use draw scopes. Substrate uploads and debug selection are frame-cached; producers refresh
their packet and bindings when resources change. See Shader contribution for the binding census.

## Upstream PR candidates

- `src/Features/InverseSquareLighting.cpp:76–95`: balanced intensity/cutoff/size
  (`intensity=1`, `cutoff=0.5`, `size=2`) gives radius zero rather than NaN.
  `ProcessLight` then divides by zero for `invRadius`/`fadeZone`, and gameplay
  smoothstep has a zero denominator. Clamp nonpositive/nonfinite radius to a
  documented positive minimum upstream. FO4 retains the pinned zero-radius
  behavior; no upstream PR or local correction is recorded.

- `features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/ScreenSpaceShadows.hlsli:7`:
  shared-fork fix `6f81ebc25` removes the extra 0.5 before truncating pixel-centered `SV_POSITION`.
  Upstream Lighting, DistantTree and all three RunGrass callers pass SV_POSITION unchanged;
  Bend writes `floor(pixel_xy)`. The offset reads the next mask texel in both axes in Skyrim
  and FO4. FO4 uses the corrected shared sampler without a counter-offset.
  Upstream PR candidate, not filed.
- `src/Utils/PerfUtils.h:41`: `Mean` implicitly converts `size_t` to float, raising C4267
  under FO4's `/W4 /WX`; an explicit float conversion preserves its current arithmetic.
  FO4 scopes the warning in `src/Shared/PerfUtils.h`, without changing shared behavior.
- `features/Screen Space GI/Shaders/ScreenSpaceGI/blur.cs.hlsl:104`: the center normal lookup
  needs `frameScale`. Main's correction is retained as shared seam `13d9d2e2d`; upstream PR is
  community-shaders/skyrim-community-shaders#2795. No FO4 shader copy remains.
- `features/Screen Space GI/Shaders/ScreenSpaceGI/gi.cs.hlsl:232`: the experimental specular
  half-angle calculation has inconsistent angular units; upstream issue/PR #2792 records it.
  The pinned behavior remains unchanged.
- `src/Deferred.cpp:362–364`: experimental HQ specular binds null diffuse Y/CoCg SRVs although
  `DeferredCompositeCS.hlsl:46–56` still samples them. FO4 retains that binding; settle the intended
  diffuse/HQ combination upstream rather than silently changing it in the host.
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

## Temporal feature bug findings (no fixes)

No new confirmed FO4 bug was established by this source-only comparison. Raw SDK depth,
zero masks, alpha conditioning and unverified hook coverage are documented differences or
evidence limits, not relabeled as proven bugs. A suspected upstream typed-depth mismatch
(`Upscaling.cpp` encoder u3 binding versus `FidelityFX.cpp` depth dispatch and the
`EncodeTexturesCS.hlsl` R32_FLOAT comment) remains unverified without backend format evidence.

## Features

- [InverseSquareLighting](InverseSquareLighting.md)
- [WaterEffects](WaterEffects.md)
- [ExponentialHeightFog](ExponentialHeightFog.md)
- [Performance Overlay](PerformanceOverlay.md)
- [Terrain Shadows](TerrainShadows.md)
- [LOD Blending](LODBlending.md)
- [Dynamic Cubemaps](DynamicCubemaps.md)
- [RenderDoc](RenderDoc.md)
- [Upscaling](Upscaling.md)
- [FrameGeneration](FrameGeneration.md)
- [MotionVectorFixes](MotionVectorFixes.md)
- [Screen Space GI](ScreenSpaceGI.md)
- [Screen Space Shadows](ScreenSpaceShadows.md)

[upstream]: https://github.com/community-shaders/skyrim-community-shaders

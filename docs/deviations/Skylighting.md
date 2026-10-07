# Skylighting

Rules, Kind legend and cross-cutting records: [README](README.md).

Pin: shared `ca9e8a74b` (upstream `bff82b03e` plus the zenith/fade fix). Consumed unchanged:
`features/Skylighting/Shaders/Skylighting/Skylighting.hlsli` and `UpdateProbesCS.hlsl`; the port
does not stage `Skylighting.ini`. Settings keys, defaults and ranges match the pin: `MaxZenith`
(radians, 0 to 90 degrees, default 90), `MinDiffuseVisibility` and `MinSpecularVisibility`
(0.01-1, default 0.1). The b6 `SkylightingSettings` block defaults to a far `PosOffset` and
visibility 1, so an unpublished or unhealthy block reads unit SH and visibility 1.

Feature classification: **core** (rule 2). Pending rows remain unfinished and do not establish
upstream parity.

## Translations

| Kind | Upstream | Fallout 4 / evidence | Where |
|---|---|---|---|
| Framework | Host activation, settings, UI | `load = false` activation, TOML persistence and forwarding-only DearModdingUI. Typed UAV load support for `R16G16B16A16_FLOAT`, `R8_UINT` and `R8_UNORM` is checked in `OnD3D11Ready`; failure quarantines the feature | `Skylighting.cpp`, `SkylightingSettings.h` |
| Forced | `Main_Precipitation_RenderOcclusion` call-site thunk replaces the stock mask with `SetupMask` and `RenderMask(emitter)` | `RenderHooks` registry detour of `Precipitation::RenderOcclusionMap` (`RegisterPostPrecipitationOcclusion`): the stock pass runs first, then the capture writes box size and direction, pre-calls `ComputeProjection` (A6b primes the camera rotation Impl's own call reads), calls `RenderOcclusionMapImpl(precip, nullptr)` (its +0x4A projection call is `SetupMask`'s) and restores with one stock-direction `ComputeProjection` call. Stock rain keeps its own mask. Evidence: fallout4-re `docs\skylighting-occlusion-capture-contract.md` A1, A6, A6b, A7; the wrapper runs every frame at the anchor, in clear weather too | `RenderHooks.cpp`, `PrecipitationOcclusion.cpp` `CaptureOcclusion` |
| Forced | A dummy `BSParticleShaderRainEmitter` receives `occlusionProjection` | The emitter argument is null; the matrix is read from the engine's occlusion matrix global after Impl and the global is restored (write-only for the engine). Evidence: Contract A6, A8: Impl stores the global unconditionally and the emitter copy only when the argument is non-null | `PrecipitationOcclusion.cpp` |
| Forced | `precip->lastCubeSize` is saved and overridden | Not touched; box size is the typed `Precipitation::GetBoxSize()` global, restored after the capture. Evidence: Contract A6a, A6b: `ComputeProjection` never dereferences `Precipitation*`, and `+0x90` is only read and written by `Precipitation::Update` | `PrecipitationOcclusion.cpp` |
| Forced | The `kPRECIPITATION_OCCLUSION_MAP` depth entry is swapped for `texOcclusion` | The DS8 platform entry (`ResolveDepthStencilTarget`) is swapped for an owned 512x512 `R16_TYPELESS` texture (DSV `D16_UNORM`, SRV `R16_UNORM`) created in `OnD3D11Ready` and restored before the capture returns. RT 86 stays engine-owned: Impl acquires it at 512x512 and forces the viewport to its size, so only DS8 is redirected; an allocated stock DS8 must be 512x512 or the capture is skipped. Evidence: Contract A5, A6 (clear weather leaves stock DS8 untouched; stock rain keeps reading it) | `PrecipitationOcclusion.cpp`, `Skylighting.cpp` `CreateOcclusionResources` |
| Forced | `SetViewFrustum` call-site thunk inside `ComputeProjection` | Same thunk and quadrant rewrite at `ComputeProjection+0x54E` (OG) and `+0x55A` (NG, AE), resolved and byte-checked per runtime, active only while the capture is. Evidence: Contract A6b call-site table | `PrecipitationOcclusion.cpp` |
| Forced | The precipitation mask culls with `AccumulateScene` | A gated call-site thunk on the `BSPreCulledObjects::QEnabled` call inside Impl (`+0x23D` OG, `+0x232` NG, AE) reports off during the capture, because the Umbra rain list is empty in clear weather and is built for the stock direction. Evidence: Contract A2 | `PrecipitationOcclusion.cpp` |
| Forced | `GetPrecipitationOcclusionMapRenderPassesImpl` on `BSLightingShaderProperty` vslot 0x2D | `GetRenderPasses_ShadowMapOrMask` (vslot 44, ids 757582 / 2316434), active only while the capture is and the accumulator is the precipitation occlusion accumulator. The upstream predicate (flags, radius, below-grid, BSX) decides; where the stock builder also accepts its pass list is returned, and any occluder upstream accepts but stock rejects (loaded-cell landscape, whose `kCastShadows` `TESObjectLAND::CreateGeometry` clears; geometry without Cast Shadows; alpha-blended geometry; faded materials) gets an owned `BSUtilityShader` pass. Its technique is the stock builder's: `DetermineUtilityShaderDecl` bits (land bit removed), geometry-type and merge-instanced bits, alpha test, additional alpha mask, plus Sm and Smclamp (`0xC01A` for a plain static or landscape). Counters split own passes by reason. Evidence: Contract A3, A3a, A5; the pass build mirrors the stock else-branch (`RenderPassArray::Add`, `BSUtilityShader::CreateCommandBuffer`) | `OccluderPasses.cpp` |
| Forced | Skinned geometry with tree animation, decals and LOD landscape reach the predicate | `RegisterObject_OcclusionMap` rejects skinned, decal and LOD-landscape properties before vslot 44, so the skinned tree-animation accept is unreachable and LOD landscape cannot be admitted by this hook. Evidence: Contract A3 | `OccluderPasses.cpp` `RejectOccluder` |
| Forced | The replacement builder also governs the stock rain mask (non-capture branch) | The hook is inert outside the capture; FO4 keeps the stock rain occluders. Evidence: Contract A3, A5; stock rain reads stock DS8 | `OccluderPasses.cpp` |
| Forced | Only `BSLightingShaderProperty` is hooked | `BSGrassShaderProperty` keeps the shared stock builder through its own vtable, as upstream leaves Skyrim's grass property unhooked. Evidence: Contract A3, B12 | `OccluderPasses.cpp` |
| Forced | Alpha group pool growth and the `geometryGroups[14]` flag clear | Not ported: FO4's pool is per renderer and saturates, mode 14 registers with group -1/9, and group 14 is clouds. Evidence: Contract A4 | n/a |
| Forced | `Util::IsInterior()` gates the capture | `engine::IsInterior()` (interior cell, or a no-sky or fixed-dimension worldspace) feeds the capture and the b5 `InInterior` flag, replacing `!cell->IsExterior()`. One predicate with upstream semantics; effect on Dynamic Cubemaps is recorded in its deviation file | `World/Sky.cpp`, `SharedData.cpp` |
| Fix | A disc point that rounds past the unit disc takes the square root of a negative number | The frame is skipped when the direction is not finite instead of handing the engine a NaN. Upstream PR candidate, not filed | `Skylighting.cpp` `RenderOcclusion` |
| Framework | None (upstream logs through spdlog and Tracy zones) | Capture log channel `cs.feature.skylighting.capture` (hook install results, resolved targets, one `summary` line every 300 anchor frames, on the first and on every capture-state change), telemetry fields, profiler pass `Skylighting/OcclusionMask` and the `occlusion_depth` texture preview. Evidence: Runtime-safety contract: cached or atomic telemetry, render-thread-only logging | `Skylighting.cpp` |

## Pending

| Kind | Upstream | Remaining work / evidence | Where |
|---|---|---|---|
| Pending | LOD landscape occluders | Rejected by `RegisterObject_OcclusionMap` before vslot 44; admitting it needs a gated detour of that function and a no-texture utility permutation (contract A3a) | `OccluderPasses.cpp` |
| Pending | In-game proof of the capture | Clear-weather capture, quadrant cycle, Umbra bypass, landscape pass layout under the debug layer, box position stability, cost in a dense exterior | RenderDoc, DevBench |
| Pending | P2 probe grid and lifecycle | Probe/accum/bitmask/visibility textures, grid advance, update dispatch, b6 publication, `Rebuild Skylighting` button, loading-screen reset | `Skylighting.cpp`, `FeatureBuffer.cpp`, `SharedData.cpp` |
| Pending | P3 consumers | `SKYLIGHTING` define, forward/deferred diffuse and specular consumers, `FeatureShaderDeclarations.h` entry | `package/Shaders/` |
| Pending | P4 deferred lighting coverage | Directional-shadow split and ambient addends in DFLight | `package/Shaders/` |
| Pending | P5 specular and provider integration | Specular visibility, SSGI/IBL/DynamicCubemaps provider interaction, water | `package/Shaders/` |
| Pending | P6 shadow cascades | Cascade copy, `DirectionalShadowLightData` buffer (272 B stride) and shadow visibility probes | `Skylighting.cpp`, `ShadowLightData.h` |

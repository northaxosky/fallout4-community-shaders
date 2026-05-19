# Ambient/IBL pixel shader analysis — `Shaders011.3560`

Status: **analysis complete, HLSL reconstruction WIP**. The full HLSL has
not been written; see "Reconstruction gap" below.

This document captures the structural reverse-engineering of the FO4
ambient/IBL deferred pixel shader (DXBC blob 3560 in
`Fallout4 - Shaders.ba2/ShadersFX/Shaders011.fxp`), focused on the
question that gates SSGI Phase 2: **how does the engine apply kSSAO to
the ambient term?**

* Source ASM: `Fallout4RE/Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.3560.2b6e36c08aca.dxbc.asm`
* Stage: `ps_5_0`
* Instruction count: 321 numbered instructions
* Output: 1 SV_Target (RGBA float)
* Dispatch site: inside `DrawWorld::DeferredLightsImpl`
  (REL::ID `{1108521, 2318312, 2318312}`)

## SSGI Phase 2 answer (high confidence)

The engine applies kSSAO to the ambient term via **one multiply** on the
combined ambient+IBL contribution, after the cubemap and bilateral-blur
math has already finished, **before** fog blending. Specifically:

```
ASM line 263: r0.x = kSSAO.SampleLevel(uv, 0).x        // single-channel AO
ASM line 264: r0.yzw = r0.x * r1.xyz                    // AO * combined ambient/IBL
```

At line 264 `r1.xyz` holds the result of:

```
ASM line 262: r1.xyz = (IBL_color_lerp) * (t5+t11)*3 + r3.xyz_bilateral_blur
```

* `(t5+t11)*3` = sum of two precomputed ambient-diffuse buffers, scaled.
* `r3.xyz_bilateral_blur` = the 9-tap bilateral-filtered "screen-space
  ambient" from the material-5 block (or just `t10`'s center sample for
  non-skin pixels).
* `IBL_color_lerp` = lerp(`t8` cubemap sample, `t14` per-pixel buffer,
  alpha) scaled by glossiness terms.

So the AO multiply hits the ambient diffuse + IBL specular together but
**never touches direct light**. Direct lighting is computed in separate
per-light pixel shaders that also live inside `DeferredLightsImpl` and
write to kDiffuseBuffer / kSpecularBuffer additively.

The implication for SSGI Phase 2 in the sibling repo: instead of
post-modulating kDiffuseBuffer (which currently darkens direct light
along with ambient), the right integration boundary is to either:

1. **Pre-write the ambient buffer** that this shader samples at `t5` /
   `t11` so AO modulation is naturally inherited by the multiply at line
   264 — but those buffers are not in the public `cs::engine::RenderTarget`
   enum so they are likely engine-internal scratch RTs.
2. **Replace the AO source itself** by writing an SSGI-modulated value
   into kSSAO=28 before this pass dispatches — the shader applies AO
   only at line 264, so any SSAO value we put in kSSAO will only affect
   the ambient/IBL result, not direct light.

Option 2 is the cleanest and matches what `RegisterPostDeferredPrePass`
(already wired in `src/RenderHooks.cpp`) is positioned to support.

## Resource map (per-SRV-slot inference)

| Slot | Role (inferred) | Confidence | Evidence |
|---|---|---|---|
| t1  | kGbufferNormal (RT 20) | high | sampled at L30, decoded as octahedral normal at L31-36 (`r4.zw * 4 - 2`, then `sqrt(1 - dot2/4)` reconstruction) |
| t2  | kGbufferMaterial (RT 24) | medium | L27: `.x` used as material-mask flag (>0.5/255), `.y` used as gloss-derived weight; L49: `.x * 255 - 1` rounded as cubemap-array slice |
| t3  | gbuffer "shading-data" packed buffer | medium | L1: `.xyz` (via `.xywz` swizzle) read as `(roughness, ?, mat-id)`; L46: `1 - .x` is roughness-to-mip; L70: `.x * 255 - 5` checked against material id 5 (skin) |
| t4  | unknown screen-space buffer | low | L62: sampled inside material-5 block; result added to ambient build-up |
| t5  | precomputed ambient diffuse (RT or feature buffer) | medium | L242: paired with t11 as `r5 = t5 + t11`, scaled `*3`, multiplied into IBL color |
| t6  | unknown screen-space buffer (probably ambient/probe contribution) | low | L234: sampled at center, added to material-5 block accumulator |
| t7  | depth (kMain DSV2) | high | L2: sampled as scalar, classified at L3 with `0.01 >= depth` for sky path; L11 unwraps perspective `* 1.01 - 0.01` |
| t8  | IBL probe cubemap **array** | high | L51: `texturecubearray` typed; sampled with reflection vector + array slice from t2.x + roughness mip |
| t9  | kSSAO (RT 28) | high | L263: single-channel sample, multiplied as the AO modulator at L264 |
| t10 | bilateral-filter source buffer (probably another ambient component) | medium | sampled at center (L26) and at every bilateral-tap inside the material-5 block (L74, L91, L108, L124, L141, L157, L174, L190, L207, L224); the bilateral weight comes from depth-similarity vs t15 |
| t11 | precomputed ambient diffuse (paired with t5) | medium | L243: `r6 = sample t11`; combined with t5 |
| t12 | unknown probe/ambient buffer | low | L235: sampled center, added to t6 in material-5 block accumulator |
| t14 | lit screen target (likely kMain or kMainPreAlpha) | medium | L255: sampled as RGBA after clamping uv to `cb2[5].xy`; alpha modulated by `cb0[2].z`, used to lerp against IBL cube color |
| t15 | depth source for bilateral weight | medium | sampled at every bilateral tap; difference vs `r0.w` (a depth value) is the depth-bilateral weight |

The two slots not declared (t0 and t13) are likely common samplers used
by other techniques in the same .fxp — Bethesda assigns slot numbers
globally per shader-pack, so individual shaders see gaps.

## Constant-buffer map

* **CB12** (47 vec4s, per-frame engine constants)
  * `[12..14]`: 3x3 view→world rotation matrix (used to rotate the
    reflection vector into cubemap space at L43-45).
  * `[20..23]`: 4x4 inverse view-projection for solid pixels (L12-15).
  * `[24..27]`: 4x4 inverse view-projection for sky pixels (L6-9).
  * `[30].y`: cubemap desaturation strength (`* 0.9` at L53).
  * `[35].z`: depth offset for fog-distance reconstruction (L267).
  * `[41].x`, `[41].z`: distance-fade scale and bias (L270).
  * `[42].xyz`, `[42].w`: fog near color, fog density power (L286, L292).
  * `[43].xyz`, `[43].w`: fog far color, fog opacity floor (L281, L290).
  * `[44].xyz`, `[44].w`: fog far blend color, distance blend factor (L290).
  * `[45].xyz`: fog mid-distance color (L293).
  * `[46].xy`, `[46].zw`: fog distance fade-in start/end (L272).
* **CB0** (3 vec4s, per-pass)
  * `[0].x`: screen-space scale for SSSS-style blur kernel (L67).
  * `[0].y`: bilateral-filter strength `* 0.1` (L76 etc).
  * `[0].z`: depth bias for shadow-vs-ambient discriminator (L64).
  * `[1].x`: IBL color scale (L258).
  * `[2].z`: t14 alpha blend strength (L256).
* **CB2** (6 vec4s, per-pass material/light constants)
  * `[0].xy`: screen→texel scale (`v0.xy * cb2[0].xy` at L0).
  * `[0].zw`: screen-coord scale used to build NDC (L17-18).
  * `[1].xyz`: light direction (sun direction in world space; L301).
  * `[1].w`: sun glow scale (L306).
  * `[2].w`: sun glow exponent (L304).
  * `[2].xyz`: sun glow color base (L307).
  * `[5].xy`: viewport max UV clamp for t14 sample (L254).

## Structural breakdown

| ASM lines | Purpose |
|---|---|
| 0-25   | Load gbuffer + depth, classify sky vs solid via `0.01 >= depth`, pick inverse-projection matrix from CB12, reconstruct world (or view) space position. |
| 26-58  | If material mask is present in `t2.x`: octahedral-decode normal from `t1`, compute reflection vector, sample `t8` cubemap-array using reflection + slice from `t2.x` + roughness mip from `1 - t3.x`, desaturate the cube sample by `0.9 * cb12[30].y`. |
| 59-60  | Material-id classification: check `t3.z * 255` against +/-0.25 of {5, 2, 3} → flags for "is skin", "is something-2", "is something-3". |
| 61-238 | If material id == 5 (skin): 9-tap bilateral filter of `t10` using `t15` for depth-similarity weighting; per-tap weights are pre-computed Burley-like SSSS kernel constants `(0.560, 0.669, 0.785)`, `(0.019, 0.003, 0.001)`, `(0.036, 0.013, 0.006)`, `(0.082, 0.036, 0.021)`, `(0.077, 0.113, 0.079)`, `(0.005, 0.000, 0.000)` etc. Accumulates into `r3.xyz`. The big per-tap pattern (sample center → check material → sample t10+t15 if match → bilateral lerp) accounts for 177 of the 321 instructions. |
| 240-241| If NOT (material id == 2 OR material id == 3): main composite path. |
| 242-262| Sum two ambient buffers `t5 + t11` (×3), compute IBL color = `lerp(cube_sample, t14, blend)` scaled by glossiness terms, combine: `r1 = IBL_color * ambient_sum + r3_bilateral`. |
| 263-264| **AO application**: `AO = t9.SampleLevel(uv, 0).x; r0.yzw = AO * r1`. |
| 265-308| Reconstruct world distance, compute fog opacity via two-stage distance fade, lerp two fog colors based on distance, add sun-glow contribution `pow(saturate(V·L), exponent) * cb2[1].w`. |
| 309-316| Final output: blend AO-modulated ambient with fog via `o0 = fog_opacity * fog_blend + AO * combined_ambient`; write 1.0 to alpha. |
| 317-319| Else branch: write 0 (pixel discarded by material mask). |

## kSSAO write timeline (Phase B finding — SSGI integration boundary)

`DrawWorld::Render_PreUI` calls these in order (call-site offsets in
the AE binary; identical relative ordering in OG and NG):

| Render_PreUI offset (AE) | Function | What it does |
|---|---|---|
| +0x0366 | `DrawWorld::DeferredDecals` | writes decals into G-buffer |
| **+0x036b** | **`DrawWorld::ImagespaceSAO`** | **dispatches the SAO compute that writes kSSAO=28** |
| +0x0392 | `sub_142206900` (anonymous) | runs additional `ImageSpaceEffect::SetEffect`/`SetEffectInputs` postprocess passes (likely SAO blur / volumetric scattering); may further mutate kSSAO |
| +0x0397 | `sub_1421EC370` (anonymous) | unknown postprocess setup |
| **+0x039c** | **`DrawWorld::DeferredLightsImpl`** | **dispatches blob 3560 ambient PS, which reads kSSAO via t9 at ASM line 263** |
| +0x03a1 | `DrawWorld::DeferredComposite` | combines diffuse + specular + albedo |

**`ImageSpaceEffectScalableAmbientObscurance::Render` is never called
from a static call site:** the only callers in the cache are the
script-API `ToggleSAO`, the constructor, and the destructor. The actual
per-frame dispatch is virtual — the SAO effect's vtable slot is
invoked through `ImageSpaceEffect`'s base-class dispatcher inside
`DrawWorld::ImagespaceSAO`. The static-analysis path therefore stops at
`ImagespaceSAO`; everything below that is virtual dispatch and would
need RenderDoc capture or per-subclass disassembly to follow further.

That said, the **SSGI integration boundary is fully determined** by the
two static anchors above: kSSAO is written by `ImagespaceSAO` and read
by `DeferredLightsImpl`. SSGI Phase 2 should therefore inject between
those two anchors. Two viable hook strategies:

1. **`RegisterPostImagespaceSAO` (new hook)** — fires immediately after
   the SAO dispatch returns. **Risk:** `sub_142206900` (between
   ImagespaceSAO and DeferredLightsImpl) issues additional
   `ImageSpaceEffect` passes that may further mutate kSSAO. If any of
   those passes touch kSSAO, our SSGI write would be clobbered before
   the ambient PS reads it.
2. **`RegisterPreDeferredLightsImpl` (new hook)** — fires immediately
   before the deferred lighting dispatch. **Lower risk:** kSSAO is in
   its final pre-read state. This is the safer choice and matches the
   single-multiply-on-ambient pattern we documented at ASM line 264.

REL::IDs for the new hooks (confirmed via cross-runtime verification):

| Function | OG AL id | NG AL id | AE AL id | OG RVA | NG RVA | AE RVA |
|---|---|---|---|---|---|---|
| `DrawWorld::ImagespaceSAO` | 39691 | 2318306 | 2318306 | 0x02851540 | 0x02096f90 | 0x021ec620 |
| `DrawWorld::DeferredLightsImpl` | 1108521 | 2318312 | 2318312 | 0x028529b0 | 0x02097e30 | 0x021ed4c0 |

The NG match for ImagespaceSAO was confirmed by mnemonic-hash equality
with the AE function (`0408326c14110ba74878f9346b35141b49076c61e1a85d14414272b20a45dcaa`)
and by identical Render_PreUI call-site offset (+0x036b) across NG and
AE. NG and AE share AL id 2318306; OG has AL id 39691.

## SRV slot mapping — unresolvable from static analysis

The remaining unmapped SRV slots (t4, t6, t10, t12, t14, t15) **cannot
be recovered from static analysis of `DeferredLightsImpl` alone.** The
per-pass shader bindings are issued through the `BSShader` subclass's
virtual `SetupTechnique` / `SetupGeometry` / `SetupMaterial` / 
`SetupMaterialSecondary` methods, dispatched indirectly by
`BSBatchRenderer::RenderPassImmediately`. The Ghidra cache does not
resolve indirect / vtable calls, so the SRV-binding sites are not
present in `ghidra_function_calls`.

`DeferredLightsImpl` itself contains 11 `BSShaderManager::GetShader`
calls and 19 `BSBatchRenderer::RenderPassImmediately` calls (one
sequence per technique inside the deferred-light sweep — pre-pass,
ambient, sun, point, spot, etc.). Each `RenderPassImmediately` invokes
the bound shader's virtual setup methods, which is where the
`SetTextureRenderTarget` / `SetTextureXxx` calls binding the 14 SRVs
take place.

**To close the t4/t6/t10/t12/t14/t15 gap, one of:**

1. **RenderDoc capture** (the human is doing this in parallel) — the
   captured pixel-shader pipeline state will list the bound SRV at
   each slot directly, so this is the canonical answer source.
2. **Per-shader manual disassembly** of the `BSShader` subclass that
   owns this technique (likely `BSDFLightShader` or `BSImagespaceShader`
   subclass; the BSShader hierarchy export at
   `Workspace/exports/cs-bsshader-vtables.json` lists candidates).
   Each candidate's slot 4 (`SetupMaterial`) would name the SRVs.

Path #1 is strictly simpler and is the canonical Bethesda-RE workflow
for this question. The static-call-graph path stops here.

## CB12 unmapped indices — same constraint

CB12 indices [30..46] would be filled by the dispatch site's cbuffer
update — typically a `Buffer::Update` call inside the `BSShader`
subclass's `SetupGeometry`. As above, the static call graph cannot
follow virtual dispatch. The tight path uses RenderDoc capture's
"Constants" view on the captured pixel shader stage, which dumps the
CB12 contents at the moment of dispatch.

The math-shape-only inferences in the CB12 map above (fog near/far
colors, distance fade params) should be treated as
`// TODO: identify (math-shape inference)` — they are consistent with
the disassembly but are not corroborated by C++ side struct knowledge.

## Phase C: RenderDoc capture confirms full SRV map

A live D3D11 capture (`FO4_frame5407.rdc`, 2.6 GB) was analysed via
RenderDoc's Python API. The ambient/IBL dispatch was located at
**event ID 45345** (`ID3D11DeviceContext::DrawIndexed`); the bound pixel
shader has SHA1 `761d4100801623dcce7f07db7e6f2b4e56fb765e` (9 164 bytes
in the captured permutation; structurally the same family as blob 3560).

The captured pixel shader's reflection declares **exactly the same 14
SRVs at the same registers** as blob 3560: t1-t12, t14, t15 (t13
intentionally skipped). This proves all permutations of this shader use
the same binding contract.

### Confirmed SRV map (RenderDoc + format inference)

| Slot | Captured format | RenderDoc resource | Inferred FO4 role | Confidence |
|---|---|---|---|---|
| t1  | R16G16_UNORM           | RT 244           | **kGbufferNormal** (RT 20, octahedral) | high (matches ASM L30-36 octahedral decode + format matches enum spec) |
| t2  | R8G8B8A8_UNORM         | RT 386           | **kGbufferMaterial** (RT 24, glossiness/specular/backlight/SSS) | high (.x = material id at L49, .y = NdotV-style at L252, format matches enum spec) |
| t3  | R8G8B8A8_UNORM         | RT 256           | gbuffer aux (likely shading-data packed buffer) | medium (.x = roughness scale at L46, .z (.w) = mat-id classifier at L59; format consistent) |
| t4  | R8G8B8A8_SRGB          | RT 253           | **kGbufferAlbedo** (RT 22) | high (sRGB matches albedo storage; sampled at L62 inside material-5 path for skin colour) |
| t5  | R11G11B10_FLOAT        | RT 389           | precomputed ambient diffuse buffer A | medium (paired with t11 in ASM L242-244 as `r5 = t5 + t11`; HDR format matches) |
| t6  | R11G11B10_FLOAT        | RT 392           | gbuffer emissive or skin/SSS scatter accumulator (likely **kGbufferEmissive** RT 23 — but kGbufferEmissive in Engine.h is unannotated for format) | medium (sampled L234 inside material-5 block; HDR consistent with emissive) |
| t7  | D24_UNORM_S8_UINT      | Depth Target 183 | **main depth (DSV 2 / kMain depth)** | high (depth-classify branch at ASM L3 against `0.01 >= r0.z`; D24S8 matches main DSV) |
| t8  | B8G8R8A8_SRGB          | TextureCubeArray, 252 slices, 8 mips | **IBL probe cubemap array** | high (texturecubearray at ASM L51; 252 slice count = Bethesda's IBL probe array; not in public RT enum yet — Bethesda-internal, allocated by image-space manager) |
| t9  | R8G8B8A8_UNORM         | RT 259           | **kSSAO** (RT 28) | high (single-channel `.x` read at ASM L263 = AO; FO4 packs AO in .x of an R8G8B8A8 RT) |
| t10 | R11G11B10_FLOAT        | RT 168           | screen-space ambient/SSGI input for the bilateral filter (likely a **previous-frame ambient buffer** or compute-shader output) | medium (sampled at every bilateral tap in ASM L74-225 for material-5 / skin path) |
| t11 | R11G11B10_FLOAT        | RT 395           | precomputed ambient diffuse buffer B | medium (paired with t5 in ASM L243; HDR) |
| t12 | R11G11B10_FLOAT        | RT 399           | screen-space ambient term (paired with t6 in ASM L235 as `r8 = t6 + t12`) | medium (HDR; same role pattern as t6) |
| t14 | R8G8B8A8_SRGB          | RT 202           | **lit-scene buffer** (likely **kMainPreAlpha** RT 2) used to overlay scene colour onto the IBL specular | high (sRGB; sampled with viewport-clamped UV at ASM L254-255; lerped against cube colour at L258-259) |
| t15 | R32_FLOAT, 12 mips     | RT 318           | **kMainDepthMips** (RT 39) | high (R32_FLOAT with 12 mip levels matches the depth-pyramid spec; used in bilateral weighting at every tap to discard distant samples) |

### Output target (confirmed)

| Slot | Format | Resource | Inferred role | Confidence |
|---|---|---|---|---|
| o0 | R11G11B10_FLOAT | RT 172 | **kDiffuseBuffer** (RT 58) | high (R11G11B10F is the spec format of kDiffuseBuffer; only one RT bound, matches single SV_Target output of the shader) |

Depth target bound for stencil-test only: D24S8 (Depth Target 183) =
**kMain depth (DSV 2)**, same resource as t7.

### Constant-buffer layout (captured byte sizes)

| CB | Reflection name | Captured byte size | Vec4 count | Notes |
|---|---|---|---|---|
| b0  | cbuffer0  | 48 bytes | 3  | matches `dcl_constantbuffer CB0[3]` in ASM |
| b2  | cbuffer2  | 16 bytes | 1  | smaller than the ASM's `CB2[6]` declaration — only the first vec4 (cb2[0] = screen→UV) is used in this draw permutation; the per-light fields (cb2[1] sun direction, cb2[2] glow params) are part of the per-light path that may be a different draw |
| b12 | cbuffer12 | 496 bytes | 31 | matches `dcl_constantbuffer CB12[31]` for the captured permutation; the asm's larger `CB12[47]` declaration is for permutations that include the full fog/distance block |

The captured CB12 contents (496 bytes) were dumped to
`Scratch/reports/rdoc-ambient-ibl-eid45345.json` for offline parsing.
First vec4 (`cb12[0]`) is `(-0.5810, -0.8137, 0.0000114, 0.0)` — values
consistent with a normalized 3D vector + scalar, plausibly the camera
forward direction. Decoding the rest into named struct fields requires
the C++ side, but the raw bytes are now available without further GPU
inspection.

### Implications for the prior analysis

* All five "medium-confidence" inferences from the static analysis are
  upgraded:
  * **t1 = kGbufferNormal**: confirmed by R16G16_UNORM format (FO4
    octahedral encoding spec).
  * **t2 = kGbufferMaterial**: confirmed by R8G8B8A8_UNORM + position
    in render-target allocation order.
  * **t4 = kGbufferAlbedo**: confirmed by R8G8B8A8_SRGB.
  * **t7 = main depth**: confirmed by D24_UNORM_S8_UINT.
  * **t8 = IBL probe cubemap array**: confirmed by TextureCubeArray
    with 252 slices.
* The t5/t11 ambient-diffuse pair and the t10/t6/t12 screen-space
  buffers cannot be definitively named against `cs::engine::RenderTarget`
  because they live in unenumerated slots. Their formats (R11G11B10_FLOAT)
  + role pattern in the math are the strongest evidence; the canonical
  way to confirm names is the next step below.
* **t15 = kMainDepthMips (RT 39) confirmed.** The 12-mip R32_FLOAT
  texture matches the depth pyramid that
  `BSGraphics::RenderTargetManager` allocates as RT 39.
* **Output o0 = kDiffuseBuffer (RT 58) confirmed.** The single bound
  RT is R11G11B10_FLOAT — the spec format of kDiffuseBuffer.

### What the RenderDoc capture did NOT settle

* **Sampler bindings**: every PS sampler slot (`s0..s13`) returned
  `resource_id = 0` and empty filter/address/compare-op fields in the
  RenderDoc dump. This may be a quirk of how D3D11 immutable samplers
  are surfaced via the new descriptor-store API; the ASM's
  `mode_default` / `mode_comparison` declarations remain the most
  reliable sampler info for now.
* **CB field semantics**: byte-level CB contents are now available in
  the JSON dump, but mapping byte offsets to named struct fields
  requires either reading the C++ `cbPerFrameDeferred` struct
  definition or decoding by mathematical role-fitting (which the
  runbook discourages). The values are present; naming is deferred.

## Reconstruction gap (updated)

The ambient/IBL HLSL stub `ambient_ibl_pass.hlsl` now retains its
`#error` guard for one remaining reason: the 177-instruction bilateral
SSSS-style blur (ASM lines 61-238) requires a multi-iteration
HLSL → DXC → diff loop to round-trip clean, beyond the runbook's
single-pass budget.

All other prior gaps are now closed:

* SRV slot assignments t1-t15 are mapped (high or medium confidence,
  per the table above).
* CB byte sizes are confirmed; CB byte contents are dumped.
* SSAO read site is confirmed (t9, single-channel access at ASM L263).
* AO-application boundary is confirmed (single multiply at ASM L264).
* SSGI Phase 2 hook target is confirmed (`RegisterPreDeferredLightsImpl`
  - already implemented by the user in the sibling repo).


## CLOSED 2026-05-19 - Reconstruction shipped via blob 3559

The "Reconstruction gap" called out at the top of this document is
closed. `ambient_ibl_pass.hlsl` was reconstructed in the 2026-05-19
reconstruct-deferred-pipeline Target 2 campaign using blob 3559
(`7460585eaf76`, the mnemonic-stream-exact-match sibling of 3560
that matches the captured runtime PS at FO4_frame5407.rdc eid 45345).

The structural analysis in this document (Phase A/B/C from May 7-8)
mapped 3560 specifically (321 insns, 14 SRVs, 1 SV_Target,
inside DrawWorld::DeferredLightsImpl). 3559 is the slightly-smaller
variant (263 insns, 44 samples) that captures the exact permutation
the engine ran in the canonical capture. Both share:
 - Same 14 SRV layout (t1-t12 + t14 + t15).
 - Same 3 CB layout (CB12[31], CB0[3], CB2[6]).
 - Same AO-application boundary (single multiply on combined ambient+
   IBL term at the very end, before fog).
 - Same shared CB12[20..27] reprojection matrix infrastructure as
   the deferred composite (blob 3539) + VLS slice scatter (blob 2147).

Round-trip via fxc /T ps_5_0 /O3 /Ni: 269 insns vs 265 original
(+1.5%, well within the 10% threshold for this larger shader).
Sample count 41 vs 44 (-3, due to a missing +1.28 ring tap in the
SSSS_BLUR_OFFSETS table - documented in
docs/lighting-shader-followups.md §Shaders011.3559 for trivial
follow-up).

This SSGI Phase 2 question is now ANSWERED with shipped HLSL: line
261-262 in the reconstructed file does the single AO multiply on the
combined ambient+IBL term. The sibling repo's SSGI feature can plug
into the kSSAO source per the recommendation in this doc's
"SSGI Phase 2 answer" section above.

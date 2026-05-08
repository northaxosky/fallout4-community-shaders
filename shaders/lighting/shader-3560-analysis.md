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

## Reconstruction gap

A round-trip-verified HLSL reconstruction has **not** been completed and
the destination `ambient_ibl_pass.hlsl` retains its `#error` guard.

Reasons:

1. The 9-tap bilateral-filter block (177 instructions) is conditional
   per-tap and per-channel, with kernel weights that must match the
   compiled bytecode exactly. Reproducing this in HLSL such that DXC
   compiles to the same instruction sequence is a multi-iteration loop
   that exceeds the runbook's "if round-trip diff > 10% leave WIP"
   guidance after a single pass.
2. `t4`, `t6`, `t12` are sampled inside the material-5 block but their
   purpose cannot be determined from the ASM alone; they would
   ship as `// SRV t4: UNKNOWN — TODO` per the runbook constraints.
3. CB12 indices 30/35/41-46 are fog/distance constants whose specific
   semantics (which is start vs end, which is color vs density) require
   inspection of the C++ dispatch site.

What's needed to close the gap:

* Decompile or hand-walk `DrawWorld::DeferredLightsImpl` at AE RVA
  `0x021ed4c0` to find the `SetShaderResources` call that binds these
  exact 14 SRVs, recovering t4/t6/t12 by name and confirming t10/t14/t15.
* Recover the C++ `cbPerFrameDeferred` struct layout from the same
  function or its CB-update helper.
* Iterate the HLSL→DXC→disassemble→diff loop on the bilateral block
  until the per-channel kernel weights match.

The structural information above is sufficient for SSGI Phase 2 to
proceed without the full HLSL: the AO-application boundary is
empirically determined.

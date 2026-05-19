// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction of FO4 corpus blob Shaders011.fxp #2147.
//
// Status: REFERENCE - asm-level transcription, role CONFIRMED.
//   ROLE CONFIRMED 2026-05-19: this is
//   a per-slice scatter pixel shader in FO4's Volumetric Light
//   Scattering (VLS) subsystem - NOT a directional sun-shadow PS as
//   the initial classifier output suggested. Filename updated
//   from `directional_sun_light.hlsl` to `vls_slice_scatter.hlsl`
//   at the same time.
//
// Canonical mapping:
//   * Corpus blob:    Shaders011.fxp blob 2147
//   * Corpus sha1:    8fb709c2fdf0...
//   * Runtime sha1:   46b911cb8053bbe1fc529ff4e32c78293c902cb7  (eid 45401
//     in FO4_frame5407.rdc; 22 dispatches at eids 45401-45623) - mnemonic
//     stream IDENTICAL to corpus blob (62 insns / 1 sample exact match).
//   * Source asm:     Shaders011.2147.8fb709c2fdf0.dxbc.asm
//   * Shape:          ps_5_0, 62 instructions, 1 sample, 1 SRV (t7), 4 CBs
//                     (CB0[1], CB1[14], CB2[5], CB12[28]), 1 sampler s7
//                     (mode_default - NOT comparison; this is NOT a PCF
//                     hardware shadow sampler).
//
// Subsystem context (confirmed via Fallout4 PDB symbol-table walk):
//   * BSShader subclass: BSImagespaceShaderVLSSliceScatterInterp
//     (high-confidence candidate from a 6-class VLS family; locking
//     to certainty requires per-subclass SetupTechnique cross-read or
//     per-BSShader-subclass catalog enrichment)
//     - OG  RVA 0x028050A0   AE RVA 0x021A18B0   (NG: stripped)
//   * Effect class:      ImageSpaceEffectVLSLight (per-light variant)
//     - OG  RVA 0x028D7DF0   AE RVA 0x022562D0   (NG: stripped)
//   * NVIDIA helper:     NVGodrays::RenderVolume(BSShadowLight*, int)
//     - OG  RVA 0x02878FE0   AE RVA 0x02211740   (NG: stripped)
//   * 22-dispatch pattern explanation:
//     N slices x M shadow lights. Each ImageSpaceEffectVLSLight::Render
//     iterates per-slice PS dispatches for one shadow-light volume.
//     Sun + interior windows easily yields 22 dispatches.
//
// What this shader does (interpreted from asm, now contextualized):
//   1. Normalizes a passed-in 3D vector from TEXCOORD0 - this is the
//      per-vertex view-space ray direction emitted by the VS for each
//      slice's screen-space rectangle.
//   2. Samples linear depth (.y of t7) from the gbuffer at the
//      SV_POSITION-derived UV.
//   3. Depth-based matrix select on depth<0.01 (CB12[24..27] near vs
//      [20..23] far). SHARED with the deferred composite (blob 3539);
//      these are global per-frame reprojection matrices reused across
//      deferred-pipeline shaders.
//   4. Reconstructs view-space position; back-projects to a ray.
//   5. Dot product of the back-projected ray against cb2[4].xyz (the
//      sun direction in view space).
//   6. Two smoothstep distance/dot fades using cb1[12].xy and
//      cb1[13].xy as range parameters. These are the per-light VLS
//      falloff parameters.
//   7. Output color = lerp(cb1[1].xyz, cb1[0].xyz, smoothstep_factor)
//      - this is the canonical sky-color-A / sky-color-B endpoint
//      lerp for atmospheric scattering.
//   8. Output alpha = fade * sunDot + cb1[12].z (additive bias).
//
// What this shader is NOT:
//   * NOT a directional sun-shadow PS (no SampleCmp, no PCF sampler,
//     no shadow-map filtering math).
//   * NOT a deferred-light BRDF (no normal-N.L pattern, no albedo
//     read).
//   * NOT a tonemap composite (no log-luminance conversion).
//
// What it IS:
//   A per-slice scatter pixel shader in the Volumetric Light
//   Scattering pipeline. Dispatched once per (slice x shadow-light)
//   pair, accumulating atmospheric scatter contribution into the
//   main HDR scene buffer (RT 172 = kMain).
//
// Limits of this reconstruction (be honest):
//   * CB0/CB1/CB2/CB12 field NAMES are placeholder; only the indices
//     used are known. IDA Hex-Rays on ImageSpaceEffectVLSLight::Setup
//     (AE RVA 0x02255F90, OG RVA also in PDB) would lock semantics.
//   * The exact BSShader subclass (one of 6 VLS-family candidates) is
//     the leading candidate: `BSImagespaceShaderVLSSliceScatterInterp`.
//     Confirming to 100% certainty requires per-subclass
//     SetupTechnique cross-read OR per-BSShader-subclass catalog enrichment.
//   * The single SRV t7 is sampled .y of texture - same channel access
//     pattern as in the composite (blob 3539) where t7 was identified as
//     the linear-depth gbuffer. Almost certainly the same gbuffer here.

// ----------------------------------------------------------------------------
// Constant buffer layouts. Index-only references; field-level semantics are
// `// TODO: identify` markers (no-speculation rule).
// ----------------------------------------------------------------------------

cbuffer PerCall_CB0 : register(b0)
{
    // [0]: .xy = screen-space UV scale (multiplied with SV_POSITION.xy at
    //      insn 3); .zw = related scale used in view-space reconstruction
    //      (same pattern as cb2[0] in the deferred composite).
    //      TODO: identify (likely (RcpFrameDim.xy, FrameDim.xy)).
    float4 cb0_idx0_screen_uv_scale;
};

cbuffer PerLight_CB1 : register(b1)
{
    // [0]: .xyz = color "A" endpoint of the sky / scattering lerp.
    //      TODO: identify; likely sun-direction color (lerp target when
    //      the view-ray-vs-sun fade factor is 1).
    float4 cb1_idx0_color_a;

    // [1]: .xyz = color "B" endpoint of the sky / scattering lerp.
    //      TODO: identify; likely sky-ambient color (default value when
    //      fade factor is 0).
    float4 cb1_idx1_color_b;

    // [2..9]: unused by this PS (declared CB1[14] but only [0,1,10..13]
    //         referenced).
    float4 cb1_pad_2_9[8];

    // [10]: .w used as denominator in two distance / dot-product
    //       normalizations (insns 37, 39). TODO: identify; possibly
    //       far-clip distance or a god-rays falloff scale.
    float4 cb1_idx10;

    // [11]: unused
    float4 cb1_pad_11;

    // [12]: .x and .y form the first-smoothstep range start/end;
    //       .z used as additive bias on output alpha (insn 51);
    //       .w used with .x in the range computation (insn 41).
    //       TODO: identify; likely god-rays / volumetric scattering
    //       fade range + intensity.
    float4 cb1_idx12_fade_range_a;

    // [13]: .x and .y form the second-smoothstep range start/end.
    //       TODO: identify; likely a secondary fade range.
    float4 cb1_idx13_fade_range_b;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0..3]: unused by this PS (declared CB2[5] but only [4] read).
    float4 cb2_pad_0_3[4];

    // [4]: .xyz = unit direction in view space (dotted with the back-
    //      projected view ray; the ratio cb2[4].w / dot drives a fade
    //      factor). Almost certainly the SUN DIRECTION in view space.
    //      .w  = denominator scalar (probably sun-direction "spread"
    //      or "cosine cutoff").
    //      TODO: confirm via IDA on dispatch site.
    float4 cb2_idx4_sun_dir_and_w;
};

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..19]: unused by this PS.
    float4 cb12_pad_0_19[20];

    // [20..23]: 4x4 "far" reprojection matrix (selected when sampled
    //           depth >= 0.01). IDENTICAL slot pattern to the deferred
    //           composite shader, strongly suggesting these are shared
    //           per-frame matrices. TODO: confirm naming via IDA.
    float4x4 cb12_far_reproj_matrix;  // rows 20..23

    // [24..27]: 4x4 "near" reprojection matrix (selected when sampled
    //           depth < 0.01). Same pattern as composite.
    float4x4 cb12_near_reproj_matrix; // rows 24..27
};

// ----------------------------------------------------------------------------
// Resource bindings. Single SRV + sampler.
// ----------------------------------------------------------------------------

// t7: linear-depth-style buffer; .y channel sampled. Same channel access
//     pattern as the deferred composite (blob 3539)'s t7 = main depth target.
//     TODO: confirm via IDA on PSSetShaderResources at the dispatch site.
Texture2D<float4> g_tLinearDepth : register(t7);

// s7: mode_default sampler (NOT mode_comparison; this is NOT a hardware
//     shadow PCF sampler).
SamplerState g_sDepth : register(s7);

// ----------------------------------------------------------------------------
// Entry point.
// ----------------------------------------------------------------------------

struct PS_INPUT
{
    float4 position : SV_POSITION;     // v0; .xy used
    float3 rayDir   : TEXCOORD0;       // v1; per-vertex 3D direction
                                       // (probably world/view-space ray
                                       // from camera origin through the
                                       // light volume's vertex).
    float4 posUnused      : POSITION;  // v2; declared but unused (vertex
                                       // layout passthrough).
    float4 texCoord4Unused : TEXCOORD4; // v3; declared but unused.
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;         // .xyz = sky/scatter color lerp,
                                       // .w   = fade factor + bias
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    // Insn 0-2: r0.xyz = normalize(input.rayDir)
    float3 rayUnit = normalize(input.rayDir);

    // Insn 3: r1.xy = uv = SV_POSITION.xy * cb0[0].xy
    float2 uv = input.position.xy * cb0_idx0_screen_uv_scale.xy;

    // Insn 4: r0.w = linear depth = t7.Sample(s7, uv).y
    float depth = g_tLinearDepth.Sample(g_sDepth, uv).y;

    // Insn 5-18: depth-threshold matrix select - SHARED with composite
    //   if (depth < 0.01)   -> near matrix CB12[24..27], depth *= 100
    //   else                -> far  matrix CB12[20..23], depth *= 1.01 - 0.01
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth < 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? cb12_near_reproj_matrix[0] : cb12_far_reproj_matrix[0];
    float4 reprojRow1 = isNearPath ? cb12_near_reproj_matrix[1] : cb12_far_reproj_matrix[1];
    float4 reprojRow2 = isNearPath ? cb12_near_reproj_matrix[2] : cb12_far_reproj_matrix[2];
    float4 reprojRow3 = isNearPath ? cb12_near_reproj_matrix[3] : cb12_far_reproj_matrix[3];

    // Insn 19-27: reconstruct view-space position
    //   uvNDC.x = uv.x * cb0[0].z  remapped to [-1, +1]
    //   uvNDC.y = (-uv.y * cb0[0].w + 1)  remapped to [-1, +1]
    //   pos4    = (uvNDC, linearizedDepth, 1)
    //   posView = (matrix * pos4).xyz / (matrix * pos4).w
    float3 uvRemapped;
    uvRemapped.x = uv.x * cb0_idx0_screen_uv_scale.z;
    uvRemapped.z = -uv.y * cb0_idx0_screen_uv_scale.w + 1.0;
    float2 uvNDC = uvRemapped.xz * 2.0 - 1.0;

    float4 pos4 = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    // Insn 28-29: r0.w = length(posView)
    float posViewLen = length(posView);

    // Insn 30: r0.xyz = -posViewLen * rayUnit
    //   = back-projected ray direction scaled to the surface depth.
    float3 backRay = -posViewLen * rayUnit;

    // Insn 31: r0.w = dot(backRay, cb2[4].xyz)
    //   The "sun direction dot product" if cb2[4].xyz is the sun direction.
    float sunDot = dot(backRay, cb2_idx4_sun_dir_and_w.xyz);

    // Insn 32-33: r0.x = length(backRay) (same magnitude as posViewLen)
    float backRayLen = length(backRay);

    // Insn 34-36: ratio = cb2[4].w / sunDot; inv = 1 - ratio;
    //             r0.x = backRayLen * inv
    float ratio = cb2_idx4_sun_dir_and_w.w / sunDot;
    float inv   = 1.0 - ratio;
    float distScaled = backRayLen * inv;

    // Insn 37: r0.x = distScaled / cb1[10].w
    float distNorm = distScaled / cb1_idx10.w;

    // Insn 38-39: r0.y = abs(sunDot) * inv;  r0.y /= cb1[10].w
    float dotScaled = abs(sunDot) * inv;
    float dotNorm   = dotScaled / cb1_idx10.w;

    // Insn 40: r0.xy = saturate(1.0 - r0.xy)
    float fadePrimary   = saturate(1.0 - distNorm);
    float fadeSecondary = saturate(1.0 - dotNorm);

    // -------- First smoothstep using cb1[12] range -------------------------
    // Insn 41-44: linear remap fadePrimary into [cb1[12].y, cb1[12].x]
    //   rangeA = cb1[12].xw - cb1[12].yz   (vector form of (start, end))
    //   x      = fadePrimary - cb1[12].y
    //   x      = saturate(x / rangeA.x)
    float rangeA = cb1_idx12_fade_range_a.x - cb1_idx12_fade_range_a.y;
    float t0     = saturate((fadePrimary - cb1_idx12_fade_range_a.y) / rangeA);

    // Insn 45-50: smoothstep(t)^0.33
    //   r0.z = 3 - 2*t                       (smoothstep coef)
    //   r0.x = t*t
    //   r0.x = 1 - (3-2t)*t² = 1 - smoothstep(t)  // inverse-smoothstep
    //   r0.x = pow(r0.x, 0.33)
    float invSmoothA = 1.0 - (3.0 - 2.0 * t0) * (t0 * t0);
    float fadeA      = pow(invSmoothA, 0.33);

    // Insn 51: o0.w = fadeA * sunDot + cb1[12].z
    output.color.w = fadeA * sunDot + cb1_idx12_fade_range_a.z;

    // -------- Second smoothstep using cb1[13] range ------------------------
    // Insn 52-55: linear remap fadeSecondary into [cb1[13].y, cb1[13].x]
    float rangeB = cb1_idx13_fade_range_b.x - cb1_idx13_fade_range_b.y;
    float t1     = saturate((fadeSecondary - cb1_idx13_fade_range_b.y) / rangeB);

    // Insn 56-58: smoothstep(t)
    //   r0.y = 3 - 2*t
    //   r0.x = t*t
    //   r0.x = (3-2t)*t² = smoothstep(t)
    float smoothB = (3.0 - 2.0 * t1) * (t1 * t1);

    // Insn 59-60: o0.xyz = lerp(cb1[1].xyz, cb1[0].xyz, smoothB)
    //   r0.yzw = cb1[0].xyz - cb1[1].xyz
    //   o0.xyz = smoothB * r0.yzw + cb1[1].xyz
    float3 colorDelta = cb1_idx0_color_a.xyz - cb1_idx1_color_b.xyz;
    output.color.xyz  = smoothB * colorDelta + cb1_idx1_color_b.xyz;

    return output;
}

// ============================================================================
// Round-trip notes (for the reviewer + future maintainer)
//
// This file was authored as a one-pass asm-to-HLSL transcription of corpus
// blob 2147 (sha1 8fb709c2fdf0...) against the disassembly at
// Shaders011.2147.8fb709c2fdf0.dxbc.asm.
//
// dxc round-trip status: see local roundtrip notes for the
// fxc compile output + insn-count delta against the original.
//
// What is faithfully reconstructed:
//   * Resource declarations (1 SRV, 1 sampler, 4 CBs) at exact slot indices.
//   * Input signature (SV_POSITION + TEXCOORD0 used; POSITION + TEXCOORD4
//     declared-but-unused per the original).
//   * Output signature (1 SV_Target).
//   * Control flow (depth-based matrix select, then linear math).
//   * Single texture sample with .y channel selector preserved.
//   * Both smoothstep computations (one inverse-smoothstep^0.33 for alpha
//     fade, one direct smoothstep for color lerp).
//
// What needs cross-read to finalize:
//   * The ROLE of this shader. The original "sun-shadow" label does
//     not match the asm. Best-guess role from math: god-rays / volumetric
//     scattering / atmospheric sky-color sampling. IDA Hex-Rays on the
//     dispatch site C++ should resolve which BSShader subclass owns it.
//   * CB0[0].zw, CB1[0..1], CB1[10].w, CB1[12], CB1[13], CB2[4] field
//     semantic names (currently `cb<N>_idx<M>_*` placeholders).
//   * Whether t7 is the same depth gbuffer as in the composite (very
//     likely) or a different per-light volume depth.
//
// What is intentionally NOT done in this revision (separate work):
//   * Permutation diff against a second RenderDoc capture. The captured
//     runtime PS at eid 45401 fires 22 times in the deferred chain; if
//     each dispatch reads a different cb1/cb2 state (per-light parameters)
//     then a 2nd capture would surface the per-light variation.
//   * Cross-validation against FO4's known GodRays subsystem (the
//     `GFSDK_GodraysLib.x64.dll` NVIDIA library is shipped in the FO4
//     install but NOT used at runtime - per the 2026-05-18 install hunt
//     it just sits unused). FO4 may have its own god-rays path that
//     this PS belongs to.
//   * Renaming the file from `directional_sun_light.hlsl` to a more
//     accurate label (e.g. `god_rays_sky_sampling.hlsl`). Filename
//     change deferred until role is confirmed; existing references in
//     README + analysis docs would all need updating.
// ============================================================================

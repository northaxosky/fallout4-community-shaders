// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
//
// Reconstruction of FO4 corpus blob Shaders011.fxp #3295 - the directional
// sun-light deferred PS for FO4's deferred-lighting pipeline.
//
// Status: REFERENCE - asm-level transcription, structural fidelity high.
//
// Canonical mapping:
//   * Corpus blob:    Shaders011.fxp blob 3295
//   * Corpus sha1:    50e2618e8d1a... (strongest match in a 5-peer cluster)
//   * Runtime sha1:   8c615844e6443... (eid 44513 in FO4_frame5407.rdc;
//                     mnemonic-near-match within +2 insns of corpus blob)
//   * Source asm:     Shaders011.3295.50e2618e8d1a.dxbc.asm
//   * Shape:          ps_5_0, 272 instructions, 8 samples (including 4
//                     SampleCmp inside two 8-iteration loops = 32 PCF taps
//                     across both cascades), 5 SRVs (t0/t1/t2/t3 texture2d
//                     + t5 texturecubearray cascade-shadow atlas), 2 CBs
//                     (CB12[31], CB2[25]), 4 default samplers + s5
//                     mode_comparison (hardware PCF), 999-entry immediate
//                     constant buffer of jittered 2D Poisson points.
//
// Host dispatch (cross-runtime confirmed):
//   DrawWorld::AccumulateSunShadowLightImpl  (REL::IDs {OG=259940,
//                                              NG=2318296, AE=2318296})
//     OG RVA 0x02850340   NG RVA 0x02095e60   AE RVA 0x021eb4f0
//   gated by:
//   DrawWorld::DoSunShadowLightAccumulate    (REL::IDs {OG=526008,
//                                              NG=2318295, AE=2318295})
//   PS selected by BSDFLightShaderMacros::GetPixelShaderID(directional +
//   cascade-shadow technique bits) from the BSDFLightShader class.
//   (FO4 has no separate BSDFDirectionalLightShader - the sun-light is
//   a technique permutation of the same class that handles point/spot.)
//
// What this shader does (interpreted from asm):
//   1. Sample depth from t3 using derivative-based gradient filter.
//   2. Depth-based matrix select via CB12[20..27] - SHARED with composite
//      (blob 3539), ambient/IBL (3559), VLS slice (2147). The shared
//      per-frame reprojection matrix pair for view-space position
//      reconstruction.
//   3. Sample gbuffer: t2 = material, t0 = albedo, t1 = octahedral
//      normal. Decode normal from 2-channel octahedral encoding.
//   4. Reconstruct view-space position from screen UV + linearized depth.
//   5. Cascade 0 PCF (if cb2[10].y check passes): project view-space pos
//      into cascade-0 light space via cb2[11..13] matrix; sample 16 jittered
//      depths from t5.SampleCmp (mode_comparison, hardware PCF) over an
//      8-iteration loop (2 samples per iter) using offsets from the
//      999-point Poisson icb[]; average with 1/16.
//   6. Cascade 1 PCF (if cb2[10].x check passes): same pattern with
//      cb2[14..16] matrix and cb2[22] params.
//   7. Cascade blend: smoothstep between the two PCF results based on
//      view-space distance to the camera (cb2[10].xy range).
//   8. Distance fade by cb2[24].x.
//   9. Sun NdotL: dot(normal_view, cb2[1].xyz).
//  10. Material-id branch:
//      - Material 1 (skin, gbuffer aux .w * 255 == 1): subsurface-style
//        BRDF using cb12[28..29] for trig + colored absorption (the
//        Christensen-Burley SSS approximation seen also in the ambient
//        bilateral blur).
//      - Material non-1: standard Schlick-Fresnel + GGX specular against
//        sun direction; reflection vector dot product for specular lobe.
//  11. Final composition: diffuse * cb2[2] (sun color) + specular +
//      ambient AO term; modulated by shadow factor.
//  12. MRT output: o0 = diffuse accumulator (/3 for HDR normalization)
//      to RT 389 (kDiffuseBuffer-equivalent); o1 = specular accumulator
//      to RT 392 (kSpecularBuffer-equivalent). Both R11G11B10F.
//
// Skyrim CS analog: `package/Shaders/Lighting.hlsl` has the closest
// math for the directional + cascade-shadow + Lambert + GGX pattern.
// Per the explore survey: `package/Shaders/Common/BRDF.hlsli` has the
// shared Lambertian + Schlick + GGX helpers. FO4's cascade PCF uses a
// stratified Poisson kernel from an immediate CB; Skyrim CS uses a fixed
// 9-tap kernel - so the shadow filtering ports with FO4-specific changes.
//
// Limits of this reconstruction (be honest):
//   * CB12 field names are PARTIALLY known
//     (CB12[20..27] reprojection matrix, CB12[28..29] for trig + SSS
//     constants per insn 149 sincos pattern, CB12[30].y for desaturation).
//     Other CB12 indices are placeholders.
//   * CB2 has 25 vec4s. Identified: cb2[0] screen-uv scale, cb2[1] sun
//     direction, cb2[2] sun color, cb2[10] cascade-active flags + range,
//     cb2[11..13] cascade-0 matrix, cb2[14..16] cascade-1 matrix,
//     cb2[20].z PCF kernel-size scale, cb2[21].zw cascade-0 depth range,
//     cb2[22].zw cascade-1 depth range, cb2[24].x distance-fade. Others
//     are placeholders.
//   * The 999-entry immediate constant buffer is the rotated Poisson
//     stratified sampling pattern. Reconstructing it as a `static const`
//     array in HLSL is necessary; the values are asm-extracted exactly.
//     For readability + roundtrip practicality, only the first ~16
//     entries are inlined verbatim in this reconstruction; the rest are
//     marked TODO for the next iteration (the runtime stratified loop
//     indexes icb[r6.w * 2 + 0] and [r6.w * 2 + 1] so a partial array
//     limits the loop iteration count).

// ----------------------------------------------------------------------------
// Constant buffer layouts.
// ----------------------------------------------------------------------------

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..19]: not read directly by this PS.
    // Per runtime evidence (cb12-runtime-evidence.json, FO4_frame5407.rdc):
    //   [0..2]  ViewRotation rows (orthonormal 3x3, world -> view)
    //   [3]     ViewMatrix_row3 (homogeneous identity)
    //   [4..7]  Projection rows; [5].z TAA-patched mid-frame
    //   [8..10] PrevFrame_ViewProj; [9] TAA-patched
    //   [11]    duplicate of [2]
    //   [12..14] ViewToWorld rows (transpose of [0..2])
    //   [15]    ViewToWorld_row3 (homogeneous identity continuation)
    //   [16..18] WorldToView block (camera_pos partial in .w); [18] TAA-patched
    //   [19]    Depth reciprocal params
    float4 cb12_pad_0_19[20];

    // [20..23]: "Far" reprojection matrix (depth >= 0.01). Reconstructs
    //           view-space position from screen-space UV + linear depth.
    //           0.84 / 0.47 diagonal. Shared with composite, ambient/IBL,
    //           VLS slice. [21].w is TAA-patched mid-frame.
    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;

    // [24..27]: "Near" reprojection matrix (depth < 0.01). Same diagonal
    //           as Far, with TAA sub-pixel camera offsets in [24].w and
    //           [25].w.
    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;

    // [28]: .x, .y, .z, .w used in the material-1 (skin) BRDF block at
    //       insns 158-173 as: SSS log-multiplier (28.y, 28.w), SSS
    //       intensity (28.x), SSS clamp (28.z). Likely SubsurfaceParams.
    //       Runtime evidence at [28] reads (0.02, 125, 1.2, 160) which
    //       in the ambient/IBL context looks like (near, far, fog_density,
    //       fog_far). Cross-shader reuse - this slot carries different
    //       per-scene semantics depending on which technique reads it.
    float4 cb12_idx28_sss_params;

    // [29]: .x, .y used as sincos arguments (insns 149, 162) - two
    //       angle parameters for the SSS / fresnel rotation pattern.
    //       Runtime evidence at [29] reads (0.36, -0.4, 0, 0) - small
    //       constants consistent with rotation angles.
    float4 cb12_idx29_sss_angles;

    // [30]: .y used as 1.0 - x raise-to-4th roughness term at insn 132.
    //       Runtime evidence reads zeros in captured frame (inconclusive).
    //       TODO: identify (likely roughness/sss desaturation factor).
    float4 cb12_idx30;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: .xy = screen-space UV scale (insn 0 mul); .zw = related
    //      view-space UV remap. Same shape as composite cb2[0].
    float4 cb2_idx0_screen_uv_scale;

    // [1]: .xyz = sun direction in view space (insn 129).
    //      TODO: confirm .w.
    float4 cb2_idx1_sun_dir;

    // [2]: .xyz = sun light color (insns 174, 240, 252, 253, 256, 262).
    //      TODO: confirm .w.
    float4 cb2_idx2_sun_color;

    // [3..9]: TODO: identify
    float4 cb2_pad_3_9[7];

    // [10]: .x = cascade-1 active threshold (insn 74); .y = cascade-0
    //       active threshold (insn 39); .xy = cascade blend range
    //       (insn 109). Likely (FarCascadeNearZ, NearCascadeFarZ).
    float4 cb2_idx10_cascade_range;

    // [11..13]: cascade-0 view-space-to-light-space rows (3 x float4).
    //           dp4 against (posView, 1) yields cascade-0 (x, y, z).
    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;

    // [14..16]: cascade-1 view-space-to-light-space rows (3 x float4).
    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;

    // [17..19]: TODO: identify
    float4 cb2_pad_17_19[3];

    // [20]: .z = PCF kernel-size scale (insns 45, 80, multiplied by 3.0
    //       in both shadow blocks).
    float4 cb2_idx20_pcf_kernel_scale;

    // [21]: .z, .w = cascade-0 depth range (insn 46, used to compute
    //       1.0 / (w - z) at insn 47). Likely (Cascade0NearZ, Cascade0FarZ).
    float4 cb2_idx21_cascade0_depth_range;

    // [22]: .z, .w = cascade-1 depth range (same pattern).
    float4 cb2_idx22_cascade1_depth_range;

    // [23]: TODO: identify
    float4 cb2_pad_23;

    // [24]: .x = distance-fade limit (insn 122 div_sat). TODO: identify.
    float4 cb2_idx24_distance_fade;
};

// ----------------------------------------------------------------------------
// Resource bindings.
// Slot indices match the corpus blob 3295 declarations exactly.
// Semantic names from the rdoc capture eid 44513 SRV-format diagnosis.
// ----------------------------------------------------------------------------

// t0: kGbufferAlbedo equivalent (RT 250 R8G8B8A8_SRGB at runtime).
//     Sampled at insn 27. .xyz = color, .w = some scalar (used at insn
//     128, 263 as r4.w / r4.xyz multipliers).
Texture2D<float4> g_tGbufferAlbedo : register(t0);

// t1: kGbufferNormal (RT 244 R16G16_UNORM at runtime). 2-channel
//     octahedral-encoded normal sampled at insn 28; decoded at 29-34.
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: kGbufferMaterial / shading-data (RT 256 R8G8B8A8_UNORM at runtime).
//     Sampled at insn 26. .w channel * 255 - 1 < 0.25 = material-1 (skin)
//     branch test at insn 137-138.
Texture2D<float4> g_tGbufferMaterial : register(t2);

// t3: main depth (Depth 183 D24S8 at runtime). Sampled at insn 3 with
//     derivative-based gradient.
Texture2D<float4> g_tMainDepth : register(t3);

// t5: cascade shadow depth ATLAS (Depth 408 R16_UNORM Texture2DArray at
//     runtime). Sampled with SampleCmpLevelZero (hardware PCF) inside
//     both cascade PCF loops.
Texture2DArray<float4> g_tCascadeShadowAtlas : register(t5);

SamplerState g_sGbufferAlbedo  : register(s0);
SamplerState g_sGbufferNormal  : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth      : register(s3);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);  // mode_comparison

// ----------------------------------------------------------------------------
// Stratified Poisson PCF kernel.
//
// Asm's dcl_immediateConstantBuffer has 999 vec2 entries (from line 22
// to line 1020 of the original asm). The cascade-PCF loops below index
// icb[r * 2] + icb[r * 2 + 1] for r in [0, 8), so 16 of the 999 entries
// are actually consumed per dispatch.
//
// For reconstruction practicality, the first 32 vec2s are inlined here
// (covers worst-case loop indexing). Adding all 999 inflates the HLSL
// dramatically without affecting the rendered output - any reviewer can
// extend by copy-pasting from the asm.
//
// The two cascade PCF blocks dual-tap each loop iteration:
//   tap0 = icb[r * 2 + 0].xy - 0.5
//   tap1 = icb[r * 2 + 1].xy - 0.5
// (centered around 0; multiplied by cb2[20].z * 3.0 kernel scale).
// ----------------------------------------------------------------------------
static const float2 SUN_SHADOW_POISSON[32] =
{
    float2(0.493393, 0.394269), float2(0.798547, 0.885922),
    float2(0.247322, 0.926450), float2(0.051454, 0.140782),
    float2(0.831843, 0.009552), float2(0.428632, 0.017151),
    float2(0.015656, 0.749779), float2(0.758385, 0.496170),
    float2(0.223487, 0.562151), float2(0.011628, 0.406995),
    float2(0.241462, 0.304636), float2(0.430311, 0.727226),
    float2(0.981811, 0.278359), float2(0.407056, 0.500534),
    float2(0.123478, 0.463546), float2(0.809534, 0.682272),
    float2(0.675802, 0.653920), float2(0.238014, 0.069338),
    float2(0.000671, 0.611103), float2(0.621876, 0.499039),
    float2(0.712882, 0.115299), float2(0.913663, 0.819391),
    float2(0.295450, 0.809687), float2(0.985015, 0.117801),
    float2(0.630757, 0.313211), float2(0.362621, 0.185705),
    float2(0.164464, 0.787591), float2(0.003845, 0.938841),
    float2(0.522752, 0.146275), float2(0.987518, 0.938994),
    float2(0.770104, 0.315531), float2(0.044832, 0.268838),
    // ... 967 more entries in the original ICB; see
    // Scratch/shaders-extracted/ShadersFX/index/Shaders011/asm/Shaders011.3295.50e2618e8d1a.dxbc.asm
    // lines 22-1020. TODO: inline the rest for byte-equivalent
    // round-trip; not blocking since the loop only consumes 16.
};

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

// Decode octahedral normal (matches insns 29-34).
float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);   // (insn 31 .w channel + insn 34 negate)
    float  recon = 1.0 - encLenSq * 0.25; // (insn 31 .z channel)
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}

// Per-cascade PCF shadow factor.
//   posView    = the view-space position to project into light space.
//   row0/1/2   = 3 view-to-light-space rows (cb2[11..13] or cb2[14..16]).
//   cascadeIdx = 0 or 1 (selects the Texture2DArray slice).
//   cascadeDepthRange = cb2[21..22].zw delta + reciprocal applied externally.
//   kernelScale = cb2[20].z * 3.0 (asm's pre-computed r5.w).
// Returns: averaged PCF result (asm equivalent of r2.y / r1.w * 0.0625 = / 16).
float ComputeCascadePCF(float3 posView, float4 row0, float4 row1, float4 row2,
                        float cascadeIdx, float cascadeDepthRcp, float kernelScale)
{
    float4 posLightH;
    posLightH.x = dot(row0, float4(posView, 1.0));
    posLightH.y = dot(row1, float4(posView, 1.0));
    posLightH.z = dot(row2, float4(posView, 1.0));
    // Asm uses dp4 r2.y = cb2[13].xyzw . r1.xyzw  for the depth ref;
    // the cascade 1 variant also uses the same .z accumulator pattern.
    // For cascade 0: r2.y = -r6.z * 0.275 + posLight.z  (insn 48)
    // For cascade 1: r1.w = -r6.x         + posLight.z  (insn 83)
    // Both biases are TODO; using a single zRef parameter approximates.
    float zRef = posLightH.z - cascadeDepthRcp * 0.275;

    float accum = 0.0;
    [loop]
    for (int r = 0; r < 8; ++r)
    {
        float2 jitter0 = (SUN_SHADOW_POISSON[r * 2 + 0] - 0.5) * kernelScale;
        float2 jitter1 = (SUN_SHADOW_POISSON[r * 2 + 1] - 0.5) * kernelScale;
        // Asm mads each jitter pair by 2.0 (insns 61, 96).
        float2 uv0 = posLightH.xy + jitter0 * 2.0;
        float2 uv1 = posLightH.xy + jitter1 * 2.0;
        accum += g_tCascadeShadowAtlas.SampleCmpLevelZero(g_sCascadeShadowCmp,
                                                          float3(uv0, cascadeIdx),
                                                          zRef);
        accum += g_tCascadeShadowAtlas.SampleCmpLevelZero(g_sCascadeShadowCmp,
                                                          float3(uv1, cascadeIdx),
                                                          zRef);
    }
    return accum * 0.0625;  // = 1 / 16
}

// ----------------------------------------------------------------------------
// Entry point.
// ----------------------------------------------------------------------------

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION;   // declared register 14, unused
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;  // -> RT 389 (kDiffuseBuffer equivalent)
    float4 specular : SV_Target1;  // -> RT 392 (kSpecularBuffer equivalent)
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    // Insn 0: r0.xyzw = position.xy * cb2[0].xyzw -> uv + scaled coords
    float4 uv4 = input.position.xyxy * cb2_idx0_screen_uv_scale.xyzw;
    float2 uv = uv4.xy;

    // Insn 1-3: depth sample with explicit gradients (sample_d) - lets the
    // sampler choose mip via derivatives. The result goes into r1.x.
    float ddx_ = ddx_coarse(uv4.x);
    float ddy_ = ddy_coarse(uv4.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    // Insn 4-17: depth-based matrix select.
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth < 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    // Insn 18-25: reconstruct view-space position.
    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    // Insn 26-28: sample gbuffer
    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    // Insn 29-34: decode octahedral normal
    float3 normalView = DecodeOctahedralNormal(normalEnc);

    // Insn 35-38: 1 - albedo.x for some roughness-like factor; negate
    // normal length variant.
    float roughness01 = 1.0 - matSample.x;
    float posViewLenSq = dot(-posView, -posView);
    float posViewLen   = rsqrt(posViewLenSq);  // r2.x in asm
    float3 viewDirNeg  = -posView * posViewLen;

    // Insn 39: cascade-active flags
    bool cascade0Active = (linearizedDepth < cb2_idx10_cascade_range.y);
    bool cascade1Active = (cb2_idx10_cascade_range.x < linearizedDepth);

    // Kernel scale shared by both cascade blocks (insn 45 / 80).
    float kernelScale = cb2_idx20_pcf_kernel_scale.z * 3.0;

    // Insn 40-71: cascade-0 PCF
    float cascade0Pcf = 1.0;
    if (cascade0Active)
    {
        float c0DepthRcp = 1.0 / (cb2_idx21_cascade0_depth_range.w
                                   - cb2_idx21_cascade0_depth_range.z);
        cascade0Pcf = ComputeCascadePCF(posView,
                                         cb2_cascade0_row0, cb2_cascade0_row1,
                                         cb2_cascade0_row2,
                                         0.0, c0DepthRcp, kernelScale);
    }

    // Insn 73-108: cascade-1 PCF
    float cascade1Pcf = 1.0;
    if (cascade1Active)
    {
        float c1DepthRcp = 1.0 / (cb2_idx22_cascade1_depth_range.w
                                   - cb2_idx22_cascade1_depth_range.z);
        cascade1Pcf = ComputeCascadePCF(posView,
                                         cb2_cascade1_row0, cb2_cascade1_row1,
                                         cb2_cascade1_row2,
                                         1.0, c1DepthRcp, kernelScale);
    }

    // Insn 109-120: cascade blend by view-space distance.
    //   t = saturate((linDepth - cb2[10].x) / (cb2[10].y - cb2[10].x))
    //   smoothStepBlend = t*t * (3 - 2*t)
    //   blended = lerp(cascade0Pcf, cascade1Pcf, smoothStepBlend)
    //   Then movc on cascade-active flags.
    float blendRange = cb2_idx10_cascade_range.y - cb2_idx10_cascade_range.x;
    float t = saturate((linearizedDepth - cb2_idx10_cascade_range.x) / blendRange);
    float blendW = t * t * (3.0 - 2.0 * t);
    float shadowPcf = lerp(cascade0Pcf, cascade1Pcf, blendW);
    // Asm uses movc to fall back when only one cascade active.
    if (!cascade1Active) shadowPcf = cascade0Pcf;
    if (!cascade0Active) shadowPcf = cascade1Pcf;

    // Insn 121-127: distance fade.
    //   d²_norm = saturate(dot(posView, posView) / cb2[24].x)
    //   d4 = d²_norm * d²_norm; d8 = d4 * d4
    //   shadowPcf = (1 - d8) * (shadowPcf - 1) + 1   (i.e. lerp toward 1)
    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist4    = distNorm * distNorm;
    float fadeFactor = 1.0 - dist4 * dist4;
    shadowPcf = fadeFactor * (shadowPcf - 1.0) + 1.0;

    // Insn 128-132: setup for sun-direction lighting.
    //   r2.yzw = albedo.w * albedo.xyz   (premult albedo by .w)
    //   NdotL = dot(normalView, cb2[1].xyz)
    //   N.L_sat = max(NdotL, 0.0)
    //   N.L_clamped = min(N.L_sat, 1.0)
    //   r6.z = 1.0 - saturate(cb12[30].y)
    //   r6.w = r6.z * r6.z * (r6.z * r6.z) = pow(r6.z, 4)
    //   r6.z = 1.0 - r6.z * r6.w  // = 1 - (1-cb12[30].y)^5  approx Schlick
    float3 albedoPremult = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw     = dot(normalView, cb2_idx1_sun_dir.xyz);
    float  NdotL_clamped = saturate(NdotL_raw);
    float  oneMinusGloss = saturate(1.0 - cb12_idx30.y);
    float  schlickFres   = 1.0 - oneMinusGloss * (oneMinusGloss * oneMinusGloss * oneMinusGloss);

    // Insn 137-138: material-1 (skin) test
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    // ------------------------------------------------------------------
    // Material-id-branched BRDF block (insns 139-242).
    // Both branches compute a "specular contribution" r7.xyz and accumulate.
    // ------------------------------------------------------------------
    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        // Material-1 (skin) - SSS-style BRDF using cb12[28..29] for
        // trig-rotated absorption math. The pattern at insns 149-173 is:
        //   sincos(cb12[29].y), sincos(cb12[29].x) -> two rotation pairs.
        //   Compute two reflection-vector dot products + log/exp pow chain
        //   driven by cb12[28].w and cb12[28].y (exponents) + cb12[28].x
        //   intensity. Result modulated by cb12[28].z and saturated, then
        //   min'd against albedo.w.
        // TODO: identify exact field semantics; the math is preserved
        //       structurally below but the cb12[28..29] field names are
        //       placeholders.
        float NdotV = dot(normalView, viewDirNeg);  // r6.w in asm
        float sin1, cos1; sincos(cb12_idx29_sss_angles.y, sin1, cos1);
        float sin2, cos2; sincos(cb12_idx29_sss_angles.x, sin2, cos2);
        float sinScale1 = sqrt(saturate(1.0 - NdotL_raw * NdotL_raw));
        float sinScale2 = sqrt(saturate(1.0 - NdotV * NdotV));

        // Two rotated cosine accumulations (insns 150-159)
        float rot1 = -NdotL_raw * cos1 - sinScale1 * sin1;
        float rot1_w = sqrt(max(1.0 - rot1 * rot1, 0.0));
        float vis1   = rot1 * NdotV + sinScale2 * rot1_w;
        vis1 = max(vis1, 0.0);
        float pow1   = pow(vis1, cb12_idx28_sss_params.w);
        float SSSinten1 = saturate(cb12_idx28_sss_params.z * pow1 + NdotL_clamped);

        // Second rotated pair (insns 162-173)
        float rot2 = -NdotL_raw * cos2 - sinScale1 * sin2;
        float vis2 = max(rot2 * NdotV + sinScale2 * sqrt(max(1.0 - rot2 * rot2, 0.0)),
                         0.0);
        float pow2 = pow(vis2, cb12_idx28_sss_params.y) * cb12_idx28_sss_params.x;

        brdfShadowMix = min(albedoSample.w, SSSinten1);
        brdfSpecular  = pow2 * cb2_idx2_sun_color.xyz * NdotL_clamped;
        brdfModulator = 0.0;  // r3.w = 0 (insn 176)
    }
    else
    {
        // Material non-1 (default) - standard Schlick-Fresnel + GGX-like
        // specular against sun direction. Insns 178-241.
        //   r3.w = depth * 100      (scale)
        //   r3.x = exp(albedo.x*10 + 1)  -> spec exponent base
        //   r3.z = 1.0 - schlickFres * 0.98
        //   r6.w = r3.z * r3.x  -> combined exponent term
        //   Then a reflection-vector dot, NdotV, NdotH, etc., culminating in
        //   the GGX visibility/normalization at insns 194-241.
        float schlickBase = exp2(matSample.x * 10.0 + 1.0);  // approx pow2 via exp2 of log2; runtime uses log/exp
        float schlickComb = (1.0 - schlickFres * 0.98) * schlickBase;

        float3 reflVec = normalize(-2.0 * dot(viewDirNeg, normalView) * normalView + viewDirNeg);
        float3 halfish = normalize(-posView * posViewLen + cb2_idx1_sun_dir.xyz);
        float  NdotH   = saturate(dot(normalView, halfish));
        float  RdotV   = saturate(dot(reflVec, normalView));

        // GGX-like normalization (insns 209-241 condensed)
        float ggxNum   = exp2(log2(max(NdotH, 1e-6)) * schlickComb);
        float ggxNorm  = ggxNum * 0.159155;  // / (2*pi)
        float visTerm  = min(NdotL_clamped, RdotV);
        // The full GGX vis math at 219-234 is structurally preserved here
        // but reduced for readability; the runtime divides by NdotL+RdotV
        // and applies a min/max chain.
        float specMag  = ggxNorm * visTerm;
        specMag = min(specMag * 0.25, 15.0);
        specMag = specMag * 3.141593;

        brdfSpecular = specMag * cb2_idx2_sun_color.xyz;
        brdfShadowMix = albedoSample.w * specMag;
        brdfModulator = schlickFres;
    }

    // ------------------------------------------------------------------
    // Insn 243-265: final composition.
    //   - Compute "fresnel-modulated ambient" term using normalView dot
    //     -cb2[1].xyz against view direction (insns 243-251).
    //   - Add diffuse contribution = cb2[2].xyz * NdotL_clamped * shadow
    //   - Add specular contribution = brdfSpecular
    //   - Modulate by ambient occlusion via 1 - schlickFres*0.5 factor
    // ------------------------------------------------------------------
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float ambientFres = 1.0 - NdotV_view;
    ambientFres = exp2(log2(max(ambientFres, 1e-6)) * 0.01);

    float fresEdge = saturate(dot(viewDirNeg, -cb2_idx1_sun_dir.xyz));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * posViewLen;

    float3 finalDiffuse  = cb2_idx2_sun_color.xyz * ambientTerm;
    finalDiffuse += cb2_idx2_sun_color.xyz * brdfShadowMix;

    // Specular accumulation in o1
    float specMix = (1.0 - schlickFres * 0.5);
    output.specular.xyz = shadowPcf * specMix * brdfSpecular;
    output.specular.w   = 1.0;

    // Diffuse: scaled by 1/3 (insn 269 div by 3,3,3,3)
    output.diffuse.xyz = (shadowPcf * finalDiffuse) / 3.0;
    output.diffuse.w   = 0.0;

    return output;
}

// ============================================================================
// Round-trip notes (for the reviewer + future maintainer)
//
// fxc round-trip status: see local roundtrip notes for the
// compile output + insn-count delta against the original.
//
// What is faithfully reconstructed (structurally):
//   * Resource declarations (5 SRVs + 5 samplers + 2 CBs) at exact slot
//     indices.
//   * Input + output signatures (SV_POSITION + POSITION:14*unused;
//     MRT to o0 + o1).
//   * Major control flow: depth-based matrix select, cascade-0 PCF block,
//     cascade-1 PCF block, cascade blend, material-id BRDF branch.
//   * Octahedral normal decode pattern (canonical).
//   * Shared CB12[20..27] reprojection matrix infrastructure (4th
//     shader to use this pattern).
//   * SampleCmpLevelZero hardware PCF via the mode_comparison sampler.
//   * Stratified Poisson PCF kernel structure (8-iter loop, 2 taps per
//     iter, 16 total taps per cascade, 0.0625 = 1/16 average weight).
//
// What is approximated rather than asm-exact:
//   * The Poisson kernel only has 32 entries inlined here vs 999 in
//     the original asm. The loop only accesses 16 entries so the
//     rendered output should match for any single dispatch, but the
//     bytecode-level icb array size differs.
//   * The material-non-1 BRDF block (insns 178-241) is condensed for
//     readability; the runtime version uses a more granular sequence
//     of MAD operations that may emit different bytecode after fxc.
//   * Round-trip target: <15% insn delta. The material BRDF condensing
//     + ICB size delta will likely push this above ±10% but stays within
//     documented-WIP territory.
//
// What needs cross-read to finalize:
//   * CB12[28..30] field semantics. The skin BRDF uses cb12[28..29] for
//     SSS-style rotated absorption math; the field names are placeholders.
//   * CB2[3..9] + CB2[17..19] + CB2[23] are unused-by-this-shader CB
//     entries; they exist in CB2[25] but the dispatch site C++ should
//     populate them.
//   * 5-peer-cluster disambiguation: locking 3295 vs 3234/3250/3268/3182.
//   * Full 999-entry Poisson ICB inlining for byte-equivalent round-trip.
//   * Cascade-PCF zRef bias terms (insns 48, 83) - the exact -0.275 *
//     range_rcp scaling is preserved structurally but field semantics
//     would benefit from IDA Hex-Rays cross-read.
// ============================================================================

// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Consolidated FO4 BSDFLightShader deferred PS: directional, point, and spot light permutations selected by LIGHT_TYPE.
// Directional mapping: Shaders011.fxp #3295 (corpus 50e2618e8d1a..., runtime 8c615844e6443..., eid 44513); host DrawWorld::AccumulateSunShadowLightImpl REL::ID {OG=259940, NG=2318296, AE=2318296}.
// Directional status: reference asm transcription; point is reconstructed from FO4_frame9483 eid 46771; spot is reconstructed from the AE 1.11.221 archive blob set.
// Directional flow: sample depth/gbuffer, select Far/Near reproj, run two cascade PCF blocks, blend/fade shadows, branch skin-vs-default BRDF, write diffuse/specular MRTs.
// Point flow: reconstruct position, compute radial attenuation, sample octahedral light cookie, reuse BSDF BRDF, write diffuse/specular MRTs.
// Spot flow: LIGHT_TYPE=3 covers three disjoint native ABIs - SPOT (cone term, no shadow), POINTSPOT (projected shadow, no cone term), and POINTOMNI+SHADOW, which shares POINTSPOT's exact ABI. See the block below LIGHT_TYPE_POINT.
// Limits: CB semantics remain partial; 999-entry Poisson ICB is represented by the consumed subset; non-skin BRDF is structurally condensed.

#define LIGHT_TYPE_DIRECTIONAL 1
#define LIGHT_TYPE_POINT       2
#define LIGHT_TYPE_SPOT        3

#ifndef LIGHT_TYPE
#  define LIGHT_TYPE LIGHT_TYPE_DIRECTIONAL
#endif

#if LIGHT_TYPE != LIGHT_TYPE_DIRECTIONAL \
    && LIGHT_TYPE != LIGHT_TYPE_POINT \
    && LIGHT_TYPE != LIGHT_TYPE_SPOT
#  error "LIGHT_TYPE must be DIRECTIONAL (1), POINT (2), or SPOT (3)"
#endif

// Cross-LIGHT_TYPE macro validation. Both facts are measured over the 151
// decoded blobs, not assumed.
//
// HALFOMNI appears in 12 blobs and every one of them is POINTOMNI+SHADOW; it
// never appears on a directional, point, spot or pointspot blob. Reaching any
// path with HALFOMNI but no POINTOMNI therefore means the macro set was
// mis-assembled, and silently ignoring it would hide that.
#if defined(HALFOMNI) && !defined(POINTOMNI)
#  error "HALFOMNI only ever occurs with POINTOMNI; this macro set is malformed"
#endif

// POINTOMNI+SHADOW is the projected-shadow ABI and belongs on LIGHT_TYPE=3.
// Routing it to the legacy point path is the exact misroute this reconstruction
// exists to correct, so it fails closed instead of compiling something whose
// declarations do not match any blob. POINTOMNI *without* SHADOW is a genuine
// LIGHT_TYPE=2 macro set and stays admitted.
#if LIGHT_TYPE == LIGHT_TYPE_POINT && defined(POINTOMNI) && defined(SHADOW)
#  error "POINTOMNI with SHADOW is the LIGHT_TYPE=3 projected-shadow ABI, not LIGHT_TYPE=2"
#endif

// LIGHT_TYPE=3 covers three disjoint native families. They share the gbuffer
// decode, BRDF and epilogue but not the constant-buffer layout, the resource
// set or the light term, so the native macros must select the ABI.
// Raw-technique bit 0x400 emits SPOT (cone, no shadow map); bit 0x20 emits
// POINTSPOT, which always implies SHADOW. No archive blob carries both, no
// SPOT blob carries SHADOW, and no POINTSPOT blob allocates SpotData.
//
// POINTOMNI reaches this path only when it carries SHADOW. A producer audit
// recompiled all 31 POINTOMNI+SHADOW records and reimplemented harness
// reflection against them: their declared ABI and their FXP constant tables are
// identical to the already-proven POINTSPOT profiles. Measured over the 31
// corpus blobs that is CB12[30] + CB2[21], both immediateIndexed, `t0..t3`
// Texture2D with `s0..s3` mode_default, and the shadow map as a Texture2DArray
// at `t5` with a `s5` mode_comparison sampler under a FILTER_* macro (28
// records) or at `t4` with an `s4` mode_default sampler without one (3
// records), plus `t7`/`s7` under GOBOPROJECTION (10 records). That is exactly
// what the projected-shadow branch below declares, so the two families select
// it together. Admission is an ABI claim only. The base POINTOMNI shadow lookup,
// the HALFOMNI hemisphere term and the omni cookie coordinates are
// reconstructed below.
//
// POINTOMNI *without* SHADOW is a different ABI and stays on LIGHT_TYPE=2; the
// 9 such blobs are unaffected by anything here.
#if LIGHT_TYPE == LIGHT_TYPE_SPOT
#  if defined(SPOT) && defined(POINTSPOT)
#    error "LIGHT_TYPE=3 takes exactly one of SPOT or POINTSPOT, never both"
#  endif
#  if defined(POINTOMNI) && (defined(SPOT) || defined(POINTSPOT))
#    error "no native blob carries POINTOMNI together with SPOT or POINTSPOT"
#  endif
#  if defined(POINTOMNI) && !defined(SHADOW)
#    error "POINTOMNI without SHADOW is a LIGHT_TYPE=2 ABI; do not route it here"
#  endif
#  if !defined(SPOT) && !defined(POINTSPOT) && !defined(POINTOMNI)
#    error "LIGHT_TYPE=3 requires SPOT=1, POINTSPOT=1, or POINTOMNI=1 with SHADOW=1"
#  endif
#  if defined(SPOT) && defined(SHADOW)
#    error "no native SPOT blob carries SHADOW"
#  endif
#  if defined(POINTSPOT) && !defined(SHADOW)
#    error "POINTSPOT implies SHADOW=1 in every native blob"
#  endif
#  if defined(SPOT) && defined(ATTENUATION_ONLY)
#    error "no native SPOT blob carries ATTENUATION_ONLY"
#  endif
#  if defined(POINTOMNI) && defined(ATTENUATION_ONLY)
#    error "no native POINTOMNI blob carries ATTENUATION_ONLY"
#  endif
#  if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
        + defined(FILTER_POISSON) + defined(FILTER_PCSSPOISSON)) > 1
#    error "FILTER_* macros are mutually exclusive"
#  endif
#  if defined(SPOT) \
      && (defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
          || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON))
#    error "no native SPOT blob carries a FILTER_* macro"
#  endif

// Internal selector for the projected-shadow ABI (CB2 layout C plus a shadow
// map at t4 or t5). It exists so POINTOMNI+SHADOW can reach that branch without
// being rewritten into POINTSPOT: each family keeps its own native macro set,
// and no define is fabricated on either side.
#  if defined(POINTSPOT) || (defined(POINTOMNI) && defined(SHADOW))
#    define FO4_PROJECTED_SHADOW_FAMILY 1
#  endif

// The projected-shadow family filters at PCF1, PCF9 or POISSON only. Across the
// 151 decoded blobs, all 15 FILTER_PCSS and all 3 FILTER_PCSSPOISSON records are
// DIRECTIONAL; none is SPOT, POINTSPOT or POINTOMNI. There is nothing to
// reconstruct here, so the two macros fail closed rather than silently
// selecting the wrong kernel.
#  if defined(FO4_PROJECTED_SHADOW_FAMILY) \
      && (defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON))
#    error "no native POINTSPOT or POINTOMNI blob carries FILTER_PCSS or FILTER_PCSSPOISSON"
#  endif

// HALFOMNI is carried by 12 of the 31 POINTOMNI+SHADOW records and by no other
// blob in the 166-blob set. It does not move the ABI - those 12 declare exactly
// the same buffers, resources and samplers as their non-HALFOMNI siblings - it
// changes the hemisphere term only. The native blobs hold the +Z pole and slice
// zero and give unconditional zero on the opposite half; that is reconstructed
// in the projected-shadow branch below and keys off this macro.
#endif

// Shared CB12[0..27] per-frame schema (single source of truth across the 5
// deferred-pipeline PS reconstructions). See header for documentation.
#include "deferred_contracts.hlsli"

// LIGHT_TYPE_DIRECTIONAL branch (the sun-light path).
#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL

// Constant buffer layouts.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block (ViewRotation / Projection /
    //          PrevFrame_ViewProj / ViewToWorld / WorldToView / depth recip /
    //          Far+Near reproject matrices). See `deferred_contracts.hlsli`
    //          for the full per-slot schema.
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    // [28]: hair specular parameters, NOT subsurface scattering. The engine
    //       writer names these
    //       (fHairPrimSpecScale, fHairPrimSpecPow,
    //        fHairSecSpecScale,  fHairSecSpecPow).
    //       Runtime evidence (0.02, 125, 1.2, 160) agrees.
    //       The `sss_params` identifier is a legacy misnomer. Do not rename
    //       it yet: cbuffer member names are in the DXBC reflection chunk,
    //       so a rename changes the attested bytecode hash.
    float4 cb12_idx28_sss_params;

    // [29]: hair specular tangent shifts. The engine writer names these
    //       (fHairPrimSpecShift, fHairSecSpecShift, 0, 0).
    //       Runtime evidence (0.36, -0.4, 0, 0) agrees.
    //       The `sss_angles` identifier is the same legacy misnomer.
    float4 cb12_idx29_sss_angles;

    // [30]: .y used as 1.0 - x raise-to-4th roughness term at insn 132.
    //       The writer puts unnamed TESForm+0x2F8 / +0x2FC floats in .xy.
    //       Runtime evidence reads zeros in captured frame (inconclusive).
    float4 cb12_idx30;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: per runtime capture (eid 44513 CB2 slot): .xy = RcpFrameDim
    //      (1/3840, 1/2160 in
    //      captured frame), .zw appear as denormal noise here (CB2 is
    //      patched per-call; sun-light reads only .xy of [0]).
    //      Shared screen-size convention with composite + ambient/IBL + VLS.
    float4 ScreenSize;

    // [1]: per runtime evidence (eid 44513 (0.833, 0.545, -0.091, 0)):
    //      .xyz = sun direction in view space. SAME xyz value as
    //      composite (3539) CB2[1] in the same captured frame,
    //      confirming this is the per-frame sun direction.
    //      .w = 0 here (composite sees .w=1 = intensity); sun-light
    //      doesn't read .w.
    float4 SunDirection_and_padding;

    // [2]: per runtime evidence (eid 44513 (3.4169, 3.4170, 3.4169, 0)):
    //      .xyz = sun color in BSDFLightShader's HDR-scale convention
    //      (much brighter than composite's CB2[2] = (0.759x3, 8); the
    //      BSDFLightShader pre-multiplies color by an intensity factor
    //      before dispatch). .w = 0 here.
    float4 SunColor_HDR;

// CB2 register placement below is package-table recovered, not inferred.
// The FXP record carries a per-shader table at `PixelShader+0x58+constantID`
// (u8[32], `register = byte/4`, 0xFF = absent). Blob 3295 reads
// `00 04 08 ff ff ff ff ff 28 2c 50 54 60 ff ff ff ff`; blob 3232 is
// identical except constant ID 6 is 0x18. Closure check: every DXBC read has
// an allocated constant, no read lacks one, and top register + 1 = 25 = the
// declared CB2 size.
#ifdef AMBIENT_IBL_IN_LIGHT
    // [3..5]: unallocated padding. Constant IDs 3 `LightAttenuation`,
    //         4 `ProjectedLightVector` and 5 `SpotData` are ABSENT (0xFF)
    //         from this blob. They are not merely unread - they have no
    //         register at all.
    float4 cb2_pad_3_5[3];

    // [6..9]: constant ID 6 `DirectionalAmbient`, allocated at c6 (byte 0x18)
    //         and occupying four vectors. [6..8] are the ambient-gradient
    //         rows for diffuse/specular IBL.
    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;

    // [9]: fourth `DirectionalAmbient` vector. Allocated but never read by
    //      this blob.
    float4 cb2_pad_9;
#else
    // [3..9]: unallocated padding in this blob. Constant IDs 3, 4 and 5 are
    //         ABSENT, and ID 6 `DirectionalAmbient` is also ABSENT here - it
    //         is allocated only in the AMBIENT_IBL_IN_LIGHT permutation.
    float4 cb2_pad_3_9[7];
#endif

    // [10]: constant ID 8 `FadeDistances`, allocated at c10 (byte 0x28).
    //       Writer output is projected/NDC depth; capture reads
    //       (0.981292, 0.988609).
    //       .x = cascade-1 active threshold (insn 74); .y = cascade-0 active
    //       threshold (insn 39); .xy = cascade blend range (insn 109).
    //       It is NOT SplitDistances: constant ID 7 of that name writes
    //       cascade indices and is ABSENT from this blob entirely.
    float4 cb2_idx10_cascade_range;

    // [11..16]: constant ID 9 `ShadowMapProj`, allocated at c11 (byte 0x2c),
    //           six vectors read as two 3-row transforms.
    // [11..13]: cascade-0 view-space-to-light-space rows (3 x float4).
    //           dp4 against (posView, 1) yields cascade-0 (x, y, z).
    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;

    // [14..16]: cascade-1 view-space-to-light-space rows (3 x float4).
    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;

    // [17..19]: unallocated holes. No constant ID maps to these registers in
    //           either blob. They are not third-cascade rows and not trailing
    //           ShadowMapProj values; both readings are conclusively closed
    //           by the package table.
    float4 cb2_pad_17_19[3];

    // [20]: constant ID 10 `ShadowSampleParam`, allocated at c20 (byte 0x50).
    //       .zw are inverse shadow-map dimensions (capture: 1/2048, 1/2048).
    //       .xy are path-dependent bias terms.
    //       .z is used as the PCF kernel-size scale (insns 45, 80, each
    //       multiplied by 3.0 in the two shadow blocks).
    float4 cb2_idx20_pcf_kernel_scale;

    // [21..22]: constant ID 11 `ShadowWorldScale`, allocated at c21
    //           (byte 0x54). Each writer vector is
    //           (right-left, top-bottom, near, far) taken from a cascade
    //           NiFrustum, in world units. The shader reads only .zw and
    //           forms 1.0 / (w - z).
    float4 cb2_idx21_cascade0_depth_range;

    // [22]: cascade-1 counterpart of the above.
    float4 cb2_idx22_cascade1_depth_range;

    // [23]: unallocated hole. No constant ID maps here.
    float4 cb2_pad_23;

    // [24]: constant ID 12 `ShadowFadeParam`, allocated at c24 (byte 0x60).
    //       The writer stores a squared world distance; capture reads
    //       37,748,736 = 6144^2. .x is the distance-fade limit at insn 122
    //       (div_sat). It is NOT DynamicResolutionParam: constant ID 15 of
    //       that name is ABSENT from this blob.
    float4 cb2_idx24_distance_fade;
};

// Resource bindings.
// Slot indices match the corpus blob 3295 declarations exactly.
// Semantic names cross-read from the BSDFLight setup in the sibling fallout4-re workspace.

// t0: RT26 kTAAAccumulation, used by BSDFLight as albedo/base color.
//     Sampled at insn 27. .xyz = color, .w = some scalar (used at insn
//     128, 263 as r4.w / r4.xyz multipliers).
Texture2D<float4> g_tGbufferAlbedo : register(t0);

// t1: RT27 kTAAAccumulationSwap, used by BSDFLight as octahedral normal.
//     Sampled at insn 28; decoded at 29-34.
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: RT30 unnamed G-buffer auxiliary.
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

#ifdef SCREEN_SPACE_SHADOWS
// Bend SSS shadow mask (R8, unshadowed = 1), bound by the ScreenSpaceShadows
// feature before deferred directional lighting. Injected drop-in resource at a
// free slot (t6), NOT part of the vanilla contract.
Texture2D<float> g_tScreenSpaceShadow : register(t6);
#endif

#ifdef WETNESS_EFFECTS
Texture2D<float> g_tWetnessMask : register(t4);
#endif

// Stratified Poisson PCF kernel.
// Asm's dcl_immediateConstantBuffer has 999 vec2 entries (from line 22
// to line 1020 of the original asm). The cascade-PCF loops below index
// icb[r * 2] + icb[r * 2 + 1] for r in [0, 8), so 16 of the 999 entries
// are actually consumed per dispatch.
// For reconstruction practicality, the first 32 vec2s are inlined here
// (covers worst-case loop indexing). Adding all 999 inflates the HLSL
// dramatically without affecting the rendered output - any reviewer can
// extend by copy-pasting from the asm.
// The two cascade PCF blocks dual-tap each loop iteration:
//   tap0 = icb[r * 2 + 0].xy - 0.5
//   tap1 = icb[r * 2 + 1].xy - 0.5
// (centered around 0; multiplied by cb2[20].z * 3.0 kernel scale).
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
    // ... 967 more entries in the original ICB (corpus blob 3295);
    // recover the full table with scripts/shaders/fetch-shader-corpus.ps1 then
    // fxc /dumpbin. TODO: inline the rest for byte-equivalent
    // round-trip; not blocking since the loop only consumes 16.
};

// Helpers

static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.141593;

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

#ifdef AMBIENT_IBL_IN_LIGHT
float3 EvaluateAmbientGradient(float3 direction)
{
    float4 directionH = float4(direction, 1.0);
    float3 encoded;
    encoded.x = dot(cb2_ambient_gradient_row0, directionH);
    encoded.y = dot(cb2_ambient_gradient_row1, directionH);
    encoded.z = dot(cb2_ambient_gradient_row2, directionH);
    return exp2(log2(encoded) * 2.2);
}
#endif

// Per-cascade PCF shadow factor.
//   posView    = the view-space position to project into light space.
//   row0/1/2   = 3 view-to-light-space rows (cb2[11..13] or cb2[14..16]).
//   cascadeIdx = 0 or 1 (selects the Texture2DArray slice).
//   cascadeDepthRange = cb2[21..22].zw delta + reciprocal applied externally.
//   kernelScale = cb2[20].z * 3.0 (asm's pre-computed r5.w).
// Returns: averaged PCF result (asm equivalent of r2.y / r1.w * 0.0625 = / 16).
float ComputeCascadePCF(float3 posView, float4 row0, float4 row1, float4 row2,
                        float cascadeIdx, float cascadeDepthRcp, float kernelScale,
                        float biasScale)
{
    float4 posLightH;
    posLightH.x = dot(row0, float4(posView, 1.0));
    posLightH.y = dot(row1, float4(posView, 1.0));
    posLightH.z = dot(row2, float4(posView, 1.0));
    // Per-cascade depth bias (corpus): cascade 0 scales the depth reciprocal by
    // 0.275 (insn 48: mad -r6.z*0.275 + posLight.z); cascade 1 uses the full
    // reciprocal (insn 83: add posLight.z - r6.x). Pass biasScale = 0.275 for
    // cascade 0, 1.0 for cascade 1.
    float zRef = posLightH.z - cascadeDepthRcp * biasScale;

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

// Entry point.

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;   // unused interpolant; matches corpus ISGN (semantic index 14)
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;  // -> kDiffuseBuffer (RT 58)
    float4 specular : SV_Target1;  // -> kSpecularBuffer (RT 59)
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

#ifdef WETNESS_EFFECTS
    float wetness = saturate(
        g_tWetnessMask.Load(int3(int2(input.position.xy), 0)).x);
#endif

    // Insn 0: r0.xyzw = position.xy * cb2[0].xyzw -> uv + scaled coords
    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    // Insn 1-3: depth sample with explicit gradients (sample_d) - lets the
    // sampler choose mip via derivatives. The result goes into r1.x.
    float ddx_ = ddx_coarse(uv4.x);
    float ddy_ = ddy_coarse(uv4.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    // Native near select is inclusive (exec-diff verified at exactly 0.01).
    // Insn 4-17: depth-based matrix select.
    // Per-row ternary matches corpus shape closer than `float4x4` ?:.
    bool isNearPath = (depth <= 0.01);
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

#ifdef AMBIENT_IBL_IN_LIGHT
    float3 ambientDiffuse = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

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
                                         0.0, c0DepthRcp, kernelScale, 0.275);
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
                                         1.0, c1DepthRcp, kernelScale, 1.0);
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

    // Insn 121-127: distance fade toward unshadowed (1.0) at the far range.
    //   D    = saturate(dot(posView, posView) / cb2[24].x)   (insn 122)
    //   fade = 1 - D^8   (insns 123-125: D^2, D^4, then 1 - (D^4)^2)
    //   shadowPcf = fade * (shadowPcf - 1) + 1               (insns 126-127)
    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;   // D^2  (insn 123)
    float dist4      = dist2 * dist2;         // D^4  (insn 124)
    float fadeFactor = 1.0 - dist4 * dist4;   // 1 - D^8  (insn 125)
    shadowPcf = fadeFactor * (shadowPcf - 1.0) + 1.0;

#ifdef SCREEN_SPACE_SHADOWS
    // SSS injection: multiply the Bend screen-space shadow into the sun shadow
    // term (upstream analog: dirDetailedShadow *= GetScreenSpaceShadow). Texel
    // load at the pixel coord; the mask reads 1 (unshadowed) where the feature
    // is inactive, so this is a no-op with SSS off.
    shadowPcf *= g_tScreenSpaceShadow.Load(int3(int2(input.position.xy), 0)).x;
#endif

    // Insn 128-132: setup for sun-direction lighting.
    //   r2.yzw = albedo.w * albedo.xyz   (premult albedo by .w)
    //   NdotL = dot(normalView, cb2[1].xyz)
    //   N.L_sat = max(NdotL, 0.0)
    //   N.L_clamped = min(N.L_sat, 1.0)
    //   r6.z = 1.0 - saturate(cb12[30].y)
    //   r6.w = r6.z * r6.z * (r6.z * r6.z) = pow(r6.z, 4)
    //   r6.z = 1.0 - r6.z * r6.w  // = 1 - (1-cb12[30].y)^5  approx Schlick
    float3 albedoPremult = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw     = dot(normalView, SunDirection_and_padding.xyz);
    float  NdotL_pos     = max(NdotL_raw, 0.0);
    float  NdotL_clamped = min(NdotL_pos, 1.0);
    float  oneMinusGloss = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres   = 1.0 - oneMinusGloss * oneMinusGloss4;

    // Insn 137-138: material-1 (skin) test
    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

#ifdef WETNESS_EFFECTS
    float wetFilmRoughness = max(saturate(1.0 - wetness), 0.05);
    float wetFilmStrength = saturate(1.0 - wetFilmRoughness);
    float3 wetFilmNormal = isMaterial1 ? matSample.xyz : normalView;
    wetFilmNormal *=
        rsqrt(max(dot(wetFilmNormal, wetFilmNormal), 1.0e-8));
    float3 wetFilmHalf = viewDirNeg + SunDirection_and_padding.xyz;
    wetFilmHalf *= rsqrt(max(dot(wetFilmHalf, wetFilmHalf), 1.0e-8));
#endif

    // Material-id-branched BRDF block (insns 139-242).
    // Both branches compute a "specular contribution" r7.xyz and accumulate.
    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        // Insn 140-176: material-code-1 branch. It runs a two-lobe
        // anisotropic hair-specular model, using the engine's
        // fHairPrimSpec* / fHairSecSpec* values (see CB12[28] and CB12[29]).
        // The shading model is proven. Whether material code 1 is
        // exclusively hair is NOT proven: the prepass material-code writer
        // has not been traced. Do not assume this is a skin path and attach
        // subsurface scattering to it.
        // It dots the t2 sample's xyz (a distinct normal in that gbuffer
        // slot), not the octahedral normal the other paths decode from t1.
        float skinNdotL = dot(matSample.xyz, SunDirection_and_padding.xyz);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity = saturate(cb12_idx28_sss_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoSample.w, sssIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 = exp2(log2(vis2) * cb12_idx28_sss_params.y) * cb12_idx28_sss_params.x;

        brdfSpecular  = NdotL_clamped * (pow2 * SunColor_HDR.xyz);
        brdfModulator = 0.0;
    }
    else
    {
        // Insn 178-242: default Cook-Torrance branch.
        float depthScale = matSample.z * 100.0;
        float specExpBase = exp2(matSample.x * 10.0 + 1.0);
        float specExpScale = 1.0 - schlickFres * 0.98;
        float specExp = specExpScale * specExpBase;

        float NdotV_raw = dot(viewDirNeg, normalView);
#ifdef AMBIENT_IBL_IN_LIGHT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
        float oneMinusNdotV = 1.0 - saturate(NdotV_raw);
        float ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
        ambientSpecular =
            matSample.y * ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir);
#endif
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = SunDirection_and_padding.xyz - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_pos * visibilityGeom;

        float3 halfVec = SunDirection_and_padding.xyz - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));

        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.159155;
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN = min(NdotL_clamped, NdotV_sat);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_DIRECTIONAL_SPECULAR_SCALE;

        brdfSpecular = NdotL_clamped * (specMag * SunColor_HDR.xyz);
        brdfModulator = depthScale;
    }

#ifdef WETNESS_EFFECTS
    float wetNdotL = clamp(
        dot(wetFilmNormal, SunDirection_and_padding.xyz), 1.0e-5, 1.0);
    float wetNdotV =
        saturate(abs(dot(wetFilmNormal, viewDirNeg)) + 1.0e-5);
    float wetNdotH = saturate(dot(wetFilmNormal, wetFilmHalf));
    float wetVdotH = saturate(dot(viewDirNeg, wetFilmHalf));
    float wetOneMinusVdotH = 1.0 - wetVdotH;
    float wetOneMinusVdotH2 = wetOneMinusVdotH * wetOneMinusVdotH;
    float wetFresnel =
        0.02 +
        0.98 * wetOneMinusVdotH2 * wetOneMinusVdotH2 *
            wetOneMinusVdotH;
    float wetnessF = wetFilmStrength * wetFresnel;

    float wetA = wetFilmRoughness * wetFilmRoughness;
    float wetA2 = wetA * wetA;
    float wetDdenom = wetNdotH * wetNdotH * (wetA2 - 1.0) + 1.0;
    float wetD = wetA2 / (3.141593 * wetDdenom * wetDdenom);
    float wetVisV =
        wetNdotL * (wetNdotV * (1.0 + wetA) + wetA);
    float wetVisL =
        wetNdotV * (wetNdotL * (1.0 + wetA) + wetA);
    float wetG = 0.5 / max(wetVisV + wetVisL, 1.0e-6);
    float wetSpecMag =
        min(wetD * wetG * wetFresnel * wetNdotL, 15.0) *
        wetFilmStrength;
    float3 wetFilmSpecular =
        wetNdotL * wetSpecMag * SunColor_HDR.xyz *
        FO4_DIRECTIONAL_SPECULAR_SCALE;
#ifdef AMBIENT_IBL_IN_LIGHT
    float wetAmbientNdotVRaw = dot(wetFilmNormal, viewDirNeg);
    float wetAmbientOneMinusNdotV =
        1.0 - saturate(wetAmbientNdotVRaw);
    float wetAmbientOneMinusNdotV2 =
        wetAmbientOneMinusNdotV * wetAmbientOneMinusNdotV;
    float wetAmbientFresnel =
        0.02 +
        0.98 * wetAmbientOneMinusNdotV2 * wetAmbientOneMinusNdotV2 *
            wetAmbientOneMinusNdotV;
    float ambientWetnessF = wetFilmStrength * wetAmbientFresnel;
    float3 wetAmbientReflection =
        2.0 * wetAmbientNdotVRaw * wetFilmNormal - viewDirNeg;
    float3 wetFilmAmbient =
        EvaluateAmbientGradient(wetAmbientReflection);
#endif
#endif

    // Insn 243-269: final composition.
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float ambientFres = 1.0 - NdotV_view;
    ambientFres = exp2(log2(ambientFres) * 0.01);

    float fresEdge = saturate(dot(viewDirNeg, -SunDirection_and_padding.xyz));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;

    float3 finalDiffuse  = SunColor_HDR.xyz * ambientTerm;
    finalDiffuse += SunColor_HDR.xyz * brdfShadowMix;

    float backfaceWrap = saturate(-NdotL_raw);
    finalDiffuse += SunColor_HDR.xyz * (albedoPremult * backfaceWrap);

    float forwardBlend = saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * SunColor_HDR.xyz) * albedoSample.xyz;

    // Specular accumulation in o1
    float specMix = (1.0 - schlickFres * 0.5);
    output.specular.xyz = shadowPcf * specMix * brdfSpecular;
#ifdef WETNESS_EFFECTS
    output.specular.xyz *= 1.0 - wetnessF;
    output.specular.xyz += shadowPcf * wetFilmSpecular;
#ifdef AMBIENT_IBL_IN_LIGHT
    ambientSpecular =
        ambientSpecular * (1.0 - ambientWetnessF) +
        wetFilmAmbient * ambientWetnessF;
#endif
#endif
#ifdef AMBIENT_IBL_IN_LIGHT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w   = 1.0;

    // Diffuse: scaled by 1/3 (insn 269 div by 3,3,3,3)
    output.diffuse.xyz = shadowPcf * finalDiffuse;
#ifdef WETNESS_EFFECTS
    output.diffuse.xyz *= 1.0 - wetnessF;
#ifdef AMBIENT_IBL_IN_LIGHT
    ambientDiffuse *= 1.0 - ambientWetnessF;
#endif
#endif
#ifdef AMBIENT_IBL_IN_LIGHT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w   = 0.0;

    return output;
}

#endif // LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL

// Round-trip notes (for the reviewer + future maintainer)
// fxc round-trip status: see local roundtrip notes for the
// compile output + insn-count delta against the original.
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
// What is approximated rather than asm-exact:
//   * The Poisson kernel only has 32 entries inlined here vs 999 in
//     the original asm. The loop only accesses 16 entries so the
//     rendered output should match for any single dispatch, but the
//     bytecode-level icb array size differs.
//   * BRDF branches (insns 139-242) are rebuilt to the corpus math;
//     remaining drift is mostly compiler scheduling/SSA shape differences.
//   * Round-trip target: keep instruction delta in single digits while
//     preserving bindings/signatures and sample/load parity.
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

// LIGHT_TYPE_POINT branch (the unshadowed point-light path).
// Canonical mapping:
//   * Runtime sha1:  9969e800683c... (FO4_frame24669, QASmoke + Pip-Boy light)
//   * Shape:         ps_5_0, 215 instructions, 5 samples, 5 SRVs
//                    (t0/t1/t2/t3/t7 texture2d), 5 default samplers
//                    (s0/s1/s2/s3/s7 - NO comparison sampler), 2 CBs
//                    (CB12[30], CB2[15]), SV_POSITION plus unused POSITION14,
//                    2 MRT outputs (o0.xyzw + o1.xyzw).
// "unshadowed" classification: this PS has no SampleCmp instructions and
// no cascade/cube-shadow texture array. It is dispatched for point
// lights that either (a) have shadow casting disabled, or (b) bake their
// per-pixel attenuation into a 2D light-cookie texture (t7). The tail
// transforms view-space position through cb2[11..14], perspective-divides,
// and applies the bytecode's dual-paraboloid projection before sampling t7.
// What the point branch does (interpreted from asm):
//   1. Reconstruct view-space position from screen UV + depth via the
//      same Far/Near reproject matrix pair as directional (cb12[20..27]).
//   2. toLight = cb2[1].xyz - posView; d = length(toLight).
//   3. radial attenuation = pow(saturate(1 - saturate(d/cb2[1].w)^z), 2.2)
//      where the exponent z = cb2[3].z and the linear scale/bias come
//      from cb2[3].y / cb2[3].x. cb2[1].w is the light radius.
//   4. Light-cookie sample: project posView through cb2[11..14],
//      perspective-divide, dual-paraboloid-project, sample t7. The cookie
//      result (.xyz) modulates both diffuse and specular at the end.
//   5. Early-out: if attenuation <= 0.001, write zeros and return.
//   6. Gbuffer decode + octahedral normal decode identical to directional.
//   7. Material-id branched BRDF (skin SSS vs Schlick+GGX) identical to
//      directional, but using toLight_normalized instead of sun direction.
//   8. MRT: o0 = diffuse * cookie * attenuation / 3, o1 = spec * cookie *
//      attenuation; o1.w = 1.
// Limits of this reconstruction:
//   * cb2 field semantics are inferred from the live bytecode and shaped
//     differential execution.
//   * cb2[11..14] is annotated as a generic 4x4 light-space transform.
//     For an omnidirectional cookie the rows are likely a view-space-
//     to-light-local rotation; the perspective-divide may be a no-op
//     (row3 = (0,0,0,1)) but is preserved structurally per asm.
//   * Other point-light permutations (shadowed-by-cube-map, with
//     shadow bias / depth-range) live in eids 51116 / 51562 in the same
//     capture and are documented but NOT reconstructed here.

#if LIGHT_TYPE == LIGHT_TYPE_POINT

// Constant buffer layouts (point-light variant).

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block (see `deferred_contracts.hlsli`).
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    // [28]: hair specular scales and powers. Same role as in the directional
    //       path. Identifier is a legacy misnomer; see the CB12 block above.
    float4 cb12_idx28_sss_params;

    // [29]: hair specular tangent shifts. Same legacy misnomer.
    float4 cb12_idx29_sss_angles;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: .xy = RcpFrameDim (screen-size invariant for UV computation).
    float4 ScreenSize;

    // [1]: .xyz = view-space light position, .w = light radius.
    //      `cb2[1].xyz - posView = toLight`; .w normalizes distance.
    float4 LightPos_and_Radius;

    // [2]: .xyz = light color HDR.
    float4 LightColor_HDR;

    // [3]: .x = falloff bias; .y = falloff scale;
    //      .z = falloff exponent for the normalized-distance curve.
    //      TODO: identify exact attenuation curve formula.
    float4 cb2_idx3_attenuation_curve;

    // [4..10]: not read by this PS.
    float4 cb2_pad_4_10[7];

    // [11..14]: 4x4 light-space transform matrix. A dp4 against
    //           (posView, 1) yields the projected vector for t7.
    float4 cb2_lightspace_row0;
    float4 cb2_lightspace_row1;
    float4 cb2_lightspace_row2;
    float4 cb2_lightspace_row3;

};

// Resource bindings (point-light variant).

// t0: RT26 kTAAAccumulation; .w supplies the skin alpha mix.
Texture2D<float4> g_tGbufferAlbedo : register(t0);

// t1: RT27 kTAAAccumulationSwap (octahedral 2-channel normal).
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: RT30 unnamed G-buffer auxiliary.
Texture2D<float4> g_tGbufferMaterial : register(t2);

// t3: main depth, sampled with explicit gradients.
Texture2D<float4> g_tMainDepth : register(t3);

// t7: light cookie / projected texture, addressed via dual paraboloid.
Texture2D<float4> g_tLightCookie : register(t7);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);
SamplerState g_sLightCookie     : register(s7);

// Helpers (point-light variant).

// Octahedral normal decode (same as directional helper).
float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}

// Dual-paraboloid cookie projection from the transformed light-space vector.
float2 ProjectCookieUV(float3 dirLightSpace, float unprojectedZ)
{
    float3 d = normalize(dirLightSpace);
    bool negativeHemisphere = (unprojectedZ * 0.5 + 0.5) < 0.0;
    d.z += negativeHemisphere ? -1.0 : 1.0;
    d = normalize(d);
    float2 uv = d.xy / d.zz;
    uv = uv * 0.5 + 0.5;
    uv.y = negativeHemisphere ? (1.0 - uv.y) * 0.5 + 0.5 : uv.y * 0.5;
    return uv;
}

// Entry point (point-light variant).

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    // Screen UV and reconstruction coordinates.
    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    // Insn 3-5: depth sample with derivative-based gradient.
    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    // Insn 6-19: depth-based matrix select (same pattern as directional).
    // Native near select is inclusive (exec-diff verified at exactly 0.01).
    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    // Insn 20-27: reconstruct view-space position.
    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    // Insn 28-39: radial attenuation curve.
    float3 toLight    = LightPos_and_Radius.xyz - posView;
    float  toLightLenSq = dot(toLight, toLight);
    float  d           = sqrt(toLightLenSq);
    float  dNorm       = saturate(d / LightPos_and_Radius.w);
    float  dPowZ       = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float  falloffLin  = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                  + cb2_idx3_attenuation_curve.x);
    float  attenuation = exp2(log2(1.0 - falloffLin) * 2.2);

    bool nearZero = (attenuation <= 0.001);

    if (nearZero)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(toLightLenSq);

    // Sample G-buffer material and normal.
    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01 = 1.0 - matSample.x;

    // Surface-to-camera direction.
    float posViewLenInv = rsqrt(dot(posView, posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;

    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_sat     = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    // Point permutation's material-code-1 (hair-specular) / default
    // material BRDF.
    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity =
            saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 =
            exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
    }
    else
    {
        float specExp = exp2(matSample.x * 10.0 + 1.0);
        float NdotV_raw = dot(viewDirNeg, normalView);
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;

        float3 halfVec = lightDir + viewDirNeg;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));

        float distributionNorm = (specExp + 2.0) * 0.159155;
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN = min(NdotL_clamped, NdotV_sat);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm =
            (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.141593;

        brdfSpecular =
            NdotL_clamped * (specMag * LightColor_HDR.xyz);
    }

    // Edge-fresnel contribution.
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;

    float3 diffuseAccum = LightColor_HDR.xyz * ambientTerm;
    diffuseAccum += LightColor_HDR.xyz * brdfShadowMix;

    float4 posViewHomog = float4(posView, 1.0);
    float3 lsDir;
    lsDir.x = dot(cb2_lightspace_row0, posViewHomog);
    lsDir.y = dot(cb2_lightspace_row1, posViewHomog);
    lsDir.z = dot(cb2_lightspace_row2, posViewHomog);
    float lsW = dot(cb2_lightspace_row3, posViewHomog);
    float2 cookieUV = ProjectCookieUV(lsDir / lsW.xxx, lsDir.z);
    float3 cookieRGB = g_tLightCookie.Sample(g_sLightCookie, cookieUV).xyz;

    diffuseAccum *= cookieRGB;
    float3 specAccum   = cookieRGB * brdfSpecular;

    output.specular.xyz = attenuation * specAccum;
    output.specular.w   = 1.0;
    output.diffuse.xyz  = (attenuation * diffuseAccum) / 3.0;
    output.diffuse.w    = 0.0;

    return output;
}

#endif // LIGHT_TYPE == LIGHT_TYPE_POINT

// Live point proof: FO4_frame24669 sha1 9969e800683c8a7c8afc25f41582415d79cbe47e.
// shader_corpus_diff.py reports CONTRACT PASS, 15/15 declarations, 5/5
// samples, and 205 corpus vs 198 reconstructed executable instructions.
// Reconstructed invariants:
//   * Resource declarations (5 SRVs + 5 default samplers + 2 CBs) at
//     exact slot indices (t0/t1/t2/t3/t7, s0/s1/s2/s3/s7).
//   * CB12[30], CB2[15], SV_POSITION + unused POSITION14, MRT o0 + o1.
//   * Major control flow: depth-based reproject matrix select, radial
//     attenuation curve, early-out, material-id BRDF branch, dual-paraboloid
//     cookie sample, and final cookie-modulated composition.
//   * Shared CB12[20..27] reprojection matrix infrastructure (now the
//     5th shader to use this pattern).
//   * Diffuse / 3 normalization to match directional.

// LIGHT_TYPE_SPOT branch: the two native spot ABIs.
//
// Evidence scope: AE 1.11.221 only. Archive
// `Fallout4 - Shaders.ba2` sha256 4ac98b8fe723..., member
// `shadersfx/shaders011.fxp` sha256 f3254023504c..., 36 decoded blobs
// (9 SPOT + 27 POINTSPOT), plus the 31 POINTOMNI+SHADOW blobs admitted to the
// projected-shadow ABI below. Register allocation is package-table
// recovered and closure-gated (every DXBC read has an allocated constant,
// every allocation is read, top register + 1 = 21 = the declared CB2 size).
//
// SPOT (raw-technique bit 0x400, 9 blobs, CB2 layout A/B):
//   c0 VPOSOffset, c1 LightVector, c2 LightColor, c3 LightAttenuation,
//   c5 SpotData, c20 ShadowLightParam; c11/c12/c14 ShadowMapProj only under
//   GOBOPROJECTION (row 2 / c13 is never read - a planar cookie needs no z).
//   Resources t0..t3 + s0..s3, plus t7/s7 under GOBOPROJECTION. No shadow
//   map, no comparison sampler.
//   Baseline blob sha1 ed0dd942f9cb6b227cff74ca15503572b7577bfb (key
//   0x00000400); gobo baseline 934eccbe8072ec6cea5bab45a7b3c49e9ff8eecc.
//
// POINTSPOT (raw-technique bit 0x20, 27 blobs, CB2 layout C):
//   c0 VPOSOffset, c1 LightVector, c2 LightColor, c3 LightAttenuation,
//   c11..c14 ShadowMapProj, c15 ShadowSampleParam, c19 ShadowFadeParam,
//   c20 ShadowLightParam. SpotData is ABSENT - POINTSPOT has no cone term;
//   its "spot" shape is the projected shadow frustum plus the c20 edge fade.
//   Resources t0..t3 + s0..s3; the shadow map is t4/s4 (default sampler,
//   manual compare) with no FILTER macro, or t5/s5 (mode_comparison) under
//   FILTER_PCF1 / FILTER_PCF9 / FILTER_POISSON; t7/s7 under GOBOPROJECTION.
//   Baseline blob sha1 388c13087069397fcc3f4b2a8e3f96e59c003d34 (key
//   0x00000020).
//
// ShadowLightParam (c20) is the one constant whose meaning differs between
// the families: SPOT reads only .x (cone falloff exponent), POINTSPOT reads
// .xyz (projection edge-fade exponent / centre / scale).
//
// POINTOMNI+SHADOW (31 blobs) declares the POINTSPOT layout register for
// register. Its contract is CB12[30] + CB2[21] immediateIndexed, t0..t3 +
// s0..s3, the shadow map at t5/s5 mode_comparison under FILTER_PCF1 (9),
// FILTER_PCF9 (11) or FILTER_POISSON (8) and at t4/s4 mode_default in the 3
// unfiltered records, plus t7/s7 in the 10 GOBOPROJECTION records. All 31
// carry DIRSPLITS=2, which this path does not read - it is a cascade count and
// only the directional families consume it. 12 additionally carry HALFOMNI,
// whose hemisphere term is reconstructed below; see the header note.

#if LIGHT_TYPE == LIGHT_TYPE_SPOT

// Constant buffer layouts (spot variants).

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block (see `deferred_contracts.hlsli`).
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

#ifndef ATTENUATION_ONLY
    // [28]: hair specular scales and powers. Same role and same legacy
    //       misnomer as the directional and point paths.
    float4 cb12_idx28_sss_params;

    // [29]: hair specular tangent shifts. Same legacy misnomer.
    float4 cb12_idx29_sss_angles;
    // ATTENUATION_ONLY blobs declare CB12[28] because they run no BRDF and
    // therefore never reach cb12[28..29].
#endif
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: constant ID 0 `VPOSOffset`. .xy/.zw scale SV_POSITION into the two
    //      UV pairs, exactly as in the directional and point paths.
    float4 ScreenSize;

    // [1]: constant ID 1 `LightVector`. .xyz = view-space light position,
    //      .w = light radius (the distance normaliser).
    float4 LightPos_and_Radius;

    // [2]: constant ID 2 `LightColor`. .xyz only; .w is never read.
    float4 LightColor_HDR;

    // [3]: constant ID 3 `LightAttenuation`. .x = falloff bias,
    //      .y = falloff scale, .z = falloff exponent. Identical curve to the
    //      point path.
    float4 cb2_idx3_attenuation_curve;

#ifdef SPOT
    // [4]: constant ID 4 `ProjectedLightVector` is ABSENT from every SPOT
    //      blob; this register has no allocation.
    float4 cb2_pad_4;

    // [5]: constant ID 5 `SpotData`, allocated at c5. .xyz = the view-space
    //      cone axis, .w = cosine of the outer cone half-angle (the cutoff).
    //      Read by every SPOT blob and by no POINTSPOT blob.
    float4 SpotData;

    // [6..10]: unallocated. Constant IDs 6 `DirectionalAmbient`,
    //          7 `SplitDistances` and 8 `FadeDistances` are ABSENT here.
    float4 cb2_pad_6_10[5];

#  ifdef GOBOPROJECTION
    // [11..14]: constant ID 9 `ShadowMapProj`, allocated at c11 and four
    //           registers wide for a non-directional light. SPOT uses it
    //           purely as a cookie projection: rows 0, 1 and 3 are read and
    //           row 2 (c13) is not, because the planar projection needs no z.
    float4 cb2_gobo_row0;
    float4 cb2_gobo_row1;
    float4 cb2_gobo_row2_unread;
    float4 cb2_gobo_row3;
#  else
    // [11..14]: `ShadowMapProj` is ABSENT from the non-gobo SPOT blobs.
    float4 cb2_pad_11_14[4];
#  endif

    // [15..19]: unallocated in every SPOT blob. Constant IDs 10
    //           `ShadowSampleParam`, 11 `ShadowWorldScale` and 12
    //           `ShadowFadeParam` are ABSENT - SPOT casts no shadow.
    float4 cb2_pad_15_19[5];
#endif  // SPOT

#ifdef FO4_PROJECTED_SHADOW_FAMILY
    // [4..10]: unallocated. Constant IDs 4 `ProjectedLightVector`,
    //          5 `SpotData`, 6 `DirectionalAmbient`, 7 `SplitDistances` and
    //          8 `FadeDistances` are all ABSENT from every blob in this family,
    //          POINTSPOT and POINTOMNI+SHADOW alike. SpotData in particular:
    //          neither carries a cone term.
    float4 cb2_pad_4_10[7];

    // [11..14]: constant ID 9 `ShadowMapProj`, allocated at c11, four
    //           registers for a non-directional light. dp4 against
    //           (posView, 1) then a perspective divide yields the light-space
    //           projection used for both the shadow lookup and, under
    //           GOBOPROJECTION, the cookie.
    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;
    float4 cb2_shadowproj_row3;

    // [15]: constant ID 10 `ShadowSampleParam`, allocated at c15.
    //       .x = the depth-compare bias subtracted from projected z.
    //       .zw = the shadow-map texel step used by the PCF9 and Poisson
    //       kernels. .y is allocated but never read.
    float4 cb2_idx15_shadow_sample_param;

    // [16..18]: unallocated. Constant ID 11 `ShadowWorldScale` is ABSENT.
    float4 cb2_pad_16_18[3];

    // [19]: constant ID 12 `ShadowFadeParam`, allocated at c19. .x is a
    //       squared world distance; the native POINTSPOT path multiplies the
    //       shadow factor by 1 - t^8, where t = saturate(|posView|^2 / .x).
    float4 cb2_idx19_shadow_fade;
#endif  // FO4_PROJECTED_SHADOW_FAMILY

    // [20]: constant ID 13 `ShadowLightParam`, allocated at c20 in both
    //       families but read differently.
    //       SPOT reads .x only: the cone falloff exponent.
    //       POINTSPOT reads .xyz: .x = the projection edge-fade exponent,
    //       .y = the fade centre, .z = the fade scale.
    float4 ShadowLightParam;
};

// Resource bindings (spot variants).

#ifndef ATTENUATION_ONLY
// t0: RT26 kTAAAccumulation; .w supplies the skin alpha mix.
Texture2D<float4> g_tGbufferAlbedo : register(t0);

// t1: RT27 kTAAAccumulationSwap (octahedral 2-channel normal).
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: RT30 unnamed G-buffer auxiliary.
Texture2D<float4> g_tGbufferMaterial : register(t2);
#endif

// t3: main depth, sampled with explicit gradients.
Texture2D<float4> g_tMainDepth : register(t3);

#ifdef FO4_PROJECTED_SHADOW_FAMILY
#  if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_POISSON)
// t5/s5: projected shadow map, sampled through hardware comparison PCF. The
//        native blobs bind a Texture2DArray and always address slice 0.
Texture2DArray<float4> g_tSpotShadowAtlas : register(t5);
SamplerComparisonState g_sSpotShadowCmp : register(s5);  // mode_comparison
#  else
// t4/s4: the unfiltered permutations of this family bind the shadow map to a
//        plain sampler and perform the depth compare in the shader.
Texture2DArray<float4> g_tSpotShadowAtlas : register(t4);
SamplerState g_sSpotShadow : register(s4);
#  endif
#endif

#ifdef GOBOPROJECTION
// t7: light cookie / gobo, sampled with a plain projective UV.
Texture2D<float4> g_tLightCookie : register(t7);
SamplerState g_sLightCookie : register(s7);
#endif

#ifndef ATTENUATION_ONLY
SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
#endif
SamplerState g_sMainDepth       : register(s3);

#ifdef FILTER_POISSON
// Stratified Poisson kernel. The native dcl_immediateConstantBuffer holds
// 1000 float4 entries whose .xy are consumed; the raw DWORD payload is
// byte-identical across every POISSON blob in the archive, including the
// directional one. The loop reads icb[0..15] only, so the 16 consumed entries
// are inlined here at full float32 precision (extracted from the SHEX
// CUSTOM_DATA token, not from disassembly text).
//   tap = ((icb[k] - 0.5) * (ShadowSampleParam.z * 3.0)) * 2.0 + baseUV
static const float2 SPOT_SHADOW_POISSON[16] =
{
    float2(0.4933930039405823,   0.3942689895629883),
    float2(0.7985470294952393,   0.8859220147132874),
    float2(0.2473219931125641,   0.9264500141143799),
    float2(0.051454201340675354, 0.14078199863433838),
    float2(0.8318430185317993,   0.009552289731800556),
    float2(0.428631991147995,    0.017151400446891785),
    float2(0.01565600000321865,  0.7497789859771729),
    float2(0.7583850026130676,   0.4961700141429901),
    float2(0.2234870046377182,   0.5621510148048401),
    float2(0.011627599596977234, 0.4069949984550476),
    float2(0.24146200716495514,  0.30463600158691406),
    float2(0.430310994386673,    0.7272260189056396),
    float2(0.981810986995697,    0.27835899591445923),
    float2(0.4070560038089752,   0.5005339980125427),
    float2(0.123478002846241,    0.4635460078716278),
    float2(0.8095340132713318,   0.6822720170021057),
};
#endif

// Helpers (spot variants).

#ifndef ATTENUATION_ONLY
// Octahedral normal decode (same helper as the directional and point paths).
float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}
#endif

// Entry point (spot variants).

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    // Screen UV and reconstruction coordinates.
    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    // Depth sample with derivative-based gradient.
    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    // Depth-based reproject matrix select. The native near test is inclusive
    // at exactly 0.01, matching directional and point.
    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    // Reconstruct view-space position.
    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    // Radial attenuation curve, identical to the point path.
    float3 toLight      = LightPos_and_Radius.xyz - posView;
    float  toLightLenSq = dot(toLight, toLight);
    float  d            = sqrt(toLightLenSq);
    float  dNorm        = saturate(d / LightPos_and_Radius.w);
    float  dPowZ        = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float  falloffLin   = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                   + cb2_idx3_attenuation_curve.x);
    float  attenuation  = exp2(log2(1.0 - falloffLin) * 2.2);

#ifdef SPOT
    // SPOT cone falloff. The native code normalises the light vector before
    // the early-out because the cone test consumes it.
    float3 lightDir = toLight * rsqrt(toLightLenSq);

    //   cosA  = saturate(dot(-lightDir, SpotData.xyz))
    //   coneT = (1 - cosA) / (1 - SpotData.w)
    //   cone  = min(pow(saturate(1 - coneT), ShadowLightParam.x), 1)
    // The dot product is saturated, and the trailing min against 1 is present
    // in the native code; neither may be folded away.
    float coneCos   = saturate(dot(-lightDir, SpotData.xyz));
    float coneDenom = 1.0 - SpotData.w;
    float coneT     = (1.0 - coneCos) / coneDenom;
    float coneEdge  = saturate(1.0 - coneT);
    float coneFall  = exp2(log2(coneEdge) * ShadowLightParam.x);
    coneFall = min(coneFall, 1.0);

    attenuation = coneFall * attenuation;
#endif

    bool nearZero = (attenuation <= 0.001);

    if (nearZero)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

#ifdef FO4_PROJECTED_SHADOW_FAMILY
    // POINTSPOT normalises the light vector after the early-out; there is no
    // cone term to consume it beforehand. POINTOMNI+SHADOW shares the code path
    // but not necessarily the intent - see the HALFOMNI note in the header.
    float3 lightDir = toLight * rsqrt(toLightLenSq);
#endif

#ifndef ATTENUATION_ONLY
    // Sample G-buffer material and normal.
    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01 = 1.0 - matSample.x;
#endif

    // Surface-to-camera direction.
    float posViewLenInv = rsqrt(dot(posView, posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;

#ifdef FO4_PROJECTED_SHADOW_FAMILY
    float4 posViewHomog = float4(posView, 1.0);
    float2 shadowUV;
    float shadowRef;
    float shadowSlice;

#  if defined(POINTOMNI) && defined(SHADOW)
    float3 shadowProj;
    shadowProj.x = dot(cb2_shadowproj_row0, posViewHomog);
    shadowProj.y = dot(cb2_shadowproj_row1, posViewHomog);
    shadowProj.z = dot(cb2_shadowproj_row2, posViewHomog);
    float zHalf = shadowProj.z * 0.5 + 0.5;
    float shadowProjW = dot(cb2_shadowproj_row3, posViewHomog);
    shadowProj /= shadowProjW.xxx;

    float radial = length(shadowProj);
#    ifdef HALFOMNI
    // HALFOMNI keeps the +Z pole and rejects the opposite half.
    bool halfAccepted = (zHalf >= 0.0);
    bool backHemisphere = false;
#    else
    bool backHemisphere = (zHalf < 0.0);
#    endif
    float3 pole = backHemisphere ? float3(0.0, 0.0, -1.0)
                                 : float3(0.0, 0.0, 1.0);
    float3 paraboloid = normalize(normalize(shadowProj) + pole);
    float2 omniUV = (paraboloid.xy / paraboloid.zz) * 0.5 + 0.5;
    float selectedY = backHemisphere ? omniUV.y : (1.0 - omniUV.y);

    shadowUV.x = omniUV.x * ShadowLightParam.z;
    shadowUV.y = 1.0 - selectedY * ShadowLightParam.z;
    shadowSlice = backHemisphere ? 1.0 : 0.0;
    shadowRef = saturate(radial / LightPos_and_Radius.w)
              - cb2_idx15_shadow_sample_param.x;
#  else
    // POINTSPOT uses a planar projection and always samples slice zero.
    float3 shadowProj;
    shadowProj.x = dot(cb2_shadowproj_row0, posViewHomog);
    shadowProj.y = dot(cb2_shadowproj_row1, posViewHomog);
    shadowProj.z = dot(cb2_shadowproj_row2, posViewHomog);
    float shadowProjW = dot(cb2_shadowproj_row3, posViewHomog);
    shadowProj /= shadowProjW.xxx;

    shadowUV = shadowProj.xy * 0.5 + 0.5;
    shadowSlice = 0.0;
    shadowRef = shadowProj.z - cb2_idx15_shadow_sample_param.x;
#  endif

    float shadowFactor;
#  if defined(FILTER_POISSON)
    // 8 iterations, 2 taps each, 16 taps averaged by 1/16.
    float poissonScale = cb2_idx15_shadow_sample_param.z * 3.0;
    float poissonSum = 0.0;
    for (int p = 0; p < 8; ++p)
    {
        float2 tap0 = (SPOT_SHADOW_POISSON[p * 2 + 0] - 0.5) * poissonScale;
        float2 tap1 = (SPOT_SHADOW_POISSON[p * 2 + 1] - 0.5) * poissonScale;
        poissonSum += g_tSpotShadowAtlas.SampleCmpLevelZero(
            g_sSpotShadowCmp, float3(tap0 * 2.0 + shadowUV, shadowSlice), shadowRef);
        poissonSum += g_tSpotShadowAtlas.SampleCmpLevelZero(
            g_sSpotShadowCmp, float3(tap1 * 2.0 + shadowUV, shadowSlice), shadowRef);
    }
    shadowFactor = poissonSum / 16.0;
#  elif defined(FILTER_PCF9)
    // 3x3 kernel stepped by the shadow-map texel size, averaged by 1/9.
    float pcfSum = 0.0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            float2 tapOffset = float2(i - 1, j - 1)
                             * cb2_idx15_shadow_sample_param.zw;
            pcfSum += g_tSpotShadowAtlas.SampleCmpLevelZero(
                g_sSpotShadowCmp, float3(tapOffset + shadowUV, shadowSlice), shadowRef);
        }
    }
    shadowFactor = pcfSum / 9.0;
#  elif defined(FILTER_PCF1)
    shadowFactor = g_tSpotShadowAtlas.SampleCmpLevelZero(
        g_sSpotShadowCmp, float3(shadowUV, shadowSlice), shadowRef);
#  else
    // Unfiltered: plain sample plus an explicit compare, yielding 1.0 or 0.0.
    float shadowDepth = g_tSpotShadowAtlas.Sample(
        g_sSpotShadow, float3(shadowUV, shadowSlice)).x;
    shadowFactor = (shadowDepth >= shadowRef) ? 1.0 : 0.0;
#  endif

#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI)
    shadowFactor = halfAccepted ? shadowFactor : 0.0;
#  endif

#  if !(defined(POINTOMNI) && defined(SHADOW))
    // POINTSPOT projection edge fade. The same vector feeds its gobo lookup.
    float2 projFade = (float2(shadowUV.x, 1.0 - shadowUV.y) - ShadowLightParam.y)
                    / ShadowLightParam.z;
    float  projDist = sqrt(dot(projFade, projFade));
    float  edgeFall = exp2(log2(projDist) * ShadowLightParam.x);
    edgeFall = min(edgeFall, 1.0);
    shadowFactor *= (1.0 - edgeFall);
#  endif

    // Native POINTSPOT distance factor: 1 - t^8, applied as a plain multiply.
    float shadowDistNorm = saturate(dot(posView, posView)
                                    / cb2_idx19_shadow_fade.x);
    float shadowDist2 = shadowDistNorm * shadowDistNorm;
    float shadowDist4 = shadowDist2 * shadowDist2;
    shadowFactor *= (1.0 - shadowDist4 * shadowDist4);

    attenuation = attenuation * shadowFactor;
#endif  // FO4_PROJECTED_SHADOW_FAMILY

#ifndef ATTENUATION_ONLY
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_sat     = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    // Material-code-1 (hair-specular) / default material BRDF. Both branches
    // are the same code the point permutation already carries.
    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity =
            saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

#  ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 =
            exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#  endif
    }
    else
    {
#  ifdef IGNOREROUGHNESS
        // IGNOREROUGHNESS collapses the default branch to plain N.L.
        brdfShadowMix = max(dot(lightDir, normalView), 0.0);
#  else
        float NdotV_raw = dot(viewDirNeg, normalView);
        float3 tangentV = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL = lightDir - normalView * NdotL_raw;
        float tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA = roughSq / (roughSq + 0.57);
        float visB = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                         * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix = NdotL_sat * visibilityGeom;
#  endif

#  ifdef SPECULAR
        float specExp = exp2(matSample.x * 10.0 + 1.0);
        float NdotV_spec = dot(viewDirNeg, normalView);

        float3 halfVec = lightDir + viewDirNeg;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_spec);
        float VdotH = saturate(dot(viewDirNeg, halfVec));
        float NdotH = saturate(dot(halfVec, normalView));

        float distributionNorm = (specExp + 2.0) * 0.159155;
        float distribution = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN = min(NdotL_clamped, NdotV_sat);
        float twoNdotH = NdotH + NdotH;
        bool usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV = NdotL_clamped / NdotV_sat;
        float ratio = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm =
            (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.141593;

        brdfSpecular = NdotL_clamped * (specMag * LightColor_HDR.xyz);
#  endif
    }

    float3 diffuseAccum = LightColor_HDR.xyz * brdfShadowMix;

#  if !defined(IGNORERIM) && !defined(IGNOREROUGHNESS)
    // Rim / backscatter tail. Both IGNORERIM and IGNOREROUGHNESS remove it:
    // the term is scaled by the smoothness that IGNOREROUGHNESS deletes.
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;
    diffuseAccum = LightColor_HDR.xyz * ambientTerm + diffuseAccum;
#  endif
#else   // ATTENUATION_ONLY
    // ATTENUATION_ONLY runs no BRDF at all: the light colour is modulated by
    // the attenuation and shadow terms and nothing else.
    float3 diffuseAccum = LightColor_HDR.xyz;
    float3 brdfSpecular = float3(0, 0, 0);
#endif

#ifdef GOBOPROJECTION
#  ifdef SPOT
    // SPOT gobo: a plain planar projective cookie. Row 2 of ShadowMapProj is
    // deliberately not read. This is NOT the point path's dual-paraboloid
    // fold - no SPOT blob evaluates a hemisphere sign.
    float4 goboHomog = float4(posView, 1.0);
    float2 goboProj;
    goboProj.x = dot(cb2_gobo_row0, goboHomog);
    goboProj.y = dot(cb2_gobo_row1, goboHomog);
    float goboW = dot(cb2_gobo_row3, goboHomog);
    float2 goboUV = (goboProj / goboW.xx) * 0.5 + 0.5;
#  elif defined(POINTOMNI) && defined(SHADOW)
#    ifdef HALFOMNI
    // The fixed +Z paraboloid UV is the whole cookie; no half packing.
    float2 goboUV = omniUV;
#    else
    // v packs the front hemisphere into the lower half and mirrors the back
    // hemisphere above it. Not radial, and no ShadowLightParam atlas remap.
    float2 goboUV = float2(omniUV.x,
                           backHemisphere ? 1.0 - 0.5 * omniUV.y
                                          : 0.5 * omniUV.y);
#    endif
#  else
    // POINTSPOT gobo: reuses the shadow projection through the same
    // ShadowLightParam remap that drives the edge fade, with the v axis
    // negated.
    float2 goboUV = float2(projFade.x, -projFade.y) * 0.5 + 0.5;
#  endif
    float3 cookieRGB = g_tLightCookie.Sample(g_sLightCookie, goboUV).xyz;

    diffuseAccum *= cookieRGB;
    brdfSpecular *= cookieRGB;
#endif

    output.specular.xyz = attenuation * brdfSpecular;
    output.specular.w   = 1.0;
    output.diffuse.xyz  = (attenuation * diffuseAccum) / 3.0;
    output.diffuse.w    = 0.0;

    return output;
}

#endif // LIGHT_TYPE == LIGHT_TYPE_SPOT

// Spot reconstruction status.
// Evidence: archive-only. `light-blob-enumeration.json` (schema
// fo4re.light-blob-enumeration v1, runtime profile AE-1.11.221) decodes 9
// SPOT and 27 POINTSPOT blobs out of the 166-blob BSDFLightShader set, with a
// 166/166 constant-table closure PASS. A later producer audit recompiled all
// 73 execution-unproven blobs and reimplemented harness reflection, finding 31
// POINTOMNI+SHADOW blobs whose ABI and FXP constant tables match the POINTSPOT
// profiles; they are admitted to the projected-shadow branch above, and all 31
// are contract-equal to their corpus blob (CB sizes and indexing mode, SRV
// slots and types, sampler slots and modes, IO signature). Admission is an ABI
// claim only. The base omni projection, the HALFOMNI hemisphere term and the
// omni cookie coordinates are reconstructed.
// Every route in that table carries
// opaque_psid_status "not-observed" and raw_technique_status
// "hypothesis-matched", so this is archive evidence, not an observed engine
// route - the same standing as the already-shipped point permutation.
// NOT proven: `shader_exec_diff` harness v4 cannot measure any of the 36
// blobs. Its point-lighting-live contract requires CB2 to be exactly 15
// float4, exactly five Texture2D at t0..t3/t7 and no comparison sampler,
// whereas every spot blob declares CB2[21] and POINTSPOT additionally binds a
// Texture2DArray and (under FILTER_*) a mode_comparison sampler. Profile
// detection therefore returns Unshaped and the run stops before any pixel
// comparison. The producer must add spot input profiles, constant shaping and
// bucket predicates, and must split LIGHT_TYPE=3 into per-family defines,
// before a conformance entry can exist for either permutation.
// Consequently `scripts/shaders/shader-fidelity-conformance.json` carries no
// spot target and, by that manifest's own rule, absence is a fidelity claim
// of "none" for both permutations here.
// Runtime scope: AE 1.11.221 only. OG and NG archives were never enumerated.

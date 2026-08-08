// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 BSDFLightShader deferred PS, native DIRECTIONAL + SHADOW_ONLY +
// BLENDSPLIT family: the FILTER_* axis, with and without AMBIENT.
//
// A sibling of `bsdf_light_deferred_shadow_only.hlsl`, not a variant of it. That
// file holds the plain DIRECTIONAL + SHADOW_ONLY family and is pinned
// byte-for-byte by `FilterAxisNativeShex`; adding BLENDSPLIT there would move
// its bytes. BLENDSPLIT is a separate native selector and it changes the body,
// not just the epilogue: the material-1 slope bias disappears entirely, and
// AMBIENT turns the permutation into a two-resource-group family that decodes
// the g-buffer normal and adds an ambient gradient term to both MRTs.
//
// Reconstructed from six archive blobs - DIRECTIONAL + DIRSPLITS=1 + RGBSPEC +
// SHADOW + SHADOW_ONLY + SPECULAR + BLENDSPLIT, one filter each, AMBIENT on the
// second group only:
//   abi-001, no AMBIENT   CB2[25], CB12[28], t3/s3 + t5/s5 comparison
//     FILTER_PCF1     sha1 4e89dd81f40121cadb5f4b322a7323331576f05c,  1596 B
//     FILTER_PCF9     sha1 1eb732dd3f746afb73b3c3f785eb6ec5af93f1a7,  2012 B
//     FILTER_POISSON  sha1 e928ab5f44eede452d26262c9cd09ad0ca3839b7, 18348 B
//   abi-002, with AMBIENT CB2[25], CB12[28], t1/s1 + t2/s2 + t3/s3 + t5/s5
//     FILTER_PCF1     sha1 2b04bed67a5641536ecedceebedf8916d3130a97,  3076 B
//     FILTER_PCF9     sha1 e3b027b8c5549dc429abf8356c0a1e300616027d,  3500 B
//     FILTER_POISSON  sha1 c56b2b7862b68c7a7753e9c543a48378cb0c607a, 19828 B
//
// All six compile to a DXBC container that is byte-identical to the archive
// blob. `scripts/shaders/verify-filter-axis.ps1` re-measures the SHEX chunk of
// each on every test run.
//
// The native filter axis here is exactly three wide. No BLENDSPLIT blob in the
// archive carries FILTER_PCSS, FILTER_PCSSPOISSON or no filter at all, so this
// source refuses those macro sets rather than inventing a branch for them - the
// raw t4/s4 tap those three would need is absent from all six declaration sets.
//
// The three no-AMBIENT permutations compile to a DXBC container that is
// byte-identical to the archive blob, re-measured by
// `scripts/shaders/ds1-blendsplit-native-shex.json`. The three AMBIENT ones
// reach the same instruction multiset, operands, register allocation, literals
// and SHEX byte length, and differ only in where fxc schedules one
// dependency-free `mov o1.w, l(1.0)` - the shipped blobs emit it three slots
// earlier, and that placement did not move under any optimisation level or
// codegen flag. All six are pinned on declarations, constant read-sets,
// read-counts and immediate-constant rows by
// `scripts/shaders/ds1-blendsplit-native-abi.json`.

#if !defined(DIRECTIONAL) || !defined(SHADOW_ONLY) || !defined(BLENDSPLIT)
#  error "this source is the native DIRECTIONAL + SHADOW_ONLY + BLENDSPLIT family; define all three"
#endif
#if !defined(SHADOW) || !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native SHADOW_ONLY + BLENDSPLIT blob carries SHADOW, SPECULAR and RGBSPEC"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 1
#  error "the reconstructed SHADOW_ONLY + BLENDSPLIT family is DIRSPLITS=1 only"
#endif
#if defined(POINTOMNI) || defined(HALFOMNI) || defined(SPOT) || defined(POINTSPOT)
#  error "mixed light kinds; this family is DIRECTIONAL"
#endif
#if defined(LIGHT_TYPE)
#  error "LIGHT_TYPE is the legacy adapter axis in bsdf_light_deferred.hlsl, not a native macro"
#endif
#if defined(AMBIENT_IBL_IN_LIGHT)
#  error "AMBIENT_IBL_IN_LIGHT is a legacy adapter axis in bsdf_light_deferred.hlsl, not a native macro"
#endif
#if defined(GOBOPROJECTION)
#  error "GOBOPROJECTION declares t7/s7 and is a different resource contract"
#endif
#if defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON)
#  error "no BLENDSPLIT blob carries a blocker search; the raw t4/s4 tap is absent from this family"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_POISSON)) != 1
#  error "define exactly one of FILTER_PCF1, FILTER_PCF9, FILTER_POISSON"
#endif

// Shared CB12[0..27] per-frame schema (single source of truth across the
// deferred-pipeline PS reconstructions).
#include "deferred_contracts.hlsli"

#ifdef FILTER_POISSON
#  include "shadow_poisson_kernel.hlsli"
#endif

// Constant buffer layouts (native directional shadow-only blendsplit).

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block. Every blob in this family declares
    //          CB12[28] and reads only the Far/Near reproject rows.
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: constant ID 0 `VPOSOffset`. .xy scales SV_POSITION into the sampling
    //      UV, .zw into the NDC pair.
    float4 ScreenSize;

    // [1..5]: unread. The light vector, colour and attenuation constants that
    //         the full-lighting directional path allocates have no reader once
    //         SHADOW_ONLY drops the BRDF.
    float4 cb2_pad_1_5[5];

    // [6..8]: constant ID 4 `DirectionalAmbient`, one register_count 3
    //         constant. Each row is dotted with (normal, 1), so this is a
    //         hemispheric gradient rather than three independent colours. Read
    //         only under AMBIENT, and read twice there: once for the surface
    //         normal and once for the reflection vector.
    float4 cb2_ambient_row0;
    float4 cb2_ambient_row1;
    float4 cb2_ambient_row2;

    // [9]: .y is the cascade slice index. It reaches the shader as the
    //      Texture2DArray slice, and under FILTER_POISSON also as the `ftou`
    //      index into the cascade world-scale table below, so it must stay one
    //      value. .x is never read.
    float4 cb2_idx9_cascade_slice;

    // [10]: unread. `FadeDistances` drives the cascade blend and a DIRSPLITS=1
    //       blob has nothing to blend.
    float4 cb2_pad_10;

    // [11..13]: constant ID 9 `ShadowMapProj`, three rows. dp4 against
    //           (posView, 1) yields the light-space (x, y, z). An orthographic
    //           cascade needs no w row.
    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;

    // [14..19]: unread. The second cascade's rows live here in the multi-split
    //           families; DIRSPLITS=1 leaves the registers allocated and dead.
    float4 cb2_pad_14_19[6];

    // [20]: constant ID 10 `ShadowSampleParam`. FILTER_PCF9 reads .zw as the
    //       3x3 tap step; FILTER_POISSON reads .z as the kernel scale.
    float4 cb2_idx20_shadow_sample_param;

    // [21..23]: constant ID 11 `ShadowWorldScale`, one register per cascade,
    //           indexed by the runtime slice. FILTER_POISSON reads .zw as the
    //           cascade world depth range. The array shape is what the native
    //           `cb2[r + 21]` addressing requires.
    float4 cb2_idx21_cascade_world_scale[3];

    // [24]: constant ID 12 `ShadowFadeParam`. .x is the squared world distance
    //       at which the shadow term fades to unshadowed.
    float4 cb2_idx24_distance_fade;
};

// Resource bindings.
// No t0: SHADOW_ONLY never samples albedo, so the slot is not declared. No
// t4/s4 either: this family has no raw shadow tap in any of its six blobs.

#ifdef AMBIENT
// t1: RT27 kTAAAccumulationSwap, octahedral 2-channel normal. Only .xy read.
Texture2D<float4> g_tGbufferNormal : register(t1);
SamplerState g_sGbufferNormal : register(s1);

// t2: RT30 unnamed G-buffer auxiliary. .x is the specular exponent term, .y the
//     specular intensity, .w the material code.
Texture2D<float4> g_tGbufferMaterial : register(t2);
SamplerState g_sGbufferMaterial : register(s2);
#endif

// t3: main depth, sampled with explicit gradients.
Texture2D<float4> g_tMainDepth : register(t3);
SamplerState g_sMainDepth : register(s3);

// t5/s5: cascade shadow atlas bound for hardware comparison PCF. Every blob in
//        this family takes its shadow through the comparison sampler.
Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);  // mode_comparison

// Entry point.

struct PS_INPUT
{
    float4 position  : SV_POSITION;
    float4 posUnused : POSITION14;   // unused interpolant; matches the corpus ISGN
};

struct PS_OUTPUT
{
    float4 diffuse  : SV_Target0;  // -> kDiffuseBuffer (RT 58)
    float4 specular : SV_Target1;  // -> kSpecularBuffer (RT 59)
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv4.xy,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    // Near select is inclusive at exactly 0.01, matching the full-lighting path.
    // Native keeps this as real control flow, not a flattened select.
    float  linearDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 screenUV = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float4 pos4 = float4(screenUV * 2.0 - 1.0, linearDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

#ifdef AMBIENT
    // .xyw of the auxiliary target: exponent term, intensity, material code.
    float3 material = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv4.xy).xyw;
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv4.xy).xy * 4.0 - 2.0;

    float  encSq = dot(normalEnc, normalEnc);
    float3 normal;
    normal.xy = normalEnc * sqrt(1.0 - encSq * 0.25);
    normal.z = -(1.0 - encSq * 0.5);

    float3 ambientDiffuse;
    ambientDiffuse.x = dot(cb2_ambient_row0, float4(normal, 1.0));
    ambientDiffuse.y = dot(cb2_ambient_row1, float4(normal, 1.0));
    ambientDiffuse.z = dot(cb2_ambient_row2, float4(normal, 1.0));
    ambientDiffuse = pow(ambientDiffuse, 2.2);

    bool isMaterial1 = abs(material.z * 255.0 - 1.0) < 0.25;
#endif

    // Light-space projection, shared by every filter. BLENDSPLIT carries no
    // material-1 slope bias: the projected z is used as it stands.
    float4 posViewH1 = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_shadowproj_row0, posViewH1);
    shadowUV.y = dot(cb2_shadowproj_row1, posViewH1);
    float shadowZ = min(dot(cb2_shadowproj_row2, posViewH1), 0.999999);
    float slice = cb2_idx9_cascade_slice.y;

    float shadow;

#if defined(FILTER_PCF1)
    // One hardware comparison tap, unbiased.
    shadow = g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)
    // 3x3 box of comparison taps, one texel apart, averaged. The native listing
    // shows a single sample_c_lz because it is the body of a rolled 3x3 loop
    // pair, not one executed tap.
    float sum = 0.0;
    [loop]
    for (int i = 0; i < 3; ++i)
    {
        float offsetX = float(i) - 1.0;
        [loop]
        for (int j = 0; j < 3; ++j)
        {
            float offsetY = float(j) - 1.0;
            float2 tapUV = float2(offsetX, offsetY)
                * cb2_idx20_shadow_sample_param.zw + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    shadow = sum * (1.0 / 9.0);

#elif defined(FILTER_POISSON)
    // Eight loop iterations, two taps each, over the leading 16 entries of the
    // 1000-entry immediate kernel. The bias is the fixed 0.275 scaled by the
    // reciprocal cascade world depth range; there is no material-1 branch.
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
    uint  cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - 0.275 * rcpWorldRange;

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), zRef);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), zRef);
    }
    shadow = sum * 0.0625;
#else
#  error "unreachable: the filter guard above admits exactly PCF1, PCF9 or POISSON"
#endif

    // Distance fade toward unshadowed, identical to the full-lighting path:
    // D = saturate(|posView|^2 / cb2[24].x), fade = 1 - D^8.
    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2 = distNorm * distNorm;
    float dist4 = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;

#ifndef AMBIENT
    float3 result = fadeFactor * (shadow - 1.0) + 1.0;

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
#else
    float shadowBlend = fadeFactor * (shadow - 1.0);
    float splitShadow = shadowBlend + 1.0;

    // Reflected ambient. Material code 1 suppresses it outright.
    float3 ambientSpecular;
    [branch]
    if (isMaterial1)
    {
        ambientSpecular = 0.0;
    }
    else
    {
        float3 view = normalize(-posView);
        float  NdotV = dot(normal, view);
        float3 reflected = (NdotV + NdotV) * normal - view;
        float  fresnel = pow(1.0 - saturate(NdotV), 3.0 - material.x) * 0.25;

        float3 ambientReflected;
        ambientReflected.x = dot(cb2_ambient_row0, float4(reflected, 1.0));
        ambientReflected.y = dot(cb2_ambient_row1, float4(reflected, 1.0));
        ambientReflected.z = dot(cb2_ambient_row2, float4(reflected, 1.0));
        ambientSpecular = (fresnel * pow(ambientReflected, 2.2)) * material.y;
    }

    // Both MRTs are a split-shadow float4 plus an ambient float4. Writing o1 as
    // one four-wide add rather than an .xyz/.w pair is what keeps the alpha in
    // its native slot: a bare `output.specular.w = 1.0` sinks past the o0 block.
    output.specular = float4(splitShadow.xxx, 1.0) + float4(ambientSpecular, 0.0);
    output.diffuse = float4(ambientDiffuse, 1.0) + float4(splitShadow.xxx, shadowBlend);
#endif
    return output;
}

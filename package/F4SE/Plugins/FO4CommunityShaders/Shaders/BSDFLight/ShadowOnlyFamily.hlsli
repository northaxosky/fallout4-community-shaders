// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 BSDFLightShader deferred PS, native DIRECTIONAL + SHADOW_ONLY family:
// the FILTER_* macro axis.
//
// Native macros select DIRECTIONAL, POINTOMNI, SPOT, or POINTSPOT.
// SHADOW_ONLY narrows DIRECTIONAL to a permutation that writes only shadow.
//
// Reconstructed from the six archive blobs that form a controlled minimal pair
// set - DIRECTIONAL + DIRSPLITS=1 + RGBSPEC + SHADOW + SHADOW_ONLY + SPECULAR,
// differing only in the filter macro:
//   FILTER (none)      sha1 964e9b82cde7aecea38bc999d1c925127da0bc01, 2328 B
//   FILTER_PCF1        sha1 0bff5e0ecdf732495be001a37160d8aa11a9660c, 2304 B
//   FILTER_PCF9        sha1 ba94fedf2ed412c7dd0f770a990f735c92768b44, 2720 B
//   FILTER_PCSS        sha1 53e2fcf10e89547e9d02969173839a2768b22e22, 4068 B
//   FILTER_POISSON     sha1 99a112e7bc7fc4fbcefac716864d6e6a9cdcac68, 19016 B
//   FILTER_PCSSPOISSON sha1 e5ef2e946298f7eea6702473dbeef4202c7ca821, 20016 B
//
// Each source permutation reproduces the archive blob's SHEX chunk.
//
// The whole family shares one prologue (depth sample, Far/Near reproject
// select, view-space reconstruct, g-buffer decode, slope bias) and one epilogue
// (distance fade, two MRT writes). Everything between the light-space
// projection and the fade is the filter axis, and it moves four things at once:
// the resource set, the sampler modes, the constant-buffer indexing mode and
// the reference-depth expression. Those are recorded per filter below.

#if !defined(DIRECTIONAL) || !defined(SHADOW_ONLY)
#  error "this source is the native DIRECTIONAL + SHADOW_ONLY family; define both"
#endif
#ifndef SHADOW
#  error "every native SHADOW_ONLY blob carries SHADOW=1"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 1
#  error "the reconstructed SHADOW_ONLY family is DIRSPLITS=1 only"
#endif
#if defined(AMBIENT) || defined(BLENDSPLIT)
#  error "no DIRSPLITS=1 SHADOW_ONLY blob with a full FILTER_* set carries AMBIENT or BLENDSPLIT"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_POISSON) + defined(FILTER_PCSSPOISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif

// Shared CB12[0..27] per-frame schema (single source of truth across the
// deferred-pipeline PS reconstructions).
#include "../Common/DeferredContracts.hlsli"

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  include "../Common/ShadowPoissonKernel.hlsli"
#endif

// A raw (mode_default) shadow read at t4/s4 appears in exactly the three
// permutations that must recover a stored depth rather than a comparison
// result: the unfiltered lookup and the two blocker searches.
#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_SHADOW_RAW_TAP 1
#endif

// A comparison (mode_comparison) shadow read at t5/s5 appears in every
// permutation except the unfiltered one.
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  define FO4_SHADOW_CMP_TAP 1
#endif

// Only the three permutations that need the cascade near/far plane pair index
// CB2 by the runtime slice, which is what promotes the declaration from
// immediateIndexed to dynamicIndexed.
#if defined(FILTER_PCSS) || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  define FO4_SHADOW_DYNAMIC_CB2 1
#endif

// Constant buffer layouts (native directional shadow-only).

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block. Every blob in this family declares
    //          CB12[28] and reads only the Far/Near reproject rows.
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: constant ID 0 `VPOSOffset`. .xy and .zw scale SV_POSITION into the
    //      sampling UV and the NDC pair, as in every other deferred PS here.
    float4 ScreenSize;

    // [1..8]: unread by this family. The light vector, colour, attenuation and
    //         split-distance constants that the full-lighting directional path
    //         allocates have no reader once SHADOW_ONLY drops the BRDF.
    float4 cb2_pad_1_8[8];

    // [9]: .y is the cascade slice index. It reaches the shader twice: as the
    //      Texture2DArray slice, and (in the dynamically indexed filters) as
    //      the `ftou` index into the cascade near/far table below. .x is never
    //      read. This is the only float that both addresses an array slice and
    //      indexes a constant buffer, so it must stay one value.
    float4 cb2_idx9_cascade_slice;

    // [10]: unread here. `FadeDistances` drives the cascade blend, and a
    //       DIRSPLITS=1 blob has nothing to blend.
    float4 cb2_pad_10;

    // [11..13]: constant ID 9 `ShadowMapProj`, three rows. dp4 against
    //           (posView, 1) yields the light-space (x, y, z). Only three rows
    //           are read: an orthographic cascade needs no w.
    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;

    // [14..19]: unread. The second cascade's rows live here in the multi-split
    //           families; DIRSPLITS=1 leaves the registers allocated and dead.
    float4 cb2_pad_14_19[6];

    // [20]: constant ID 10 `ShadowSampleParam`. Only FILTER_PCF9 reads it, and
    //       only .zw - the inverse shadow-map dimensions used as the 3x3 tap
    //       step. .xy carry bias terms this family never reads.
    float4 cb2_idx20_shadow_sample_param;

    // [21..23]: constant ID 11 `ShadowWorldScale`, one register per cascade,
    //           indexed by the runtime slice. Each is
    //           (right-left, top-bottom, near, far) from the cascade frustum.
    //           PCSS and PCSSPOISSON read .xy as the blocker-search step
    //           reciprocal and .zw as the world depth range; POISSON reads
    //           only .zw. The array shape is what the native
    //           `cb2[r + 21]` addressing requires.
    float4 cb2_idx21_cascade_world_scale[3];

    // [24]: constant ID 12 `ShadowFadeParam`. .x is the squared world distance
    //       at which the shadow term fades to unshadowed.
    float4 cb2_idx24_distance_fade;
};

// Resource bindings (native directional shadow-only).
// No t0: SHADOW_ONLY never samples albedo, so the slot is not declared.

// t1: RT27 kTAAAccumulationSwap, octahedral 2-channel normal. Only .xy read.
Texture2D<float4> g_tGbufferNormal : register(t1);

// t2: RT30 unnamed G-buffer auxiliary. Only .w read, as the material code.
Texture2D<float4> g_tGbufferMaterial : register(t2);

// t3: main depth, sampled with explicit gradients.
Texture2D<float4> g_tMainDepth : register(t3);

#ifdef FO4_SHADOW_RAW_TAP
// t4/s4: cascade shadow atlas bound to a plain sampler. The stored depth is
//        needed as a value, not as a comparison result: the unfiltered
//        permutation compares it in the shader, and the two PCSS blocker
//        searches average it.
Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
SamplerState g_sCascadeShadowRaw : register(s4);  // mode_default
#endif

#ifdef FO4_SHADOW_CMP_TAP
// t5/s5: the same atlas bound for hardware comparison PCF. PCSS and
//        PCSSPOISSON declare both bindings and use each for its own stage.
Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);  // mode_comparison
#endif

SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

// Helpers (native directional shadow-only).

// Slope-scaled depth bias for the material-1 branch. The native code inlines
// the Abramowitz & Stegun 4.4.45 acos approximation, feeds it to sincos,
// divides sine by cosine and scales by 0.08 - that is 0.08 * tan(acos(c)). `c`
// is the unnegated octahedral z term clamped at zero, so the bias grows as the
// surface turns away from the light. Written as the native instruction chain
// rather than as `0.08 * tan(acos(c))` because the polynomial is the shipped
// approximation, not an identity; its coefficients are the literal float32
// values in the archive bytecode, which the disassembler rounds to six places.
float ComputeSlopeBias(float c)
{
    float sqrtTerm = sqrt(1.0 - c);
    float acosApprox = ((-0.0187293 * c + 0.0742610) * c - 0.2121144) * c + 1.5707288;
    acosApprox *= sqrtTerm;
    float sinA, cosA;
    sincos(acosApprox, sinA, cosA);
    return (sinA / cosA) * 0.08;
}

// Entry point (native directional shadow-only).

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

    float  materialCode = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv4.xy).w;
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv4.xy).xy * 4.0 - 2.0;

    // The decoded normal is never reconstructed here. SHADOW_ONLY consumes only
    // the z term of the octahedral decode, unnegated and clamped at zero, as
    // the slope-bias cosine.
    float encodedZ = 1.0 - dot(normalEnc, normalEnc) * 0.5;
    bool  isMaterial1 = abs(materialCode * 255.0 - 1.0) < 0.25;
    float slopeBias = ComputeSlopeBias(max(encodedZ, 0.0));

#ifdef FILTER_POISSON
    // Native emits this select ahead of the light-space projection, and stores
    // the bias unnegated. Every other filter negates inside the select and
    // schedules it after the projection.
    float poissonBias = isMaterial1 ? slopeBias : 0.275;
#endif

    // Light-space projection, shared by every filter.
    float4 posViewH1 = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_shadowproj_row0, posViewH1);
    shadowUV.y = dot(cb2_shadowproj_row1, posViewH1);
    float shadowZ = min(dot(cb2_shadowproj_row2, posViewH1), 0.999999);
    float slice = cb2_idx9_cascade_slice.y;

#ifdef FILTER_POISSON
    // Native computes the kernel scale between the projection and the cascade
    // index fetch, so it is hoisted here rather than kept in the branch.
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif

#ifdef FO4_SHADOW_DYNAMIC_CB2
    uint   cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
#endif

    float shadow;

#if defined(FILTER_PCF1)
    // One hardware comparison tap. The 64-instruction blob is the FILTER-less
    // one minus the shader-side compare: the sampler does the compare instead.
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    shadow = g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), zRef);

#elif defined(FILTER_PCF9)
    // 3x3 box of comparison taps, one texel apart, averaged. The native listing
    // shows a single sample_c_lz because it is the body of a rolled 3x3 loop
    // pair, not one executed tap.
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
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
                g_sCascadeShadowCmp, float3(tapUV, slice), zRef);
        }
    }
    shadow = sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)
    // Percentage-closer soft shadows: a 5x5 raw blocker search sets a penumbra
    // width, then a 5x5 comparison filter uses it.
    float2 searchStep = 1.0 / cascadeScale.xy;
    float  zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);

    // .x accumulates blocker depth, .y counts blockers. Native keeps them in
    // one register pair and commits both with a single conditional move.
    // The blocker loop offsets are `float(i - 2)` and the filter loop's are
    // `float(i) - 2.0`; native converts on either side of the subtract in the
    // two loops, so the asymmetry is deliberate.
    float2 blocker = float2(0.0, 0.0);
    [loop]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [loop]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = float2(offsetX, offsetY) * searchStep + shadowUV;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < zRef;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y != 0.0)
    {
        // The centre tap is read raw and compared in the shader, then seeds the
        // accumulator that the 25 comparison taps add to. The divisor stays
        // 1/25, so the centre tap is an extra term rather than a 26th sample.
        float centreDepth = g_tCascadeShadowRaw.Sample(
            g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
        float centreLit = (centreDepth >= zRef) ? 1.0 : 0.0;

        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * zRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
            ? 1.9
            : (separation * 1.8 + 0.1);

        float sum = centreLit;
        [loop]
        for (int fi = 0; fi < 5; ++fi)
        {
            float offsetX = penumbra * (float(fi) - 2.0);
            [loop]
            for (int fj = 0; fj < 5; ++fj)
            {
                float offsetY = penumbra * (float(fj) - 2.0);
                float2 tapUV = (searchStep * float2(offsetX, offsetY)) * 0.5
                    + shadowUV;
                sum = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                    g_sCascadeShadowCmp, float3(tapUV, slice), zRef);
            }
        }
        shadow = sum * 0.04;
    }
    else
    {
        shadow = 1.0;
    }

#elif defined(FILTER_POISSON)
    // Eight loop iterations, two taps each, over the leading 16 entries of the
    // 1000-entry immediate kernel. This is the only permutation whose bias is
    // stored unnegated and then scaled by the reciprocal world depth range;
    // every other filter adds the bias to the projected z directly.
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - poissonBias * rcpWorldRange;

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

#elif defined(FILTER_PCSSPOISSON)
    // The PCSS blocker search driving the Poisson kernel instead of a 5x5 box.
    // Two details are native behaviour and not transcription slips: the
    // penumbra scales by the .x reciprocal alone, and the filter reference
    // depth is the blocker reference depth with the bias added a second time.
    float2 searchStep = 1.0 / cascadeScale.xy;
    float  bias = isMaterial1 ? -slopeBias : -0.275;
    float  blockerRef = shadowZ + bias;

    float2 blocker = float2(0.0, 0.0);
    [loop]
    for (int bi = 0; bi < 5; ++bi)
    {
        float offsetX = float(bi - 2);
        [loop]
        for (int bj = 0; bj < 5; ++bj)
        {
            float offsetY = float(bj - 2);
            float2 tapUV = float2(offsetX, offsetY) * searchStep + shadowUV;
            float  tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            bool   isBlocker = tapDepth < blockerRef;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y != 0.0)
    {
        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * blockerRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
            ? 1.9
            : (separation * 1.8 + 0.1);
        float kernelScale = penumbra * searchStep.x;
        float zRef = blockerRef + bias;

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
    }
    else
    {
        shadow = 1.0;
    }

#else
    // No filter macro: one raw tap and a shader-side depth compare.
    float sampledDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    shadow = (sampledDepth >= zRef) ? 1.0 : 0.0;
#endif

    // Distance fade toward unshadowed, identical to the full-lighting path:
    // D = saturate(|posView|^2 / cb2[24].x), fade = 1 - D^8.
    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2 = distNorm * distNorm;
    float dist4 = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;

    float3 result = fadeFactor * (shadow - 1.0) + 1.0;

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
    return output;
}

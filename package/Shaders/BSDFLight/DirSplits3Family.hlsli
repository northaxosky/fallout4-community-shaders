// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 BSDFLightShader deferred PS, native DIRECTIONAL full-BRDF family at
// DIRSPLITS=3: the FILTER_* macro axis over three shadow cascades.
//
// Scope is exactly DIRSPLITS=3. The three-cascade layout is not a superset of
// the two-cascade one: nine projection rows fill CB2[11..19] where DIRSPLITS=2
// leaves [17..19] as holes, and the world-scale block is three vectors at
// CB2[21..23] where DIRSPLITS=2 has two and a hole. A wrong split count is
// therefore a different constant buffer, and the guards below reject it.
//
// Reconstructed from the 27 archive blobs carrying
// DIRECTIONAL + DIRSPLITS=3 + RGBSPEC + SHADOW + SPECULAR, which span four
// independent additive axes - AMBIENT, BLENDSPLIT, IGNOREROUGHNESS and
// FILTER_*. One representative per filter branch:
//   FILTER (none)      sha1 5681c96c44b1f0f6d64de2b374ce59e5542795c4,  7568 B
//   FILTER_PCF1        sha1 815117736d04057d09c827a9663d0298ab9ed113,  6988 B
//   FILTER_PCF9        sha1 67dd9b9c145fc4297c316c1da8536bcd2c87146a,  8320 B
//   FILTER_PCSS        sha1 d32da64922f862a7916957551f343c640ac59652, 27180 B
//   FILTER_POISSON     sha1 423798e1b7099cba22ff0588ec80fb81338c9679, 25140 B
//   FILTER_PCSSPOISSON sha1 98d5291ab3059f9e6427ee6f5b8d11331f7017ad, 43620 B
//
// FILTER_PCSSPOISSON is the branch DIRSPLITS=2 does not have. It is a PCSS
// blocker search feeding a Poisson comparison kernel, and it is the reason this
// family needs its own filter body rather than a third cascade bolted onto the
// DIRSPLITS=2 one.
//
// IGNOREROUGHNESS is reconstructed here. The controlled AMBIENT-free pair in
// `UnshadowedFamily.hlsli` localises the roughness-driven
// visibility geometry and rim deletions. Shadowed AMBIENT disassembly adds one
// more delta: its roughness-dependent exponent becomes an exact fixed square.
// Across the six IGNOREROUGHNESS blobs, cb2[1] moves 5 -> 3 and cb2[2] moves
// 6 -> 5 against their twins, matching the removed geometry and rim reads;
// AMBIENT variants retain the gradient and material.y paths without reading
// matSample.x for that exponent.

#if !defined(DIRECTIONAL)
#  error "this source is the native DIRECTIONAL family; define DIRECTIONAL"
#endif
#if defined(POINTOMNI) || defined(POINTSPOT) || defined(SPOT) || defined(HALFOMNI)
#  error "DIRECTIONAL is exclusive with the punctual light families; those live in DeferredFamily.hlsli"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the DIRSPLITS=1 family in ShadowOnlyFamily.hlsli"
#endif
#ifndef SHADOW
#  error "the reconstructed DIRSPLITS=3 family is SHADOW only; every blob in it declares the cascade block"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the split count is a native axis and is never assumed"
#endif
#if DIRSPLITS != 3
#  error "this source reconstructs DIRSPLITS=3 only; DIRSPLITS=1 and DIRSPLITS=2 are separate native families with a different CB2 layout"
#endif
#if !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native DIRSPLITS=3 SHADOW blob carries both SPECULAR and RGBSPEC"
#endif
#ifdef IGNORERIM
#  error "no DIRSPLITS=3 blob carries IGNORERIM; the archive puts it on POINTOMNI and SPOT only"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_PCSSPOISSON) + defined(FILTER_POISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif

// Shared CB12[0..27] per-frame schema (single source of truth across the
// deferred-pipeline PS reconstructions).
#include "../Common/DeferredContracts.hlsli"

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  include "../Common/ShadowPoissonKernel.hlsli"
#endif

// Internal selectors. These name what the native declarations move together;
// they are not macros the engine defines, and nothing outside this file sets
// them.

// A raw (mode_default) read at t4/s4 recovers a stored depth rather than a
// comparison result. Native declares it in exactly three branches: the
// unfiltered lookup and the two blocker searches.
#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_DS3_SHADOW_RAW_TAP 1
#endif

// A comparison (mode_comparison) read at t5/s5 appears in every branch except
// the unfiltered one.
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON)
#  define FO4_DS3_SHADOW_CMP_TAP 1
#endif

// CB2[21..23] `ShadowWorldScale` is read only where a cascade's world-space
// near/far pair is needed: the two penumbra remaps and the Poisson depth bias.
#if defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON)
#  define FO4_DS3_USES_WORLD_SCALE 1
#endif

// CB2[20] `ShadowSampleParam` is read only by the two kernels that step the tap
// pattern across the shadow map. FILTER_PCSSPOISSON is not one of them: its
// kernel radius comes from the penumbra estimate and the texel step, so it
// never reads this register.
#if defined(FILTER_PCF9) || defined(FILTER_POISSON)
#  define FO4_DS3_USES_SAMPLE_PARAM 1
#endif

// Constant buffer layouts.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block. See `Common/DeferredContracts.hlsli`.
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    // [28]: hair specular parameters (fHairPrimSpecScale, fHairPrimSpecPow,
    //       fHairSecSpecScale, fHairSecSpecPow). Not subsurface scattering.
    float4 cb12_idx28_hair_spec_params;

    // [29]: hair specular tangent shifts (fHairPrimSpecShift,
    //       fHairSecSpecShift, 0, 0).
    float4 cb12_idx29_hair_spec_shifts;

    // [30]: .y is the 1 - x raised-to-fourth gloss term in the Schlick-shaped
    //       fresnel scale. Every blob in this family declares CB12[31].
    float4 cb12_idx30;
};

cbuffer PerCall_CB2 : register(b2)
{
    // [0]: constant ID 0 `VPOSOffset`. .xy scales SV_POSITION into the sampling
    //      UV, .zw into the NDC pair.
    float4 ScreenSize;

    // [1]: constant ID 1 `LightVector`. .xyz is the sun direction in view
    //      space.
    float4 SunDirection;

    // [2]: constant ID 2 `LightColor`. .xyz in the BSDFLightShader HDR-scale
    //      convention.
    float4 SunColor_HDR;

#ifdef AMBIENT
    // [3..5]: constant IDs 3 `LightAttenuation`, 4 `ProjectedLightVector` and
    //         5 `SpotData` are absent from every blob in this family - not
    //         merely unread, they have no allocated register.
    float4 cb2_pad_3_5[3];

    // [6..8]: constant ID 6 `DirectionalAmbient`, the ambient gradient rows
    //         for the diffuse and specular image-based terms.
    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#else
    // [3..8]: unallocated in this permutation. `DirectionalAmbient` is
    //         allocated only under AMBIENT.
    float4 cb2_pad_3_8[6];
#endif

    // [9]: constant ID 7 `SplitDistances`. .w is the single outer gate over the
    //      whole cascade block: beyond it the pixel is unshadowed and the
    //      distance fade is not evaluated either. The register is unread under
    //      BLENDSPLIT, where the cross-fade replaces the gate.
    float4 cb2_idx9_split_distances;

    // [10]: constant ID 8 `FadeDistances`. All four components are live at
    //       three splits: .x/.y are the cascade-0 to cascade-1 transition pair
    //       and .z/.w the cascade-1 to cascade-2 one. Values are projected
    //       depth, compared against the same linearised depth the view-space
    //       reconstruct consumes.
    float4 cb2_idx10_fade_distances;

    // [11..19]: constant ID 9 `ShadowMapProj`, nine vectors read as three
    //           3-row view-space-to-light-space transforms, one per cascade.
    //           The register count is 3 * DIRSPLITS, which is what makes this
    //           layout specific to DIRSPLITS=3 - the two-cascade family leaves
    //           [17..19] unallocated.
    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;
    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;
    float4 cb2_cascade2_row0;
    float4 cb2_cascade2_row1;
    float4 cb2_cascade2_row2;

    // [20]: constant ID 10 `ShadowSampleParam`. .zw are inverse shadow-map
    //       dimensions, .z doubles as the Poisson kernel scale.
    float4 cb2_idx20_shadow_sample_param;

    // [21..23]: constant ID 11 `ShadowWorldScale`, one vector per cascade,
    //           each (right-left, top-bottom, near, far) from that cascade's
    //           NiFrustum in world units. The blocker searches read .xy as a
    //           texel step and .zw as the depth remap; Poisson reads only .zw.
    //           The register count is DIRSPLITS.
    float4 cb2_idx21_cascade0_world_scale;
    float4 cb2_idx22_cascade1_world_scale;
    float4 cb2_idx23_cascade2_world_scale;

    // [24]: constant ID 12 `ShadowFadeParam`. .x is a squared world distance
    //       limit; reading it is what forces the declared CB2 size to 25.
    float4 cb2_idx24_distance_fade;
};

// Resource bindings. Slot indices match the archive declarations exactly, and
// the raw/comparison split is the whole reason the filter axis moves the ABI.

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

#ifdef FO4_DS3_SHADOW_RAW_TAP
// t4/s4: cascade shadow atlas read as stored depth, sampler mode_default.
Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
#endif

#ifdef FO4_DS3_SHADOW_CMP_TAP
// t5/s5: cascade shadow atlas read through the comparison sampler.
Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
#endif

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

#ifdef FO4_DS3_SHADOW_RAW_TAP
SamplerState g_sCascadeShadowRaw : register(s4);
#endif

#ifdef FO4_DS3_SHADOW_CMP_TAP
SamplerComparisonState g_sCascadeShadowCmp : register(s5);
#endif

// Helpers.

static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.141593;

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    return float3(enc * sqrt(recon), z);
}

#ifdef AMBIENT
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

// One cascade's shadow factor. Native unrolls all three cascades into separate
// branches with literal constant-buffer registers and a literal array slice, so
// the rows, the slice and the world-scale vector arrive as arguments rather
// than through a runtime index. That is what keeps CB2 immediateIndexed here:
// the SHADOW_ONLY family indexes CB2 by the runtime cascade and is declared
// dynamicIndexed instead.
//
// The reference depth is the raw projected z. This family applies no
// slope-scaled bias and no 0.999999 clamp; the only depth offset is Poisson's,
// below.
float ComputeCascadeShadow(float3 posView,
                           float4 row0, float4 row1, float4 row2,
                           float slice
#ifdef FO4_DS3_USES_WORLD_SCALE
                           , float4 cascadeScale
#endif
#ifdef FILTER_POISSON
                           , float poissonBiasScale
#endif
                           )
{
    float4 posViewH = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(row0, posViewH);
    shadowUV.y = dot(row1, posViewH);
    float shadowZ = dot(row2, posViewH);

#if defined(FILTER_PCF1)
    // One hardware comparison tap.
    return g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)
    // Compact 3x3 box of comparison taps one shadow texel apart, averaged.
    // Native keeps this as a rolled loop pair, so a single sample_c_lz in the
    // listing is nine executed taps.
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
    return sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)
    // Percentage-closer soft shadows. A 5x5 raw blocker search over t4/s4 sets
    // a penumbra width, then a 5x5 comparison filter over t5/s5 uses it. This
    // is one of the two branches that need both resources.
    float2 searchStep = 1.0 / cascadeScale.xy;

    // .x accumulates blocker depth, .y counts blockers; native commits both
    // with one conditional move. The blocker loop offsets convert on the far
    // side of the subtract and the filter loop's on the near side; the
    // asymmetry is native and is kept.
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
            bool   isBlocker = tapDepth < shadowZ;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
    {
        return 1.0;
    }

    // The centre tap is read raw and compared in the shader, then seeds the
    // accumulator the 25 comparison taps add to. The divisor stays 1/25, so it
    // is an extra term rather than a 26th sample.
    float centreDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float centreLit = (centreDepth >= shadowZ) ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
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
            float2 tapUV = (searchStep * float2(offsetX, offsetY)) * 0.5 + shadowUV;
            sum = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * 0.04;

#elif defined(FILTER_PCSSPOISSON)
    // The DIRSPLITS=3 filter with no DIRSPLITS=2 counterpart: the PCSS blocker
    // search sizes a penumbra, and the 16 Poisson taps spend it. It is the same
    // 5x5 raw search over t4/s4 and the same world-range remap as PCSS, so it
    // reads this cascade's world scale the same six times.
    float2 searchStep = 1.0 / cascadeScale.xy;

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
            bool   isBlocker = tapDepth < shadowZ;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
    {
        return 1.0;
    }

    float averageBlocker = blocker.x / blocker.y;
    float worldRange     = cascadeScale.w - cascadeScale.z;
    float receiverWorld  = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld   = worldRange * averageBlocker + cascadeScale.z;
    float separation     = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra       = (blockerWorld < cascadeScale.z + 0.001)
        ? 1.9
        : (separation * 1.8 + 0.1);

    // The penumbra scales the texel step directly, so this branch never reads
    // CB2[20], and there is no centre-lit seed and no depth bias to go with it.
    float kernelScale = penumbra * searchStep.x;

    float sum = 0.0;
    [loop]
    for (int k = 0; k < 8; ++k)
    {
        float2 tap0 = (SHADOW_POISSON_KERNEL[k * 2] - 0.5) * kernelScale;
        float2 tap1 = (SHADOW_POISSON_KERNEL[k * 2 + 1] - 0.5) * kernelScale;
        float4 tapUV = float4(tap0, tap1) * 2.0 + shadowUV.xyxy;
        float partial = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.xy, slice), shadowZ);
        sum = partial + g_tCascadeShadowCmp.SampleCmpLevelZero(
            g_sCascadeShadowCmp, float3(tapUV.zw, slice), shadowZ);
    }
    return sum * 0.0625;

#elif defined(FILTER_POISSON)
    // Eight loop iterations, two taps each, over the leading 16 entries of the
    // 1000-entry immediate kernel. The kernel and the tap count do not change
    // with the split count; what changes is that the radius and the bias come
    // from this cascade's world scale.
    //
    // This is the only branch with a depth bias, and it is per-cascade: native
    // scales the reciprocal world range by 0.275 in cascade 0 and takes it
    // whole in cascades 1 and 2. The caller passes that scale.
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - rcpWorldRange * poissonBiasScale;
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;

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
    return sum * 0.0625;

#else
    // No filter macro: a single raw fetch and a shader-side compare. The
    // sampler is mode_default here, so the comparison is not done by hardware.
    float tapDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    return (tapDepth >= shadowZ) ? 1.0 : 0.0;
#endif
}

// Cascade selection, cross-fade and distance fade for one pixel.
//
// Without BLENDSPLIT, SplitDistances.w is a single outer gate: native evaluates
// no cascade and no distance fade past it, which is why cb2[24] survives as one
// read inside the branch rather than an unconditional one.
float ComputeDirectionalShadow(float3 posView, float linearizedDepth)
{
#ifndef BLENDSPLIT
    if (!(linearizedDepth < cb2_idx9_split_distances.w))
    {
        return 1.0;
    }
#endif

    // Every cascade body runs under its own activation test. The three tests
    // are expressed only as (depth < FadeDistances.c) or the reverse so the
    // whole set packs into the two vector compares native emits, and the
    // regions each cascade owns alone need no further comparison.
    bool cascade0Active = (linearizedDepth < cb2_idx10_fade_distances.y);
    bool cascade1Active = (cb2_idx10_fade_distances.x < linearizedDepth)
        && (linearizedDepth < cb2_idx10_fade_distances.w);
    bool cascade2Active = (cb2_idx10_fade_distances.z < linearizedDepth);

    float cascade0Shadow = 1.0;
    if (cascade0Active)
    {
        cascade0Shadow = ComputeCascadeShadow(
            posView, cb2_cascade0_row0, cb2_cascade0_row1, cb2_cascade0_row2,
            0.0
#ifdef FO4_DS3_USES_WORLD_SCALE
            , cb2_idx21_cascade0_world_scale
#endif
#ifdef FILTER_POISSON
            , 0.275
#endif
            );
    }

    float cascade1Shadow = 1.0;
    if (cascade1Active)
    {
        cascade1Shadow = ComputeCascadeShadow(
            posView, cb2_cascade1_row0, cb2_cascade1_row1, cb2_cascade1_row2,
            1.0
#ifdef FO4_DS3_USES_WORLD_SCALE
            , cb2_idx22_cascade1_world_scale
#endif
#ifdef FILTER_POISSON
            , 1.0
#endif
            );
    }

    float cascade2Shadow = 1.0;
    if (cascade2Active)
    {
        cascade2Shadow = ComputeCascadeShadow(
            posView, cb2_cascade2_row0, cb2_cascade2_row1, cb2_cascade2_row2,
            2.0
#ifdef FO4_DS3_USES_WORLD_SCALE
            , cb2_idx23_cascade2_world_scale
#endif
#ifdef FILTER_POISSON
            , 1.0
#endif
            );
    }

#ifdef BLENDSPLIT
    // Two smooth transitions over the two overlap bands, .x -> .y and
    // .z -> .w. Each cross-fade saturates outside its own band, so the lower
    // one already resolves to cascade 0 below .x and to cascade 1 above .y, and
    // one select between them is the whole partition: s0, blend01, s1, blend12,
    // s2.
    float range01 = cb2_idx10_fade_distances.y - cb2_idx10_fade_distances.x;
    float range12 = cb2_idx10_fade_distances.w - cb2_idx10_fade_distances.z;
    float t01 = saturate((linearizedDepth - cb2_idx10_fade_distances.x) / range01);
    float t12 = saturate((linearizedDepth - cb2_idx10_fade_distances.z) / range12);
    float w01 = min(t01 * t01 * (3.0 - 2.0 * t01), 1.0);
    float w12 = min(t12 * t12 * (3.0 - 2.0 * t12), 1.0);

    float lowerShadow = lerp(cascade0Shadow, cascade1Shadow, w01);
    float upperShadow = lerp(cascade1Shadow, cascade2Shadow, w12);
    float shadow = cascade2Active ? upperShadow : lowerShadow;
#else
    // Without BLENDSPLIT the lower overlap band resolves to unshadowed, exactly
    // as it does at two splits; the upper one is owned by cascade 2. Both
    // remaining boundaries reuse the activation tests rather than comparing
    // again.
    float shadow = cascade2Active ? cascade2Shadow : cascade1Shadow;
    shadow = cascade0Active ? 1.0 : shadow;
    shadow = (cb2_idx10_fade_distances.x < linearizedDepth) ? shadow : cascade0Shadow;
#endif

    // Distance fade toward unshadowed at the far range: fade = 1 - D^8.
    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    return fadeFactor * (shadow - 1.0) + 1.0;
}

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
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    // Near select is inclusive at exactly 0.01.
    bool   isNearPath = (depth <= 0.01);
    float  linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    float2 uvNDC = uv4.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float4 albedoSample = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

#ifndef IGNOREROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif
    float posViewLenSq  = dot(-posView, -posView);
    float posViewLen    = rsqrt(posViewLenSq);
    float3 viewDirNeg   = -posView * posViewLen;

    float shadow = ComputeDirectionalShadow(posView, linearizedDepth);

    // Sun-direction lighting setup.
    float3 albedoPremult  = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw      = dot(normalView, SunDirection.xyz);
    float  NdotL_pos      = max(NdotL_raw, 0.0);
    float  NdotL_clamped  = min(NdotL_pos, 1.0);
    float  oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    // Material-id-branched BRDF. Both branches produce a specular contribution
    // and a diffuse mix term.
    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        // Two-lobe anisotropic hair specular driven by the engine's
        // fHairPrimSpec* / fHairSecSpec* values. It dots the t2 sample's xyz,
        // a distinct normal in that g-buffer slot, not the octahedral normal.
        // IGNOREROUGHNESS does not touch this branch.
        float skinNdotL  = dot(matSample.xyz, SunDirection.xyz);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoSample.w, hairIntensity);

        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2     = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular  = NdotL_clamped * (pow2 * SunColor_HDR.xyz);
        brdfModulator = 0.0;
    }
    else
    {
        float depthScale     = matSample.z * 100.0;
        float specExpBase    = exp2(matSample.x * 10.0 + 1.0);
        float specExpScale   = 1.0 - schlickFres * 0.98;
        float specExp        = specExpScale * specExpBase;

        float NdotV_raw = dot(viewDirNeg, normalView);
#ifdef AMBIENT
        float3 reflectionDir = 2.0 * NdotV_raw * normalView - viewDirNeg;
        float  oneMinusNdotV = 1.0 - saturate(NdotV_raw);
#ifdef IGNOREROUGHNESS
        float  ambientSpecularFactor =
            oneMinusNdotV * oneMinusNdotV * 0.25;
#else
        float  ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
#endif
        ambientSpecular = matSample.y * ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir);
#endif

#ifdef IGNOREROUGHNESS
        // The roughness-driven visibility geometry is the whole of what
        // IGNOREROUGHNESS removes from this branch; the diffuse mix collapses
        // to the unmodified clamped N.L. The deleted `tangentL` is one of the
        // two cb2[1] reads the macro costs.
        brdfShadowMix = NdotL_pos;
#else
        float3 tangentV  = viewDirNeg - normalView * NdotV_raw;
        float3 tangentL  = SunDirection.xyz - normalView * NdotL_raw;
        float  tangentVL = max(dot(tangentV, tangentL), 0.0);

        float roughSq = roughness01 * roughness01;
        float visA    = roughSq / (roughSq + 0.57);
        float visB    = roughSq / (roughSq + 0.09);
        visB *= 0.45;
        visA = 1.0 - 0.5 * visA;

        float tangentDenom = max(NdotL_raw, NdotV_raw);
        float tangentSin   = sqrt(saturate((1.0 - NdotV_raw * NdotV_raw)
                                           * (1.0 - NdotL_raw * NdotL_raw)));
        float visibilityGeom = tangentVL * visB;
        visibilityGeom = visibilityGeom * (tangentSin / tangentDenom) + visA;
        brdfShadowMix  = NdotL_pos * visibilityGeom;
#endif

        float3 halfVec = SunDirection.xyz - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.159155;
        float distribution     = exp2(log2(NdotH) * specExp);
        distributionNorm *= distribution;

        float VdotH_nonneg = max(VdotH, 0.0);
        float minN         = min(NdotL_clamped, NdotV_sat);
        float twoNdotH     = NdotH + NdotH;
        bool  usePeakRatio = (VdotH_nonneg >= twoNdotH * minN);
        bool  useUnityRatio = (NdotV_sat == minN);
        float ratioNLNV    = NdotL_clamped / NdotV_sat;
        float ratio        = useUnityRatio ? 1.0 : ratioNLNV;
        float visibility   = (twoNdotH * ratio) / VdotH_nonneg;
        float fallbackVisibility = 1.0 / NdotV_sat;
        visibility = usePeakRatio ? visibility : fallbackVisibility;

        float oneMinusVdotH  = 1.0 - VdotH;
        float oneMinusVdotH2 = oneMinusVdotH * oneMinusVdotH;
        float oneMinusVdotH4 = oneMinusVdotH2 * oneMinusVdotH2;
        float oneMinusVdotH5 = oneMinusVdotH * oneMinusVdotH4;
        float fresnelTerm    = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);

        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= FO4_DIRECTIONAL_SPECULAR_SCALE;

        brdfSpecular  = NdotL_clamped * (specMag * SunColor_HDR.xyz);
        brdfModulator = depthScale;
    }

    // Final composition.
    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;

#ifndef IGNOREROUGHNESS
    // Rim / backscatter tail, scaled by the smoothness. IGNOREROUGHNESS deletes
    // that smoothness and with it this whole block - the second cb2[1] read and
    // the one cb2[2] read the macro costs.
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = exp2(log2(1.0 - NdotV_view) * 0.01);
    float fresEdge    = saturate(dot(viewDirNeg, -SunDirection.xyz));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;

    finalDiffuse += SunColor_HDR.xyz * ambientTerm;
#endif

    float backfaceWrap = saturate(-NdotL_raw);
    finalDiffuse += SunColor_HDR.xyz * (albedoPremult * backfaceWrap);

    float forwardBlend = saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * SunColor_HDR.xyz) * albedoSample.xyz;

    float specMix = 1.0 - schlickFres * 0.5;
    output.specular.xyz = shadow * specMix * brdfSpecular;
#ifdef AMBIENT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w = 1.0;

    output.diffuse.xyz = shadow * finalDiffuse;
#ifdef AMBIENT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w = 0.0;

    return output;
}

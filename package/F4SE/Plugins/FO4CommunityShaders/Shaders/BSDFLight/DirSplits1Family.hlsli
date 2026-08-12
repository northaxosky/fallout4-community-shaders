// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// FO4 BSDFLightShader deferred PS, native DIRECTIONAL full-BRDF family at
// DIRSPLITS=1: the comparison FILTER_* macro axis over a single shadow cascade,
// crossed with AMBIENT.
//
// Sibling of DirSplits2Family.hlsli / DirSplits3Family.hlsli, and the full-BRDF
// counterpart of ShadowOnlyFamily.hlsli. The split count
// moves the CB2 layout, so it gets its own source rather than a runtime switch:
// DIRSPLITS=1 has three projection rows at CB2[11..13] and no FadeDistances
// read at all, where DIRSPLITS=2 has six rows plus CB2[10].
//
// Reconstructs the eight archive blobs carrying
// DIRECTIONAL + SHADOW + SPECULAR + RGBSPEC + DIRSPLITS=1 with a comparison
// filter, crossed with AMBIENT.
//   FILTER_PCF1              sha1 9fc11553c6068eaccff5a603cf038a9b4cc65546,  6260 B
//   FILTER_PCF1    + AMBIENT sha1 cf3f9141478b244932e875909255d1ebde3373e5,  6984 B
//   FILTER_PCF9              sha1 aa721295cd3b1ff82646b52dded82d88566224cd,  6676 B
//   FILTER_PCF9    + AMBIENT sha1 b732fcfa4b24e58f1876af206d794876d1cb962a,  7408 B
//   FILTER_PCSS              sha1 42b270dd2f5aa8f9238314a376a1f0ceec580d5d,  8024 B
//   FILTER_PCSS    + AMBIENT sha1 8ccc1b02d0efde734ae4d0d5f58cbe986c5e8b0c,  8748 B
//   FILTER_POISSON           sha1 02427236dcf3dc126e41ad38aaf2c07aedd43b5f, 23012 B
//   FILTER_POISSON + AMBIENT sha1 a1d88864cd30485d84f98e36eaf281d15bd15c47, 23736 B
//
// Shadow subroot is ShadowOnlyFamily.hlsli's DIRSPLITS=1
// arrangement - the cascade slice and world-scale vector arrive from
// CB2[9].y rather than from literals - and the BRDF tail is
// DirSplits2Family.hlsli's verbatim. Native carries neither the
// slope-scaled depth bias nor the 0.999999 reference clamp here; those belong
// to SHADOW_ONLY. FILTER_POISSON's bias is the literal 0.275 with no material
// branch.

#if !defined(DIRECTIONAL)
#  error "this source is the native DIRECTIONAL family; define DIRECTIONAL"
#endif
#if defined(POINTOMNI) || defined(POINTSPOT) || defined(SPOT) || defined(HALFOMNI)
#  error "DIRECTIONAL is exclusive with the punctual light families"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the shadow-term family in ShadowOnlyFamily.hlsli"
#endif
#ifndef SHADOW
#  error "the reconstructed DIRSPLITS=1 full-BRDF family is SHADOW only"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the split count is a native axis and is never assumed"
#endif
#if DIRSPLITS != 1
#  error "this source reconstructs DIRSPLITS=1 only; 2 and 3 are separate native families"
#endif
#if !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native DIRSPLITS=1 full-BRDF blob carries both SPECULAR and RGBSPEC"
#endif
#if defined(BLENDSPLIT)
#  error "no DIRSPLITS=1 full-BRDF blob carries BLENDSPLIT; one cascade has nothing to blend"
#endif
#if defined(IGNOREROUGHNESS) || defined(IGNORERIM)
#  error "no DIRSPLITS=1 full-BRDF blob carries IGNOREROUGHNESS or IGNORERIM"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_PCSSPOISSON) + defined(FILTER_POISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif
#ifdef FILTER_PCSSPOISSON
#  error "FILTER_PCSSPOISSON is not a DIRSPLITS=1 full-BRDF permutation"
#endif
#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_PCSS) \
    && !defined(FILTER_POISSON)
#  error "the archive has no unfiltered DIRSPLITS=1 full-BRDF blob; define one comparison FILTER_*"
#endif

#include "../Common/DeferredContracts.hlsli"

#ifdef FILTER_POISSON
#  include "../Common/ShadowPoissonKernel.hlsli"
#endif

// Internal selectors.

// CB2[21..23] `ShadowWorldScale` is read only where the cascade's world-space
// near/far pair is needed: the PCSS penumbra remap or Poisson depth bias. The
// runtime slice promotes the CB2 declaration to dynamicIndexed.
#if defined(FILTER_PCSS) || defined(FILTER_POISSON)
#  define FO4_DS1_USES_WORLD_SCALE 1
#endif

// Constant buffer layouts.

cbuffer PerFrame_CB12 : register(b12)
{
    // [0..27]: shared per-frame block. See `Common/DeferredContracts.hlsli`.
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    // [28]: hair specular parameters (fHairPrimSpecScale, fHairPrimSpecPow,
    //       fHairSecSpecScale, fHairSecSpecPow).
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
    // [0]: constant ID 0 `VPOSOffset`.
    float4 ScreenSize;

    // [1]: constant ID 1 `LightVector`, sun direction in view space.
    float4 SunDirection;

    // [2]: constant ID 2 `LightColor`.
    float4 SunColor_HDR;

#ifdef AMBIENT
    // [3..5]: constant IDs 3/4/5 are absent from every blob in this family.
    float4 cb2_pad_3_5[3];

    // [6..8]: constant ID 6 `DirectionalAmbient`, the ambient gradient rows.
    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#else
    float4 cb2_pad_3_8[6];
#endif

    // [9]: constant ID 7 `SplitDistances`. At DIRSPLITS=1 native reads only .y,
    //      as the cascade array slice and (under Poisson) as the `ftou` index
    //      into the world-scale table. The .w split gate that DIRSPLITS=2 reads
    //      has no reader here: with one cascade there is nothing to gate.
    float4 cb2_idx9_cascade_slice;

    // [10]: constant ID 8 `FadeDistances`. Allocated and dead: the cascade
    //       overlap band does not exist at DIRSPLITS=1.
    float4 cb2_pad_10;

    // [11..13]: constant ID 9 `ShadowMapProj`, three rows. The register count is
    //           3 * DIRSPLITS, which is what makes this layout DIRSPLITS=1.
    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;

    // [14..19]: unallocated. The second and third cascades' rows live here in
    //           the multi-split families.
    float4 cb2_pad_14_19[6];

    // [20]: constant ID 10 `ShadowSampleParam`. .zw are inverse shadow-map
    //       dimensions, .z doubles as the Poisson kernel scale.
    float4 cb2_idx20_shadow_sample_param;

    // [21..23]: constant ID 11 `ShadowWorldScale`. Declared as the three-vector
    //           table native addresses as `cb2[r + 21]`.
    float4 cb2_idx21_cascade_world_scale[3];

    // [24]: constant ID 12 `ShadowFadeParam`.
    float4 cb2_idx24_distance_fade;
};

// Resource bindings. PCSS alone adds the raw blocker atlas at t4/s4.

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

#ifdef FILTER_PCSS
Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
#endif
Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

#ifdef FILTER_PCSS
SamplerState g_sCascadeShadowRaw : register(s4);
#endif
SamplerComparisonState g_sCascadeShadowCmp : register(s5);

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

// The single cascade's shadow factor. Same body as the DIRSPLITS=2 sibling -
// no slope-scaled bias, no 0.999999 clamp - but the slice is the runtime value
// in CB2[9].y rather than a literal 0/1, and there is no cascade selection
// around it.
float ComputeCascadeShadow(float3 posView, float slice
#ifdef FO4_DS1_USES_WORLD_SCALE
                           , float4 cascadeScale
#endif
                           )
{
    float4 posViewH = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_cascade0_row0, posViewH);
    shadowUV.y = dot(cb2_cascade0_row1, posViewH);
    float shadowZ = dot(cb2_cascade0_row2, posViewH);

#if defined(FILTER_PCF1)
    // One hardware comparison tap against the raw projected z.
    return g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)
    // Compact 3x3 box of comparison taps one shadow texel apart, averaged.
    // Native keeps this as a rolled loop pair, so the single sample_c_lz in the
    // listing is nine executed taps and CB2[20] is read once, not nine times.
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
    float2 searchStep = 1.0 / cascadeScale.xy;
    float2 blocker = 0.0;
    [loop]
    for (int bx = 0; bx < 5; ++bx)
    {
        float offsetX = float(bx - 2);
        [loop]
        for (int by = 0; by < 5; ++by)
        {
            float offsetY = float(by - 2);
            float2 tapUV = float2(offsetX, offsetY) * searchStep + shadowUV;
            float tapDepth = g_tCascadeShadowRaw.Sample(
                g_sCascadeShadowRaw, float3(tapUV, slice)).x;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = tapDepth < shadowZ ? accumulated : blocker;
        }
    }

    if (blocker.y == 0.0)
        return 1.0;

    float centerDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float sum = centerDepth >= shadowZ ? 1.0 : 0.0;

    float averageBlocker = blocker.x / blocker.y;
    float worldRange = cascadeScale.w - cascadeScale.z;
    float receiverWorld = worldRange * shadowZ + cascadeScale.z;
    float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
    float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
    float penumbra = blockerWorld < cascadeScale.z + 0.001
        ? 1.9
        : separation * 1.8 + 0.1;

    [loop]
    for (int fx = 0; fx < 5; ++fx)
    {
        float offsetX = penumbra * (float(fx) - 2.0);
        [loop]
        for (int fy = 0; fy < 5; ++fy)
        {
            float offsetY = penumbra * (float(fy) - 2.0);
            float2 tapUV = searchStep * float2(offsetX, offsetY) * 0.5 + shadowUV;
            sum += g_tCascadeShadowCmp.SampleCmpLevelZero(
                g_sCascadeShadowCmp, float3(tapUV, slice), shadowZ);
        }
    }
    return sum * 0.04;

#else  // FILTER_POISSON
    // Eight loop iterations, two taps each, over the leading 16 entries of the
    // 1000-entry immediate kernel. The bias is the literal 0.275 scaled by the
    // reciprocal world depth range - at one cascade there is no per-cascade
    // 0.275/1.0 pair, and no material branch.
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - rcpWorldRange * 0.275;
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
#endif
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

    // Near select is inclusive at exactly 0.01. Native keeps this as real
    // control flow, not a flattened select, exactly as the SHADOW_ONLY sibling.
    float  linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearizedDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

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

    float roughness01   = 1.0 - matSample.x;
    float posViewLenSq  = dot(-posView, -posView);
    float posViewLen    = rsqrt(posViewLenSq);
    float3 viewDirNeg   = -posView * posViewLen;

    // One cascade, always active. The slice and the world-scale vector come
    // from CB2[9].y; there is no fade-distance select and no SplitDistances
    // gate, because neither has a reader in any blob of this family.
    float slice = cb2_idx9_cascade_slice.y;
#ifdef FO4_DS1_USES_WORLD_SCALE
    uint   cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
#endif

    float shadow = ComputeCascadeShadow(posView, slice
#ifdef FO4_DS1_USES_WORLD_SCALE
        , cascadeScale
#endif
        );

    // Distance fade toward unshadowed at the far range: fade = 1 - D^8.
    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    shadow = fadeFactor * (shadow - 1.0) + 1.0;

    // Sun-direction lighting setup. Verbatim from the DIRSPLITS=2 sibling.
    float3 albedoPremult  = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw      = dot(normalView, SunDirection.xyz);
    float  NdotL_pos      = max(NdotL_raw, 0.0);
    float  NdotL_clamped  = min(NdotL_pos, 1.0);
    float  oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
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
        float  ambientSpecularFactor =
            exp2(log2(oneMinusNdotV) * (3.0 - matSample.x)) * 0.25;
        ambientSpecular = matSample.y * ambientSpecularFactor *
            EvaluateAmbientGradient(reflectionDir);
#endif
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
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = 1.0 - NdotV_view;
    ambientFres = exp2(log2(ambientFres) * 0.01);

    float fresEdge    = saturate(dot(viewDirNeg, -SunDirection.xyz));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;

    float3 finalDiffuse = SunColor_HDR.xyz * ambientTerm;
    finalDiffuse += SunColor_HDR.xyz * brdfShadowMix;

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

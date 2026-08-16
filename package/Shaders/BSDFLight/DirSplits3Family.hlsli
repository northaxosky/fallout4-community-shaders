// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
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

#include "../Common/DeferredContracts.hlsli"

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  include "../Common/ShadowPoissonKernel.hlsli"
#endif

#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_DS3_SHADOW_RAW_TAP 1
#endif

#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON)
#  define FO4_DS3_SHADOW_CMP_TAP 1
#endif

#if defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON) || defined(FILTER_POISSON)
#  define FO4_DS3_USES_WORLD_SCALE 1
#endif

#if defined(FILTER_PCF9) || defined(FILTER_POISSON)
#  define FO4_DS3_USES_SAMPLE_PARAM 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_hair_spec_params;

    float4 cb12_idx29_hair_spec_shifts;

    float4 cb12_idx30;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 SunDirection;

    float4 SunColor_HDR;

#ifdef AMBIENT
    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#else
    float4 cb2_pad_3_8[6];
#endif

    float4 cb2_idx9_split_distances;

    float4 cb2_idx10_fade_distances;

    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;
    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;
    float4 cb2_cascade2_row0;
    float4 cb2_cascade2_row1;
    float4 cb2_cascade2_row2;

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade0_world_scale;
    float4 cb2_idx22_cascade1_world_scale;
    float4 cb2_idx23_cascade2_world_scale;

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

#ifdef FO4_DS3_SHADOW_RAW_TAP
Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
#endif

#ifdef FO4_DS3_SHADOW_CMP_TAP
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
    return g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), shadowZ);

#elif defined(FILTER_PCF9)
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
    float tapDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    return (tapDepth >= shadowZ) ? 1.0 : 0.0;
#endif
}

float ComputeDirectionalShadow(float3 posView, float linearizedDepth)
{
#ifndef BLENDSPLIT
    if (!(linearizedDepth < cb2_idx9_split_distances.w))
    {
        return 1.0;
    }
#endif

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
    float shadow = cascade2Active ? cascade2Shadow : cascade1Shadow;
    shadow = cascade0Active ? 1.0 : shadow;
    shadow = (cb2_idx10_fade_distances.x < linearizedDepth) ? shadow : cascade0Shadow;
#endif

    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    return fadeFactor * (shadow - 1.0) + 1.0;
}

struct PS_INPUT
{
    float4 position  : SV_POSITION;
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

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

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

    float3 finalDiffuse = SunColor_HDR.xyz * brdfShadowMix;

#ifndef IGNOREROUGHNESS
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

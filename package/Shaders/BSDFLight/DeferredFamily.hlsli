// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
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

#if defined(HALFOMNI) && !defined(POINTOMNI)
#  error "HALFOMNI only ever occurs with POINTOMNI; this macro set is malformed"
#endif

#if LIGHT_TYPE == LIGHT_TYPE_POINT && defined(POINTOMNI) && defined(SHADOW)
#  error "POINTOMNI with SHADOW is the LIGHT_TYPE=3 projected-shadow ABI, not LIGHT_TYPE=2"
#endif

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

#  if defined(POINTSPOT) || (defined(POINTOMNI) && defined(SHADOW))
#    define FO4_PROJECTED_SHADOW_FAMILY 1
#  endif

#  if defined(FO4_PROJECTED_SHADOW_FAMILY) \
      && (defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON))
#    error "no native POINTSPOT or POINTOMNI blob carries FILTER_PCSS or FILTER_PCSSPOISSON"
#  endif

#endif

#include "../Common/DeferredContracts.hlsli"

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;

    float4 cb12_idx30;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 SunDirection_and_padding;

    float4 SunColor_HDR;

#ifdef AMBIENT_IBL_IN_LIGHT
    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;

    float4 cb2_pad_9;
#else
    float4 cb2_pad_3_9[7];
#endif

    float4 cb2_idx10_cascade_range;

    float4 cb2_cascade0_row0;
    float4 cb2_cascade0_row1;
    float4 cb2_cascade0_row2;

    float4 cb2_cascade1_row0;
    float4 cb2_cascade1_row1;
    float4 cb2_cascade1_row2;

    float4 cb2_pad_17_19[3];

    float4 cb2_idx20_pcf_kernel_scale;

    float4 cb2_idx21_cascade0_depth_range;

    float4 cb2_idx22_cascade1_depth_range;

    float4 cb2_pad_23;

    float4 cb2_idx24_distance_fade;
};

Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

Texture2DArray<float4> g_tCascadeShadowAtlas : register(t5);

SamplerState g_sGbufferAlbedo  : register(s0);
SamplerState g_sGbufferNormal  : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth      : register(s3);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);

#ifdef SCREEN_SPACE_SHADOWS
Texture2D<float> g_tScreenSpaceShadow : register(t6);
#endif

#ifdef WETNESS_EFFECTS
Texture2D<float> g_tWetnessMask : register(t4);
#endif

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
};

static const float FO4_DIRECTIONAL_SPECULAR_SCALE = 3.141593;

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
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

float ComputeCascadePCF(float3 posView, float4 row0, float4 row1, float4 row2,
                        float cascadeIdx, float cascadeDepthRcp, float kernelScale,
                        float biasScale)
{
    float4 posLightH;
    posLightH.x = dot(row0, float4(posView, 1.0));
    posLightH.y = dot(row1, float4(posView, 1.0));
    posLightH.z = dot(row2, float4(posView, 1.0));
    float zRef = posLightH.z - cascadeDepthRcp * biasScale;

    float accum = 0.0;
    [loop]
    for (int r = 0; r < 8; ++r)
    {
        float2 jitter0 = (SUN_SHADOW_POISSON[r * 2 + 0] - 0.5) * kernelScale;
        float2 jitter1 = (SUN_SHADOW_POISSON[r * 2 + 1] - 0.5) * kernelScale;
        float2 uv0 = posLightH.xy + jitter0 * 2.0;
        float2 uv1 = posLightH.xy + jitter1 * 2.0;
        accum += g_tCascadeShadowAtlas.SampleCmpLevelZero(g_sCascadeShadowCmp,
                                                          float3(uv0, cascadeIdx),
                                                          zRef);
        accum += g_tCascadeShadowAtlas.SampleCmpLevelZero(g_sCascadeShadowCmp,
                                                          float3(uv1, cascadeIdx),
                                                          zRef);
    }
    return accum * 0.0625;
}

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

#ifdef WETNESS_EFFECTS
    float wetness = saturate(
        g_tWetnessMask.Load(int3(int2(input.position.xy), 0)).x);
#endif

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv4.x);
    float ddy_ = ddy_coarse(uv4.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
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

#ifdef AMBIENT_IBL_IN_LIGHT
    float3 ambientDiffuse = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

    float roughness01 = 1.0 - matSample.x;
    float posViewLenSq = dot(-posView, -posView);
    float posViewLen   = rsqrt(posViewLenSq);
    float3 viewDirNeg  = -posView * posViewLen;

    bool cascade0Active = (linearizedDepth < cb2_idx10_cascade_range.y);
    bool cascade1Active = (cb2_idx10_cascade_range.x < linearizedDepth);

    float kernelScale = cb2_idx20_pcf_kernel_scale.z * 3.0;

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

    float blendRange = cb2_idx10_cascade_range.y - cb2_idx10_cascade_range.x;
    float t = saturate((linearizedDepth - cb2_idx10_cascade_range.x) / blendRange);
    float blendW = t * t * (3.0 - 2.0 * t);
    float shadowPcf = lerp(cascade0Pcf, cascade1Pcf, blendW);
    if (!cascade1Active) shadowPcf = cascade0Pcf;
    if (!cascade0Active) shadowPcf = cascade1Pcf;

    float distNorm   = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2      = distNorm * distNorm;
    float dist4      = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;
    shadowPcf = fadeFactor * (shadowPcf - 1.0) + 1.0;

#ifdef SCREEN_SPACE_SHADOWS
    shadowPcf *= g_tScreenSpaceShadow.Load(int3(int2(input.position.xy), 0)).x;
#endif

    float3 albedoPremult = albedoSample.w * albedoSample.xyz;
    float  NdotL_raw     = dot(normalView, SunDirection_and_padding.xyz);
    float  NdotL_pos     = max(NdotL_raw, 0.0);
    float  NdotL_clamped = min(NdotL_pos, 1.0);
    float  oneMinusGloss = 1.0 - saturate(cb12_idx30.y);
    float  oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float  oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float  schlickFres   = 1.0 - oneMinusGloss * oneMinusGloss4;

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

    float3 brdfSpecular = float3(0, 0, 0);
    float  brdfModulator = 0.0;
    float  brdfShadowMix = 0.0;
    if (isMaterial1)
    {
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

#endif

#if LIGHT_TYPE == LIGHT_TYPE_POINT

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 LightPos_and_Radius;

    float4 LightColor_HDR;

    float4 cb2_idx3_attenuation_curve;

    float4 cb2_pad_4_10[7];

    float4 cb2_lightspace_row0;
    float4 cb2_lightspace_row1;
    float4 cb2_lightspace_row2;
    float4 cb2_lightspace_row3;

};

Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

Texture2D<float4> g_tLightCookie : register(t7);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);
SamplerState g_sLightCookie     : register(s7);

float3 DecodeOctahedralNormal(float2 enc01)
{
    float2 enc = enc01 * 4.0 - 2.0;
    float  encLenSq = dot(enc, enc);
    float  z = -(1.0 - encLenSq * 0.5);
    float  recon = 1.0 - encLenSq * 0.25;
    float  scale = sqrt(recon);
    return float3(enc * scale, z);
}

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

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
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

    float4 matSample    = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc    = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01 = 1.0 - matSample.x;

    float posViewLenInv = rsqrt(dot(posView, posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;

    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_sat     = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

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

#endif

#if LIGHT_TYPE == LIGHT_TYPE_SPOT

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

#ifndef ATTENUATION_ONLY
    float4 cb12_idx28_sss_params;

    float4 cb12_idx29_sss_angles;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 LightPos_and_Radius;

    float4 LightColor_HDR;

    float4 cb2_idx3_attenuation_curve;

#ifdef SPOT
    float4 cb2_pad_4;

    float4 SpotData;

    float4 cb2_pad_6_10[5];

#  ifdef GOBOPROJECTION
    float4 cb2_gobo_row0;
    float4 cb2_gobo_row1;
    float4 cb2_gobo_row2_unread;
    float4 cb2_gobo_row3;
#  else
    float4 cb2_pad_11_14[4];
#  endif

    float4 cb2_pad_15_19[5];
#endif

#ifdef FO4_PROJECTED_SHADOW_FAMILY
    float4 cb2_pad_4_10[7];

    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;
    float4 cb2_shadowproj_row3;

    float4 cb2_idx15_shadow_sample_param;

    float4 cb2_pad_16_18[3];

    float4 cb2_idx19_shadow_fade;
#endif

    float4 ShadowLightParam;
};

#ifndef ATTENUATION_ONLY
Texture2D<float4> g_tGbufferAlbedo : register(t0);

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);
#endif

Texture2D<float4> g_tMainDepth : register(t3);

#ifdef FO4_PROJECTED_SHADOW_FAMILY
#  if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_POISSON)
Texture2DArray<float4> g_tSpotShadowAtlas : register(t5);
SamplerComparisonState g_sSpotShadowCmp : register(s5);
#  else
Texture2DArray<float4> g_tSpotShadowAtlas : register(t4);
SamplerState g_sSpotShadow : register(s4);
#  endif
#endif

#ifdef GOBOPROJECTION
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

#ifndef ATTENUATION_ONLY
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

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float2 uv = uv4.xy;

    float ddx_ = ddx_coarse(uv.x);
    float ddy_ = ddy_coarse(uv.y);
    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv,
                                           ddx_.xx, ddy_.xx).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
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

    float3 toLight      = LightPos_and_Radius.xyz - posView;
    float  toLightLenSq = dot(toLight, toLight);
    float  d            = sqrt(toLightLenSq);
    float  dNorm        = saturate(d / LightPos_and_Radius.w);
    float  dPowZ        = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float  falloffLin   = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                   + cb2_idx3_attenuation_curve.x);
    float  attenuation  = exp2(log2(1.0 - falloffLin) * 2.2);

#ifdef SPOT
    float3 lightDir = toLight * rsqrt(toLightLenSq);

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
    float3 lightDir = toLight * rsqrt(toLightLenSq);
#endif

#ifndef ATTENUATION_ONLY
    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

    float roughness01 = 1.0 - matSample.x;
#endif

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
    float shadowDepth = g_tSpotShadowAtlas.Sample(
        g_sSpotShadow, float3(shadowUV, shadowSlice)).x;
    shadowFactor = (shadowDepth >= shadowRef) ? 1.0 : 0.0;
#  endif

#  if defined(POINTOMNI) && defined(SHADOW) && defined(HALFOMNI)
    shadowFactor = halfAccepted ? shadowFactor : 0.0;
#  endif

#  if !(defined(POINTOMNI) && defined(SHADOW))
    float2 projFade = (float2(shadowUV.x, 1.0 - shadowUV.y) - ShadowLightParam.y)
                    / ShadowLightParam.z;
    float  projDist = sqrt(dot(projFade, projFade));
    float  edgeFall = exp2(log2(projDist) * ShadowLightParam.x);
    edgeFall = min(edgeFall, 1.0);
    shadowFactor *= (1.0 - edgeFall);
#  endif

    float shadowDistNorm = saturate(dot(posView, posView)
                                    / cb2_idx19_shadow_fade.x);
    float shadowDist2 = shadowDistNorm * shadowDistNorm;
    float shadowDist4 = shadowDist2 * shadowDist2;
    shadowFactor *= (1.0 - shadowDist4 * shadowDist4);

    attenuation = attenuation * shadowFactor;
#endif

#ifndef ATTENUATION_ONLY
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_sat     = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

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
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;
    diffuseAccum = LightColor_HDR.xyz * ambientTerm + diffuseAccum;
#  endif
#else
    float3 diffuseAccum = LightColor_HDR.xyz;
    float3 brdfSpecular = float3(0, 0, 0);
#endif

#ifdef GOBOPROJECTION
#  ifdef SPOT
    float4 goboHomog = float4(posView, 1.0);
    float2 goboProj;
    goboProj.x = dot(cb2_gobo_row0, goboHomog);
    goboProj.y = dot(cb2_gobo_row1, goboHomog);
    float goboW = dot(cb2_gobo_row3, goboHomog);
    float2 goboUV = (goboProj / goboW.xx) * 0.5 + 0.5;
#  elif defined(POINTOMNI) && defined(SHADOW)
#    ifdef HALFOMNI
    float2 goboUV = omniUV;
#    else
    float2 goboUV = float2(omniUV.x,
                           backHemisphere ? 1.0 - 0.5 * omniUV.y
                                          : 0.5 * omniUV.y);
#    endif
#  else
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

#endif

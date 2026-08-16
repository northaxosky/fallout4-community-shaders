// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
#if defined(SHADOW)
#  error "this source is the no-SHADOW family; the shadowed DIRSPLITS=2 blobs are in DirSplits2Family.hlsli"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the DIRSPLITS=1 family in ShadowOnlyFamily.hlsli"
#endif
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  error "FILTER_* selects a shadow tap and requires SHADOW; a missing SHADOW is never a filtered shader"
#endif
#ifdef HALFOMNI
#  error "HALFOMNI occurs only with POINTOMNI and SHADOW; it is not part of this layer"
#endif
#ifdef GOBOPROJECTION
#  error "GOBOPROJECTION blobs declare t7/s7 and are a different resource contract"
#endif
#ifdef ATTENUATION_ONLY
#  error "ATTENUATION_ONLY is the t3-only family in AttenuationOnlyFamily.hlsli"
#endif
#if defined(SPOT) || defined(POINTSPOT)
#  error "the no-SHADOW SPOT blobs stay with the legacy adapter in DeferredFamily.hlsli"
#endif
#if (defined(DIRECTIONAL) + defined(POINTOMNI)) != 1
#  error "define exactly one of DIRECTIONAL or POINTOMNI; the archive also carries a no-light-kind AMBIENT blob at DIRSPLITS=2 that this source does not reconstruct"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; it is a native axis and is never assumed, even though these eleven only carry it as a decoder baseline"
#endif
#if DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only; DIRSPLITS=1 and DIRSPLITS=3 are separate native families"
#endif
#if !defined(RGBSPEC)
#  error "every native no-SHADOW DIRSPLITS=2 blob carries RGBSPEC"
#endif

#ifdef DIRECTIONAL
#  ifdef IGNORERIM
#    error "no DIRECTIONAL blob carries IGNORERIM; the archive puts it on POINTOMNI and SPOT only"
#  endif
#  if defined(AMBIENT) && !defined(SPECULAR)
#    error "the two AMBIENT directional blobs both carry SPECULAR; AMBIENT alone is not a native set here"
#  endif
#  if defined(IGNOREROUGHNESS) && !defined(SPECULAR)
#    error "all 19 decoded DIRECTIONAL+IGNOREROUGHNESS blobs carry SPECULAR; the 11 archive blobs that drop SPECULAR are POINTOMNI/POINTSPOT/SPOT, never DIRECTIONAL"
#  endif
#endif

#ifdef POINTOMNI
#  ifdef AMBIENT
#    error "no POINTOMNI blob carries AMBIENT; its CB2[4] has no DirectionalAmbient rows"
#  endif
#  if defined(IGNOREROUGHNESS) && defined(IGNORERIM)
#    error "no POINTOMNI blob carries both IGNOREROUGHNESS and IGNORERIM"
#  endif
#endif

#include "../Common/DeferredContracts.hlsli"

#if defined(DIRECTIONAL) && defined(SPECULAR)
#  define FO4_UNSHADOWED_USES_GLOSS_FRESNEL 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;

    float4 cb12_idx28_hair_spec_params;

    float4 cb12_idx29_hair_spec_shifts;

#if defined(DIRECTIONAL) && defined(SPECULAR)
    float4 cb12_idx30;
#endif
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 LightVector;

    float4 LightColor_HDR;

#if defined(POINTOMNI)
    float4 LightAttenuation;
#elif defined(AMBIENT)
    float4 cb2_pad_3_5[3];

    float4 cb2_ambient_gradient_row0;
    float4 cb2_ambient_gradient_row1;
    float4 cb2_ambient_gradient_row2;
#endif
};

Texture2D<float4> g_tGbufferAlbedo   : register(t0);
Texture2D<float4> g_tGbufferNormal   : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tMainDepth       : register(t3);

SamplerState g_sGbufferAlbedo   : register(s0);
SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

static const float FO4_SPECULAR_SCALE = 3.141593;

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

    float  linearizedDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    if (depth <= 0.01)
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

    float2 uvFlipped = float2(uv4.z, 1.0 - uv4.w);
    float2 uvNDC = uvFlipped * 2.0 - 1.0;
    float4 pos4  = float4(uvNDC, linearizedDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

#ifdef POINTOMNI
    float3 toLight   = LightVector.xyz - posView;
    float  distSq    = dot(toLight, toLight);
    float  distNorm  = saturate(sqrt(distSq) / LightVector.w);
    float  falloff   = exp2(log2(distNorm) * LightAttenuation.z);
    float  attenBase = saturate(LightAttenuation.y * falloff + LightAttenuation.x);
    float  attenuation = exp2(log2(1.0 - attenBase) * 2.2);

    if (attenuation <= 0.001)
    {
        output.diffuse  = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(distSq);
#else
    float3 lightDir = LightVector.xyz;
#endif

    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;

    float3 normalView = DecodeOctahedralNormal(normalEnc);

#ifdef AMBIENT
    float3 ambientDiffuse  = EvaluateAmbientGradient(normalView);
    float3 ambientSpecular = 0.0;
#endif

#ifndef IGNOREROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif

    float  posViewLen  = rsqrt(dot(-posView, -posView));
    float3 viewDirNeg  = -posView * posViewLen;

#ifdef SPECULAR
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_pos     = max(NdotL_raw, 0.0);
    float NdotL_clamped = min(NdotL_pos, 1.0);
#endif

#ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
    float oneMinusGloss  = 1.0 - saturate(cb12_idx30.y);
    float oneMinusGloss2 = oneMinusGloss * oneMinusGloss;
    float oneMinusGloss4 = oneMinusGloss2 * oneMinusGloss2;
    float schlickFres    = 1.0 - oneMinusGloss * oneMinusGloss4;
#endif

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);

    float3 brdfSpecular  = float3(0, 0, 0);
    float  brdfShadowMix = 0.0;
#ifdef DIRECTIONAL
    float  brdfModulator = 0.0;
    float4 albedoSample  = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv);
#endif

    if (isMaterial1)
    {
#ifdef POINTOMNI
        float albedoAlpha = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
#else
        float albedoAlpha = albedoSample.w;
#endif
        float skinNdotL  = dot(matSample.xyz, lightDir);
        float skinNdotV  = dot(matSample.xyz, viewDirNeg);
        float sinScaleL  = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV  = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#ifndef SPECULAR
        float NdotL_pos  = max(dot(normalView, lightDir), 0.0);
#endif

        float sinA1, cosA1;
        sincos(cb12_idx29_hair_spec_shifts.y, sinA1, cosA1);
        float rot1     = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1     = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1     = exp2(log2(vis1) * cb12_idx28_hair_spec_params.w);
        float hairIntensity =
            saturate(cb12_idx28_hair_spec_params.z * pow1 + NdotL_pos);
        brdfShadowMix = min(albedoAlpha, hairIntensity);

#ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_hair_spec_shifts.x, sinA2, cosA2);
        float rot2     = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2     = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2     = exp2(log2(vis2) * cb12_idx28_hair_spec_params.y)
            * cb12_idx28_hair_spec_params.x;

        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#endif
    }
    else
    {
        float NdotV_raw = dot(viewDirNeg, normalView);
#ifndef SPECULAR
        float NdotL_raw = dot(lightDir, normalView);
        float NdotL_pos = max(NdotL_raw, 0.0);
#endif

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
        float3 tangentL  = lightDir - normalView * NdotL_raw;
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

#ifdef SPECULAR
        float specExpBase   = exp2(matSample.x * 10.0 + 1.0);
#ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
        float specExpScale = 1.0 - schlickFres * 0.98;
        float specExp      = specExpScale * specExpBase;
        float distributionNorm = (specExpBase * specExpScale + 2.0) * 0.159155;
#else
        float specExp = specExpBase;
        float distributionNorm = (specExpBase + 2.0) * 0.159155;
#endif

        float3 halfVec = lightDir - posView * posViewLen;
        halfVec *= rsqrt(dot(halfVec, halfVec));

        float NdotV_sat = saturate(NdotV_raw);
        float VdotH     = saturate(dot(viewDirNeg, halfVec));
        float NdotH     = saturate(dot(halfVec, normalView));

        float distribution = exp2(log2(NdotH) * specExp);
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
        specMag *= FO4_SPECULAR_SCALE;

        brdfSpecular = NdotL_clamped * (specMag * LightColor_HDR.xyz);
#endif

#ifdef DIRECTIONAL
        brdfModulator = matSample.z * 100.0;
#endif
    }

#ifndef SPECULAR
    float NdotL_raw     = dot(normalView, lightDir);
    float NdotL_clamped = saturate(NdotL_raw);
#endif

    float3 finalDiffuse = LightColor_HDR.xyz * brdfShadowMix;

#if !defined(IGNOREROUGHNESS) && !defined(IGNORERIM)
    float NdotV_view  = saturate(dot(normalView, viewDirNeg));
    float ambientFres = exp2(log2(1.0 - NdotV_view) * 0.01);
    float fresEdge    = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = fresEdge * ambientFres * NdotL_clamped * roughness01;

    finalDiffuse += LightColor_HDR.xyz * ambientTerm;
#endif

#ifdef DIRECTIONAL
    float3 albedoPremult = albedoSample.w * albedoSample.xyz;

    float backfaceWrap = saturate(-NdotL_raw);
    finalDiffuse += LightColor_HDR.xyz * (albedoPremult * backfaceWrap);

    float forwardBlend = saturate((brdfModulator + NdotL_raw) / (brdfModulator + 1.0));
    forwardBlend = max(forwardBlend - NdotL_clamped, 0.0);
    finalDiffuse += (forwardBlend * LightColor_HDR.xyz) * albedoSample.xyz;
#endif

#ifdef POINTOMNI
    finalDiffuse *= attenuation;
#endif

#ifdef SPECULAR
#  ifdef FO4_UNSHADOWED_USES_GLOSS_FRESNEL
    output.specular.xyz = (1.0 - schlickFres * 0.5) * brdfSpecular;
#  else
    output.specular.xyz = attenuation * brdfSpecular;
#  endif
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
#ifdef AMBIENT
    output.specular.xyz += ambientSpecular;
#endif
    output.specular.w = 1.0;

    output.diffuse.xyz = finalDiffuse;
#ifdef AMBIENT
    output.diffuse.xyz += ambientDiffuse;
#endif
    output.diffuse.xyz /= 3.0;
    output.diffuse.w = 0.0;

    return output;
}

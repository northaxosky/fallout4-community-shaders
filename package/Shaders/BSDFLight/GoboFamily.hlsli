// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
#if !defined(POINTOMNI)
#  error "this source is the native POINTOMNI gobo family; define POINTOMNI"
#endif
#if !defined(GOBOPROJECTION)
#  error "this source owns the t7/s7 GOBOPROJECTION contract; define GOBOPROJECTION"
#endif
#if !defined(RGBSPEC)
#  error "every native unshadowed POINTOMNI gobo blob carries RGBSPEC"
#endif
#if !defined(DIRSPLITS)
#  error "define DIRSPLITS; the decoder baseline is a native axis"
#endif
#if DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only"
#endif
#ifdef SHADOW
#  error "the shadowed POINTOMNI family has a distinct shadow-map ABI"
#endif
#if defined(DIRECTIONAL) || defined(POINTSPOT) || defined(SPOT)
#  error "POINTOMNI gobo is exclusive with the directional and projected light families"
#endif
#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  error "FILTER_* selects a shadow tap and is invalid without SHADOW"
#endif
#ifdef SHADOW_ONLY
#  error "SHADOW_ONLY is the directional DIRSPLITS=1 family"
#endif
#ifdef AMBIENT
#  error "no native unshadowed POINTOMNI gobo blob carries AMBIENT"
#endif
#ifdef ATTENUATION_ONLY
#  error "ATTENUATION_ONLY is not a native POINTOMNI gobo permutation"
#endif
#ifdef HALFOMNI
#  error "HALFOMNI only occurs on the shadowed POINTOMNI path"
#endif
#ifdef BLENDSPLIT
#  error "BLENDSPLIT is a directional cascade axis"
#endif
#if defined(IGNORERIM) && defined(IGNOREROUGHNESS)
#  error "the combined IGNORERIM and IGNOREROUGHNESS cells are not admitted in Wave 1"
#endif

#include "../Common/DeferredContracts.hlsli"

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
    float toLightLenSq = dot(toLight, toLight);
    float d = sqrt(toLightLenSq);
    float dNorm = saturate(d / LightPos_and_Radius.w);
    float dPowZ = exp2(log2(dNorm) * cb2_idx3_attenuation_curve.z);
    float falloffLin = saturate(cb2_idx3_attenuation_curve.y * dPowZ
                                + cb2_idx3_attenuation_curve.x);
    float attenuation = exp2(log2(1.0 - falloffLin) * 2.2);

    if (attenuation <= 0.001)
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    float3 lightDir = toLight * rsqrt(toLightLenSq);
    float4 matSample = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv);
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv).xy;
    float3 normalView = DecodeOctahedralNormal(normalEnc);
#ifndef IGNOREROUGHNESS
    float roughness01 = 1.0 - matSample.x;
#endif
    float posViewLenInv = rsqrt(dot(posView, posView));
    float3 viewDirNeg = -posView * posViewLenInv.xxx;

#ifdef SPECULAR
    float NdotL_raw = dot(normalView, lightDir);
    float NdotL_sat = max(NdotL_raw, 0.0);
    float NdotL_clamped = saturate(NdotL_sat);
#endif

    bool isMaterial1 = (abs(matSample.w * 255.0 - 1.0) < 0.25);
    float3 brdfSpecular = float3(0, 0, 0);
    float brdfShadowMix = 0.0;
    if (isMaterial1)
    {
        float albedoW = g_tGbufferAlbedo.Sample(g_sGbufferAlbedo, uv).w;
        float skinNdotL = dot(matSample.xyz, lightDir);
        float skinNdotV = dot(matSample.xyz, viewDirNeg);
        float sinScaleL = sqrt(1.0 - min(skinNdotL * skinNdotL, 1.0));
        float sinScaleV = sqrt(1.0 - min(skinNdotV * skinNdotV, 1.0));
#ifndef SPECULAR
        float NdotL_sat = max(dot(normalView, lightDir), 0.0);
#endif

        float sinA1, cosA1;
        sincos(cb12_idx29_sss_angles.y, sinA1, cosA1);
        float rot1 = -skinNdotL * cosA1 - sinScaleL * sinA1;
        float rot1Perp = sqrt(1.0 - rot1 * rot1);
        float vis1 = max(rot1 * skinNdotV + sinScaleV * rot1Perp, 0.0);
        float pow1 = exp2(log2(vis1) * cb12_idx28_sss_params.w);
        float sssIntensity = saturate(cb12_idx28_sss_params.z * pow1 + NdotL_sat);
        brdfShadowMix = min(albedoW, sssIntensity);

#ifdef SPECULAR
        float sinA2, cosA2;
        sincos(cb12_idx29_sss_angles.x, sinA2, cosA2);
        float rot2 = -skinNdotL * cosA2 - sinScaleL * sinA2;
        float rot2Perp = sqrt(1.0 - rot2 * rot2);
        float vis2 = max(rot2 * skinNdotV + sinScaleV * rot2Perp, 0.0);
        float pow2 = exp2(log2(vis2) * cb12_idx28_sss_params.y) *
            cb12_idx28_sss_params.x;
        brdfSpecular = NdotL_clamped * (pow2 * LightColor_HDR.xyz);
#endif
    }
    else
    {
#if !defined(IGNOREROUGHNESS) || defined(SPECULAR)
        float NdotV_raw = dot(viewDirNeg, normalView);
#endif
#ifndef SPECULAR
        float NdotL_raw = dot(lightDir, normalView);
        float NdotL_sat = max(NdotL_raw, 0.0);
        float NdotL_clamped = saturate(NdotL_sat);
#endif
#ifdef IGNOREROUGHNESS
        brdfShadowMix = NdotL_sat;
#else
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
#endif

#ifdef SPECULAR
        float specExp = exp2(matSample.x * 10.0 + 1.0);
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
        float fresnelTerm = (1.0 - oneMinusVdotH5) * 0.2 + oneMinusVdotH5;
        fresnelTerm = min(fresnelTerm, 1.0);
        float specMag = visibility * fresnelTerm;
        specMag = distributionNorm * specMag;
        specMag *= 0.25;
        specMag = min(specMag, 15.0);
        specMag *= matSample.y;
        specMag *= 3.141593;
        brdfSpecular = NdotL_clamped * (specMag * LightColor_HDR.xyz);
#endif
    }

#ifndef SPECULAR
    float NdotL_raw = dot(normalView, lightDir);
#ifndef IGNOREROUGHNESS
    float NdotL_clamped = saturate(NdotL_raw);
#endif
#endif
#ifdef IGNOREROUGHNESS
    float ambientTerm = 0.0;
#elif !defined(IGNORERIM)
    float NdotV_view = saturate(dot(normalView, viewDirNeg));
    float edge = exp2(log2(1.0 - NdotV_view) * 0.01);
    float toLightDotView = saturate(dot(viewDirNeg, -lightDir));
    float ambientTerm = toLightDotView * edge * NdotL_clamped * roughness01;
#else
    float ambientTerm = 0.0;
#endif
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
    float3 specAccum = cookieRGB * brdfSpecular;
#ifdef SPECULAR
    output.specular.xyz = attenuation * specAccum;
#else
    output.specular.xyz = float3(0, 0, 0);
#endif
    output.specular.w = 1.0;
    output.diffuse.xyz = (attenuation * diffuseAccum) / 3.0;
    output.diffuse.w = 0.0;
    return output;
}

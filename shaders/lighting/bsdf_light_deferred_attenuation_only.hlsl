// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
// Native BSDFLightShader POINTOMNI attenuation-only family.
// AE 1.11.221 blob aa5cd5f492d921546a2b9cf66d34eae9baedf63f.

#if !defined(POINTOMNI) || POINTOMNI != 1
#  error "define POINTOMNI=1 for this source"
#endif
#if !defined(ATTENUATION_ONLY) || ATTENUATION_ONLY != 1
#  error "define ATTENUATION_ONLY=1 for this source"
#endif
#if !defined(RGBSPEC) || RGBSPEC != 1
#  error "define RGBSPEC=1 for this source"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 2
#  error "this source reconstructs DIRSPLITS=2 only"
#endif
#if defined(LIGHT_TYPE) || defined(DIRECTIONAL) || defined(POINTSPOT) \
    || defined(SPOT) || defined(SHADOW) || defined(SHADOW_ONLY) \
    || defined(SPECULAR) || defined(AMBIENT) || defined(BLENDSPLIT) \
    || defined(OVERDRAW) \
    || defined(CHARACTER_LIGHT) \
    || defined(AMBIENT_IBL_IN_LIGHT) || defined(SCREEN_SPACE_SHADOWS) \
    || defined(WETNESS_EFFECTS) \
    || defined(IGNORERIM) || defined(IGNOREROUGHNESS) \
    || defined(GOBOPROJECTION) || defined(HALFOMNI) \
    || defined(FILTER_PCF1) || defined(FILTER_PCF9) \
    || defined(FILTER_PCSS) || defined(FILTER_POISSON) \
    || defined(FILTER_PCSSPOISSON)
#  error "unsupported macro for the POINTOMNI attenuation-only family"
#endif

#include "deferred_contracts.hlsli"

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 LightPos_and_Radius;
    float4 LightColor_HDR;
    float4 LightAttenuation;
};

Texture2D<float4> g_tMainDepth : register(t3);
SamplerState g_sMainDepth : register(s3);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 posUnused : POSITION14;
};

struct PS_OUTPUT
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float4 uv4 = input.position.xyxy * ScreenSize.xyzw;
    float depth = g_tMainDepth.SampleGrad(
        g_sMainDepth,
        uv4.xy,
        ddx_coarse(uv4.x).xx,
        ddy_coarse(uv4.y).xx).x;

    float linearizedDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
    if (depth <= asfloat(0x3c23d70a))
    {
        linearizedDepth = depth * asfloat(0x42c80000);
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearizedDepth =
            depth * asfloat(0x3f8147ae) + asfloat(0xbc23d70a);
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 screen = uv4.zw * float2(
        asfloat(0x3f800000),
        asfloat(0xbf800000)) + float2(
        asfloat(0x00000000),
        asfloat(0x3f800000));
    float2 ndc = screen * asfloat(0x40000000) - asfloat(0x3f800000);
    float4 position = float4(ndc, linearizedDepth, asfloat(0x3f800000));

    float4 positionViewH;
    positionViewH.x = dot(reprojRow0, position);
    positionViewH.y = dot(reprojRow1, position);
    positionViewH.z = dot(reprojRow2, position);
    positionViewH.w = dot(reprojRow3, position);
    float3 positionView = positionViewH.xyz / positionViewH.www;

    float3 delta = LightPos_and_Radius.xyz - positionView;
    float distance = length(delta);
    float normalizedDistance = saturate(distance / LightPos_and_Radius.w);
    float falloff = pow(normalizedDistance, LightAttenuation.z);
    float biased = saturate(
        LightAttenuation.y * falloff + LightAttenuation.x);
    float attenuation = pow(
        asfloat(0x3f800000) - biased,
        asfloat(0x400ccccd));

    if (attenuation <= asfloat(0x3a83126f))
    {
        output.diffuse = float4(0, 0, 0, 0);
        output.specular = float4(0, 0, 0, 0);
        return output;
    }

    output.diffuse.xyz = attenuation * LightColor_HDR.xyz;
    output.diffuse.w = 0;
    output.diffuse /= asfloat(0x40400000);
    output.specular = float4(0, 0, 0, asfloat(0x3f800000));
    return output;
}

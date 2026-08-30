// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12_pad_0_13[14];

    float4 ViewToWorld_row2_fog_plane;

    float4 cb12_pad_15_19[5];

    float4 FarReproj_row0;
    float4 FarReproj_row1;
    float4 FarReproj_row2;
    float4 FarReproj_row3;

    float4 NearReproj_row0;
    float4 NearReproj_row1;
    float4 NearReproj_row2;
    float4 NearReproj_row3;

    float4 cb12_pad_28_34[7];

    float4 CameraPosAdjust_for_fog_height;

    float4 CameraPreviousPosAdjust;
    float4 cb12_pad_37_40[4];

    float4 FogDistanceRamp_and_lowHeightRamp;

    float4 FogNearLowColor_and_power;

    float4 FogNearHighColor_and_clamp;

    float4 FogFarLowColor_and_highDensityScale;

    float4 FogFarHighColor_and_padding;

    float4 FogHeightRampScaleBiasPair;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 SunDirection_and_intensity;

    float4 SunColor_and_SpecPower;
};

Texture2D<float4> g_tHdrBaseColor : register(t0);

Texture2D<float4> g_tMaterialIdBuffer : register(t3);

Texture2D<float4> g_tSecondaryColor : register(t4);

Texture2D<float4> g_tDirectDiffuse : register(t5);

Texture2D<float4> g_tLinearDepth : register(t7);

Texture2D<float4> g_tDirectSpecular : register(t11);

SamplerState g_sBaseColor       : register(s0);
SamplerState g_sMaterialId      : register(s3);
SamplerState g_sSecondaryColor  : register(s4);
SamplerState g_sDirectDiffuse   : register(s5);
SamplerState g_sDepth           : register(s7);
SamplerState g_sDirectSpecular  : register(s11);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float2 uv = input.position.xy * ScreenSize.xy;

    float matIdRaw = g_tMaterialIdBuffer.SampleLevel(g_sMaterialId, uv, 0).w;
    float depth    = g_tLinearDepth.SampleLevel(g_sDepth, uv, 0).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

    float matIdByte = matIdRaw * 255.0;
    bool  isMatId2  = abs(matIdByte - 2.0) < 0.25;
    bool  isMatId3  = abs(matIdByte - 3.0) < 0.25;
    bool  isSkinOrHair = isMatId2 || isMatId3;

    if (!isSkinOrHair)
    {
        float3 baseColor    = g_tHdrBaseColor.SampleLevel(g_sBaseColor, uv, 0).xyz;
        float3 directDiff   = g_tDirectDiffuse.SampleLevel(g_sDirectDiffuse, uv, 0).xyz;
        float3 directSpec   = g_tDirectSpecular.SampleLevel(g_sDirectSpecular, uv, 0).xyz;
        float3 directTotal  = directDiff + directSpec;
        float3 litColor     = baseColor * directTotal;

        float3 secondaryColor = g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;

        float3 uvRemapped;
        uvRemapped.x = uv.x * ScreenSize.z;
        uvRemapped.z = -uv.y * ScreenSize.w + 1.0;
        float2 uvNDC = uvRemapped.xz * 2.0 - 1.0;

        float4 pos4 = float4(uvNDC, linearizedDepth, 1.0);
        float4 posViewH;
        posViewH.x = dot(reprojRow0, pos4);
        posViewH.y = dot(reprojRow1, pos4);
        posViewH.z = dot(reprojRow2, pos4);
        posViewH.w = dot(reprojRow3, pos4);
        float3 posView = posViewH.xyz / posViewH.www;

        float fogPlaneDistance = dot(ViewToWorld_row2_fog_plane, float4(posView, 1.0));
        fogPlaneDistance += CameraPosAdjust_for_fog_height.z;

        float posViewLenSq = dot(posView, posView);
        float posViewLen   = sqrt(posViewLenSq);
        float distanceRamp = posViewLen * FogDistanceRamp_and_lowHeightRamp.x
                             - FogDistanceRamp_and_lowHeightRamp.z;
        float distanceFactor = saturate(distanceRamp);

        float2 fogRemapPair = saturate(fogPlaneDistance.xx
                                       * FogHeightRampScaleBiasPair.xy
                                       - FogHeightRampScaleBiasPair.zw);
        float  fogBlend     = lerp(fogRemapPair.x, fogRemapPair.y, distanceFactor);

        float fogIntensityClamp;
        if (distanceRamp > 0.75)
        {
            float t = (distanceFactor - 0.75) * 4.0;
            float scaled = t * (1.0 - FogNearHighColor_and_clamp.w)
                             + FogNearHighColor_and_clamp.w;
            fogIntensityClamp = min(scaled, 1.0);
        }
        else
        {
            fogIntensityClamp = FogNearHighColor_and_clamp.w;
        }

        float nearEscape = (distanceRamp < 0.015)
                           ? (distanceFactor * 66.666672)
                           : 1.0;

        float distancePow   = pow(distanceFactor, FogNearLowColor_and_power.w);
        float fogIntensity  = min(fogIntensityClamp, distancePow);

        float fogBlendWeight = fogBlend * FogFarLowColor_and_highDensityScale.w
                               + (1.0 - fogBlend);

        float3 fogColorAC = lerp(FogNearLowColor_and_power.xyz,
                                 FogFarLowColor_and_highDensityScale.xyz,
                                 fogIntensity);
        float3 fogColorBD = lerp(FogNearHighColor_and_clamp.xyz,
                                 FogFarHighColor_and_padding.xyz,
                                 fogIntensity);
        float3 fogColor   = lerp(fogColorAC, fogColorBD, fogBlend);

        float combinedFog = fogBlendWeight * fogIntensity;

        float3 viewDirUnit = posView * rsqrt(posViewLenSq);
        float  fogMixFactor = combinedFog * nearEscape;

        float NdotL    = max(dot(viewDirUnit, SunDirection_and_intensity.xyz), 0.0);
        float specular = pow(NdotL, SunColor_and_SpecPower.w)
                         * SunDirection_and_intensity.w;

        float3 sunlitFogColor = lerp(fogColor, SunColor_and_SpecPower.xyz,
                                     specular);

        float3 ambientWeighted = litColor * 3.0 + secondaryColor;
        float  gray            = dot(ambientWeighted, float3(1.0/3.0, 1.0/3.0, 1.0/3.0));
        float3 graySaturated   = sunlitFogColor + gray * (gray.xxx - sunlitFogColor);

        bool useGraySaturated = (fogMixFactor < FogNearHighColor_and_clamp.w);
        output.color.xyz = useGraySaturated ? graySaturated : sunlitFogColor;

        output.color.w = fogMixFactor;
    }
    else
    {
        output.color = float4(0, 0, 0, 0);
    }

    return output;
}

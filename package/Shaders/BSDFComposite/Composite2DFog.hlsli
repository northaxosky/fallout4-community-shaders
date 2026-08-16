// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
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

    float4 cb12_pad_36_40[5];

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

#if COMPOSITE_MODULATION
    float4 cb2_pad_3_4[2];
    float4 ModulationUvClamp;
#endif
};

Texture2D<float4> g_tHdrBaseColor : register(t0);

#if COMPOSITE_HAS_TYPE
Texture2D<float4> g_tMaterialIdBuffer : register(t3);
#endif

Texture2D<float4> g_tSecondaryColor : register(t4);

Texture2D<float4> g_tDirectDiffuse : register(t5);

#if COMPOSITE_HAS_LIGHT
Texture2D<float4> g_tAmbientLight : register(t6);
#endif

Texture2D<float4> g_tLinearDepth : register(t7);

#if COMPOSITE_MODULATION
Texture2D<float4> g_tModulation : register(t9);
#endif

#if TILED_LIGHTS
Texture2D<float4> g_tDirectSpecular : register(t11);
#if COMPOSITE_TILE_AMBIENT
Texture2D<float4> g_tAmbientLightSecondary : register(t12);
#endif
#endif

SamplerState g_sBaseColor       : register(s0);
#if COMPOSITE_HAS_TYPE
SamplerState g_sMaterialId      : register(s3);
#endif
SamplerState g_sSecondaryColor  : register(s4);
SamplerState g_sDirectDiffuse   : register(s5);
#if COMPOSITE_HAS_LIGHT
SamplerState g_sAmbientLight    : register(s6);
#endif
SamplerState g_sDepth           : register(s7);
#if COMPOSITE_MODULATION
SamplerState g_sModulation      : register(s9);
#endif
#if TILED_LIGHTS
SamplerState g_sDirectSpecular  : register(s11);
#if COMPOSITE_TILE_AMBIENT
SamplerState g_sAmbientLightSecondary : register(s12);
#endif
#endif

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

#if COMPOSITE_HAS_TYPE
    float matIdRaw = g_tMaterialIdBuffer.SampleLevel(g_sMaterialId, uv, 0).w;
    float matIdByte = matIdRaw * 255.0;
#endif
    float depth    = g_tLinearDepth.SampleLevel(g_sDepth, uv, 0).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

#if COMPOSITE_MATERIAL_EXCLUSION
    bool  isMatId2  = abs(matIdByte - 2.0) < 0.25;
    bool  isMatId3  = abs(matIdByte - 3.0) < 0.25;
    bool  isSkinOrHair = isMatId2 || isMatId3;

    if (!isSkinOrHair)
#endif
    {
        float4 baseSample   = g_tHdrBaseColor.SampleLevel(g_sBaseColor, uv, 0);
        float3 baseColor    = baseSample.xyz;
        float3 directDiff   = g_tDirectDiffuse.SampleLevel(g_sDirectDiffuse, uv, 0).xyz;
#if TILED_LIGHTS
        float3 directSpec   = g_tDirectSpecular.SampleLevel(g_sDirectSpecular, uv, 0).xyz;
        float3 directTotal  = directDiff + directSpec;
#else
        float3 directTotal  = directDiff;
#endif
        float3 litColor     = baseColor * directTotal;

        float3 secondaryColor = g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;
#if COMPOSITE_HAS_LIGHT
        secondaryColor += g_tAmbientLight.SampleLevel(g_sAmbientLight, uv, 0).xyz;
#endif
#if TILED_LIGHTS && COMPOSITE_TILE_AMBIENT
        secondaryColor += g_tAmbientLightSecondary.SampleLevel(
            g_sAmbientLightSecondary, uv, 0).xyz;
#endif

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

        float3 ambientWeighted = litColor * 3.0;
#if COMPOSITE_MATERIAL_5
        if (abs(matIdByte - 5.0) >= 0.25)
#endif
        {
            ambientWeighted += secondaryColor;
        }
#if COMPOSITE_MODULATION
        float2 modulationUv = min(uv, ModulationUvClamp.xy);
        ambientWeighted *=
            g_tModulation.Sample(g_sModulation, modulationUv).x;
#endif
        float  gray            = dot(ambientWeighted, float3(1.0/3.0, 1.0/3.0, 1.0/3.0));
        float3 graySaturated   = sunlitFogColor + gray * (gray.xxx - sunlitFogColor);

        bool useGraySaturated = (fogMixFactor < FogNearHighColor_and_clamp.w);
        float3 selectedFog =
            useGraySaturated ? graySaturated : sunlitFogColor;

#if COMPOSITE_SCENE_BLEND
        output.color.xyz = lerp(ambientWeighted, selectedFog, fogMixFactor);
#if COMPOSITE_ALPHA_ONE
        output.color.w = 1.0;
#else
        output.color.w = baseSample.w;
#endif
#else
        output.color.xyz = selectedFog;
        output.color.w = fogMixFactor;
#endif
    }
#if COMPOSITE_MATERIAL_EXCLUSION
    else
    {
#if COMPOSITE_ALPHA_ONE
        output.color = float4(0, 0, 0, 1);
#else
        output.color = float4(0, 0, 0, 0);
#endif
    }
#endif

    return output;
}

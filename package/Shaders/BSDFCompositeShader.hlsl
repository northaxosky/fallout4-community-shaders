// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#ifdef TERRAIN_SHADOWS
#include "TerrainShadows/TerrainShadows.hlsli"
#endif
#ifdef WATER_EFFECTS
#include "WaterEffects/WaterCaustics.hlsli"
#endif
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif
#ifdef DYNAMIC_CUBEMAPS
#include "DynamicCubemaps/DynamicCubemaps.hlsli"
#define FO4_SAMPLE_ENVIRONMENT(texture, sampler, direction, lod, hasProbe, slice) \
    DynamicCubemaps::SampleEnvironment(texture, sampler, direction, lod, hasProbe, slice)
#else
#define FO4_SAMPLE_ENVIRONMENT(texture, sampler, direction, lod, hasProbe, slice) \
    texture.SampleLevel(sampler, float4(direction, slice), lod).xyz
#endif
#include "Common/DeferredContracts.hlsli"

#ifdef BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

#ifdef SSGI
#include "ScreenSpaceGI/ScreenSpaceGI.hlsli"
#endif

#ifndef AMBIENT_DIFFUSE_SET_B
#define AMBIENT_DIFFUSE_SET_B 1
#endif
#ifndef AMBIENT_SUBSURFACE_BLUR
#define AMBIENT_SUBSURFACE_BLUR 1
#endif
#ifndef AMBIENT_SSAO
#define AMBIENT_SSAO 1
#endif
#ifndef AMBIENT_UNUSED_TEXCOORD
#define AMBIENT_UNUSED_TEXCOORD 0
#endif
cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
    float4 cb12_pad_28_29[2];
    float4 cb12_idx30_ibl_desaturation;
#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
#endif
};
cbuffer PerCall_CB0 : register(b0)
{
    float4 cb0_idx0_screen_scale_and_blur_tolerance;
    float4 cb0_idx1_lit_scene_weight;
    float4 cb0_idx2_lit_scene_alpha;
};
cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 cb2_pad_1_4[4];
    float4 cb2_idx5_lit_scene_uv_clamp;
};
Texture2D<float4> g_tGbufferNormal : register(t1);
Texture2D<float4> g_tGbufferMaterial : register(t2);
Texture2D<float4> g_tGbufferShadingData : register(t3);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tSkinAuxColor : register(t4);
#endif
Texture2D<float4> g_tAmbientDiffuseA : register(t5);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tAmbientProbeA : register(t6);
#endif
Texture2D<float4> g_tMainDepth : register(t7);
TextureCubeArray<float4> g_tIBLProbeCube : register(t8);
#if AMBIENT_SSAO
Texture2D<float4> g_tSSAO : register(t9);
#endif
Texture2D<float4> g_tBlurSource : register(t10);
#if AMBIENT_DIFFUSE_SET_B
Texture2D<float4> g_tAmbientDiffuseB : register(t11);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tAmbientProbeB : register(t12);
#endif
#endif
Texture2D<float4> g_tLitScene : register(t14);
#if AMBIENT_SUBSURFACE_BLUR
Texture2D<float4> g_tBlurDepthRef : register(t15);
#endif
SamplerState g_sGbufferNormal      : register(s1);
SamplerState g_sGbufferMaterial    : register(s2);
SamplerState g_sGbufferShadingData : register(s3);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sSkinAuxColor       : register(s4);
#endif
SamplerState g_sAmbientDiffuseA    : register(s5);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sAmbientProbeA      : register(s6);
#endif
SamplerState g_sMainDepth          : register(s7);
SamplerState g_sIBLProbeCube       : register(s8);
#if AMBIENT_SSAO
SamplerState g_sSSAO               : register(s9);
#endif
SamplerState g_sBlurSource         : register(s10);
#if AMBIENT_DIFFUSE_SET_B
SamplerState g_sAmbientDiffuseB    : register(s11);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sAmbientProbeB      : register(s12);
#endif
#endif
SamplerState g_sLitScene           : register(s14);
#if AMBIENT_SUBSURFACE_BLUR
SamplerState g_sBlurDepthRef       : register(s15);
#endif
#if AMBIENT_SUBSURFACE_BLUR
static const float2 SSSS_RING_OFFSETS[10] =
{
    float2(-2.000,  -2.000),
    float2(-1.280,  -1.280),
    float2(-0.720,  -0.720),
    float2(-0.320,  -0.320),
    float2(-0.080,  -0.080),
    float2( 0.080,   0.080),
    float2( 0.320,   0.320),
    float2( 0.720,   0.720),
    float2( 1.280,   1.280),
    float2( 2.000,   2.000),
};
static const float3 SSSS_RING_WEIGHTS[10] =
{
    float3(0.0047169099561870098, 0.0001847709936555475, 5.07566e-005),
    float3(0.019283099099993706,  0.0028201800305396318, 0.00084213999798521399),
    float3(0.036390,              0.01309990044683218,   0.006436849944293499),
    float3(0.08219040185213089,   0.035860799252986908,  0.020926099270582199),
    float3(0.077180199325084686,  0.113491,              0.079380303621292114),
    float3(0.077180199325084686,  0.113491,              0.079380303621292114),
    float3(0.08219040185213089,   0.035860799252986908,  0.020926099270582199),
    float3(0.036390,              0.01309990044683218,   0.006436849944293499),
    float3(0.019283099099993706,  0.0028201800305396318, 0.00084213999798521399),
    float3(0.0047169099561870098, 0.0001847709936555475, 5.07565e-005),
};
static const float3 SSSS_CENTER_WEIGHT = float3(0.560479, 0.669086, 0.784728);
#endif
struct PS_INPUT
{
    float4 position : SV_POSITION;
#if AMBIENT_UNUSED_TEXCOORD
    float3 texcoord : TEXCOORD0;
#endif
};
struct PS_OUTPUT
{
    float4 color : SV_Target0;
};
PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        input.position.xy,
        ViewToWorld_row2);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        return output;
    }
#endif
    float3 wetIblColor = float3(0, 0, 0);
    float wetFilmWeight = 0.0;
#endif
    float2 uv = input.position.xy * ScreenSize.xy;
    float3 shadingData    = g_tGbufferShadingData.SampleLevel(g_sGbufferShadingData, uv, 0).xyw;
    float3 ambientA       = g_tAmbientDiffuseA.SampleLevel(g_sAmbientDiffuseA, uv, 0).xyz;
#if AMBIENT_DIFFUSE_SET_B
    float3 ambientB       = g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz;
    float3 ambientPairSum = (ambientA + ambientB) * 3.0;
#else
    float3 ambientPairSum = ambientA * 3.0;
#endif
    float depth = g_tMainDepth.SampleLevel(g_sMainDepth, uv, 0).x;
    bool isNearPath = (depth <= 0.01);
    float4 pos;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    if (isNearPath)
    {
        pos.z = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        pos.z = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }
#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float2 terrainScreen = float2(
        uv.x * ScreenSize.z,
        1.0 - uv.y * ScreenSize.w);
    float4 terrainPosition = float4(terrainScreen * 2.0 - 1.0, pos.z, 1.0);
    float4 terrainPositionViewH = float4(
        dot(reprojRow0, terrainPosition),
        dot(reprojRow1, terrainPosition),
        dot(reprojRow2, terrainPosition),
        dot(reprojRow3, terrainPosition));
    float3 positionView = terrainPositionViewH.xyz / terrainPositionViewH.w;
#endif
#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            positionView,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        return output;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            positionView,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            waterDebugColor))
    {
        output.color = waterDebugColor;
        return output;
    }
#endif
    float3 blurSourceCenter = g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;
    float yTripled    = shadingData.y * 3.0;
    float rough01     = saturate(shadingData.x - 0.3);
    float roughFactor = min(1.0 / rsqrt(rough01), 1.0);
    float glossFactor = yTripled * roughFactor;
    float4 matRaw = g_tGbufferMaterial.SampleLevel(g_sGbufferMaterial, uv, 0);
    float matSliceFloat = matRaw.y;
    float matGlossOrSpec = matRaw.z;
    float glossSquaredScaled = matGlossOrSpec * matGlossOrSpec * 50.0;
    bool  hasIBL = (matSliceFloat > 0.5 / 255.0);
    float3 iblColor = float3(0, 0, 0);
    if (hasIBL)
    {
        float2 enc = g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy * 4.0 - 2.0;
        float  encDotEnc = dot(enc, enc);
        float  zRecon = 1.0 - encDotEnc * 0.25;
        float3 normalView = float3(enc * sqrt(zRecon), -(1.0 - encDotEnc * 0.5));
        float2 sn = float2(uv.x * ScreenSize.z, 1.0 - uv.y * ScreenSize.w);
        pos.xy = sn * 2.0 - 1.0;
        pos.w  = 1.0;
        float3 posViewXYZ = float3(dot(reprojRow0, pos),
                                   dot(reprojRow1, pos),
                                   dot(reprojRow2, pos));
        float  posViewW   = dot(reprojRow3, pos);
        pos.xyz = posViewXYZ / posViewW;
        float3 viewDirNeg = normalize(-pos.xyz);
        float  ndotv2     = dot(viewDirNeg, normalView);
        ndotv2 = ndotv2 + ndotv2;
        float3 reflView   = normalView * -ndotv2 + viewDirNeg;
        float3 reflWorld;
        reflWorld.x = dot(ViewToWorld_row0.xyz, reflView);
        reflWorld.y = dot(ViewToWorld_row1.xyz, reflView);
        reflWorld.z = dot(ViewToWorld_row2.xyz, reflView);
        float mipLevel = (1.0 - shadingData.x) * 6.0;
        mipLevel = pos.z * 0.001953125 + mipLevel;
        float arraySlice = floor(matSliceFloat * 255.0 - 1.0);
        float3 cubeSample = FO4_SAMPLE_ENVIRONMENT(
            g_tIBLProbeCube,
            g_sIBLProbeCube,
            reflWorld,
            mipLevel,
            hasIBL,
            arraySlice);
        float  luma   = dot(cubeSample, float3(0.299, 0.587, 0.114));
        float  desatW = cb12_idx30_ibl_desaturation.y * 0.9;
        iblColor      = lerp(cubeSample, luma.xxx, desatW);
#ifdef WETNESS_EFFECTS
        float3 wetReflView = WetnessEffects::GetFilmReflectionView(
            wetSurface.normalView, viewDirNeg);
        float3 wetReflWorld;
        wetReflWorld.x = dot(ViewToWorld_row0.xyz, wetReflView);
        wetReflWorld.y = dot(ViewToWorld_row1.xyz, wetReflView);
        wetReflWorld.z = dot(ViewToWorld_row2.xyz, wetReflView);
        float wetMipLevel = WetnessEffects::GetFilmMipRoughness(
            1.0 - shadingData.x, wetSurface.wetness) * 6.0;
        wetMipLevel = pos.z * 0.001953125 + wetMipLevel;
        float3 wetCubeSample = FO4_SAMPLE_ENVIRONMENT(
            g_tIBLProbeCube,
            g_sIBLProbeCube,
            wetReflWorld,
            wetMipLevel,
            hasIBL,
            arraySlice);
        float  wetLuma = dot(wetCubeSample, float3(0.299, 0.587, 0.114));
        wetIblColor = lerp(wetCubeSample, wetLuma.xxx, desatW);
        wetFilmWeight = WetnessEffects::GetEnvironmentFilmWeight(
            wetSurface.normalView, viewDirNeg, wetSurface.wetness);
#endif
    }
    else
    {
        iblColor = float3(0, 0, 0);
    }
    float2 uvClamped     = min(uv, cb2_idx5_lit_scene_uv_clamp.xy);
    float4 litRaw        = g_tLitScene.Sample(g_sLitScene, uvClamped);
    float  litAlpha      = min(litRaw.w * cb0_idx2_lit_scene_alpha.z, 1.0);
    float3 iblLitBlend   = lerp(iblColor,
                                 litRaw.xyz * cb0_idx1_lit_scene_weight.x,
                                 litAlpha);
#ifdef WETNESS_EFFECTS
    float3 wetIblLitBlend = lerp(wetIblColor,
                                 litRaw.xyz * cb0_idx1_lit_scene_weight.x,
                                 litAlpha);
#endif
    bool isSkin = (abs(shadingData.z * 255.0 - 5.0) < 0.25);
    float3 ambientAccum;
#if AMBIENT_SUBSURFACE_BLUR
    if (isSkin)
    {
        float3 skinAux = g_tSkinAuxColor.Sample(g_sSkinAuxColor, uv).xyz;
        float  depthMaskF = isNearPath ? 1.0 : 0.0;
        float  blurDepthScale = depthMaskF * cb0_idx0_screen_scale_and_blur_tolerance.z + 1.0;
        float  refDepth = g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, uv, 0).x;
        float  centerRef = blurDepthScale * refDepth;
        float2 tapBase = cb0_idx0_screen_scale_and_blur_tolerance.xx
                       * float2(0.078125, 0.138890)
                       / centerRef;
        float3 blurAccum = SSSS_CENTER_WEIGHT * blurSourceCenter;
        [unroll]
        for (int i = 0; i < 10; ++i)
        {
            float2 tapUV = uv + tapBase * SSSS_RING_OFFSETS[i];
            float  tapMatId = g_tGbufferShadingData.SampleLevel(
                                   g_sGbufferShadingData, tapUV, 0).w * 255.0 - 5.0;
            float3 tapBlended;
            if (abs(tapMatId) < 0.25)
            {
                float3 tapColor = g_tBlurSource.SampleLevel(
                                       g_sBlurSource, tapUV, 0).xyz;
                float  tapDepth = g_tBlurDepthRef.SampleLevel(
                                       g_sBlurDepthRef, tapUV, 0).x;
                float  tol = cb0_idx0_screen_scale_and_blur_tolerance.y * 0.1;
                float  dt = min(tol * abs(-tapDepth * blurDepthScale + centerRef), 1.0);
                tapBlended = lerp(tapColor, blurSourceCenter, dt);
            }
            else
            {
                tapBlended = blurSourceCenter;
            }
            blurAccum += tapBlended * SSSS_RING_WEIGHTS[i];
        }
        float3 probeA = g_tAmbientProbeA.SampleLevel(g_sAmbientProbeA, uv, 0).xyz;
#if AMBIENT_DIFFUSE_SET_B
        float3 probeB = g_tAmbientProbeB.SampleLevel(g_sAmbientProbeB, uv, 0).xyz;
        ambientAccum  = blurAccum + (probeA + probeB + skinAux);
#else
        ambientAccum  = blurAccum + (probeA + skinAux);
#endif
    }
    else
    {
        ambientAccum = blurSourceCenter;
    }
#else
    ambientAccum = blurSourceCenter;
#endif
#ifdef WETNESS_EFFECTS
    float3 spec = lerp(iblLitBlend, wetIblLitBlend, wetFilmWeight);
#else
    float3 spec = iblLitBlend;
#endif
    spec *= glossFactor;
    spec *= glossSquaredScaled;
    float3 modulated = spec * ambientPairSum + ambientAccum;
#if AMBIENT_SSAO
    float aoFactor = g_tSSAO.Sample(g_sSSAO, uvClamped).x;
#else
    const float aoFactor = 1.0;
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
#ifdef WETNESS_EFFECTS
    float3 dynamicSpec = lerp(iblColor, wetIblColor, wetFilmWeight);
#else
    float3 dynamicSpec = iblColor;
#endif
    float3 dynamicReflectionContribution =
        dynamicSpec * (1.0 - litAlpha) * glossFactor *
        glossSquaredScaled * ambientPairSum * aoFactor;
#endif
#ifdef SSGI
    float3 ssgiViewNormal = ScreenSpaceGI::DecodeViewNormal(
        g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy);
    output.color.xyz = ScreenSpaceGI::ComposeAmbient(
        input.position.xy,
        ssgiViewNormal,
        float3x3(ViewToWorld_row0.xyz, ViewToWorld_row1.xyz, ViewToWorld_row2.xyz),
        spec * ambientPairSum,
        ambientAccum,
        aoFactor
#ifdef WETNESS_EFFECTS
        , wetSurface.wetness
#endif
        );
#else
    output.color.xyz = modulated * aoFactor;
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
    output.color.xyz = DynamicCubemaps::ApplyFullscreenDebug(
        output.color.xyz, dynamicReflectionContribution);
#endif
    output.color.w = 1.0;
    return output;
}
#endif

#ifdef BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif
#ifdef EXPONENTIAL_HEIGHT_FOG
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#endif

#ifdef SSGI
#include "ScreenSpaceGI/ScreenSpaceGI.hlsli"
#endif

#ifndef FO4_AMBIENT_OCCLUSION
#define FO4_AMBIENT_OCCLUSION 1
#endif
#ifndef FO4_SKIN_BLUR
#define FO4_SKIN_BLUR 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
    float4 cb12_pad_28_29[2];
    float4 IblDesaturation;
    float4 cb12_pad_31_34[4];
    float4 CameraPosAdjust;
    float4 CameraPreviousPosAdjust;
    float4 cb12_pad_37_40[4];
    float4 FogDistanceRamp;
    float4 FogNearLowColorAndPower;
    float4 FogNearHighColorAndClamp;
    float4 FogFarLowColorAndHighDensityScale;
    float4 FogFarHighColor;
    float4 FogHeightRamp;
};

cbuffer PerCall_CB0 : register(b0)
{
    float4 ScreenBlurParameters;
    float4 LitSceneWeight;
    float4 LitSceneAlpha;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;
    float4 SunDirectionAndIntensity;
    float4 SunColorAndSpecPower;
    float4 cb2_pad_3_4[2];
    float4 LitSceneUvClamp;
};

Texture2D<float4> g_tGbufferNormal       : register(t1);
Texture2D<float4> g_tGbufferMaterial     : register(t2);
Texture2D<float4> g_tGbufferShadingData  : register(t3);
#if FO4_SKIN_BLUR
Texture2D<float4> g_tSkinAuxColor        : register(t4);
#endif
Texture2D<float4> g_tAmbientDiffuseA     : register(t5);
#if FO4_SKIN_BLUR
Texture2D<float4> g_tAmbientProbeA       : register(t6);
#endif
Texture2D<float4> g_tMainDepth           : register(t7);
TextureCubeArray<float4> g_tIblProbeCube : register(t8);
#if FO4_AMBIENT_OCCLUSION
Texture2D<float4> g_tSsao                : register(t9);
#endif
Texture2D<float4> g_tBlurSource          : register(t10);
#ifdef TILELIGHT
Texture2D<float4> g_tAmbientDiffuseB     : register(t11);
#if FO4_SKIN_BLUR
Texture2D<float4> g_tBlurredSslr         : register(t12);
#endif
#endif
Texture2D<float4> g_tLitScene            : register(t14);
#if FO4_SKIN_BLUR
Texture2D<float4> g_tBlurDepthRef        : register(t15);
#endif

SamplerState g_sGbufferNormal      : register(s1);
SamplerState g_sGbufferMaterial    : register(s2);
SamplerState g_sGbufferShadingData : register(s3);
#if FO4_SKIN_BLUR
SamplerState g_sSkinAuxColor       : register(s4);
#endif
SamplerState g_sAmbientDiffuseA    : register(s5);
#if FO4_SKIN_BLUR
SamplerState g_sAmbientProbeA      : register(s6);
#endif
SamplerState g_sMainDepth          : register(s7);
SamplerState g_sIblProbeCube       : register(s8);
#if FO4_AMBIENT_OCCLUSION
SamplerState g_sSsao               : register(s9);
#endif
SamplerState g_sBlurSource         : register(s10);
#ifdef TILELIGHT
SamplerState g_sAmbientDiffuseB    : register(s11);
#if FO4_SKIN_BLUR
SamplerState g_sBlurredSslr        : register(s12);
#endif
#endif
SamplerState g_sLitScene           : register(s14);
#if FO4_SKIN_BLUR
SamplerState g_sBlurDepthRef       : register(s15);
#endif

#if FO4_SKIN_BLUR
static const float2 SSSS_RING_OFFSETS[10] =
{
    float2(-2.000, -2.000),
    float2(-1.280, -1.280),
    float2(-0.720, -0.720),
    float2(-0.320, -0.320),
    float2(-0.080, -0.080),
    float2( 0.080,  0.080),
    float2( 0.320,  0.320),
    float2( 0.720,  0.720),
    float2( 1.280,  1.280),
    float2( 2.000,  2.000),
};

static const float3 SSSS_RING_WEIGHTS[10] =
{
    float3(0.00471690995618701, 0.0001847709936555475, 0.00005075660010334104),
    float3(0.019283099099993706, 0.002820180030539632, 0.000842139997985214),
    float3(0.036389999091625214, 0.01309990044683218, 0.006436849944293499),
    float3(0.08219040185213089, 0.03586079925298691, 0.0209260992705822),
    float3(0.07718019932508469, 0.1134909987449646, 0.07938030362129211),
    float3(0.07718019932508469, 0.1134909987449646, 0.07938030362129211),
    float3(0.08219040185213089, 0.03586079925298691, 0.0209260992705822),
    float3(0.036389999091625214, 0.01309990044683218, 0.006436849944293499),
    float3(0.019283099099993706, 0.002820180030539632, 0.000842139997985214),
    float3(0.00471690995618701, 0.0001847709936555475, 0.000050756498239934444),
};

static const float3 SSSS_CENTER_WEIGHT =
    float3(0.560479, 0.669086, 0.784728);
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
#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        input.position.xy,
        ViewToWorld_row2);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        return output;
    }
#endif
    float3 wetIblColor = float3(0.0, 0.0, 0.0);
    float wetFilmWeight = 0.0;
#endif
    float2 uv = input.position.xy * ScreenSize.xy;

    float3 shadingData =
        g_tGbufferShadingData.SampleLevel(g_sGbufferShadingData, uv, 0).xyw;
    float depth = g_tMainDepth.SampleLevel(g_sMainDepth, uv, 0).x;
    bool isNearPath = 0.01 >= depth;
    float linearizedDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
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

    float4 uvRemapped =
        float4(uv.x, 0.0, -uv.y, 0.0)
        * float4(ScreenSize.z, 0.0, ScreenSize.w, 0.0);
    uvRemapped.z += 1.0;
    float4 positionInput =
        float4(uvRemapped.xz * 2.0 - 1.0, linearizedDepth, 1.0);
    float4 positionViewH;
    positionViewH.x = dot(reprojRow0, positionInput);
    positionViewH.y = dot(reprojRow1, positionInput);
    positionViewH.z = dot(reprojRow2, positionInput);
    positionViewH.w = dot(reprojRow3, positionInput);
    float3 positionView = positionViewH.xyz / positionViewH.www;

#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            positionView,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        return output;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            positionView,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            waterDebugColor))
    {
        output.color = waterDebugColor;
        return output;
    }
#endif

#if FO4_SKIN_BLUR
    float3 blurSourceCenter =
        g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;
#else
    float3 blurSourceCenter;
#endif
    float4 material =
        g_tGbufferMaterial.SampleLevel(g_sGbufferMaterial, uv, 0);
    bool hasIbl = material.y > (0.5 / 255.0);
    float3 iblColor = float3(0.0, 0.0, 0.0);
    if (hasIbl)
    {
        float2 encodedNormal =
            g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy * 4.0 - 2.0;
        float encodedLengthSquared = dot(encodedNormal, encodedNormal);
        float3 normalView = float3(
            encodedNormal * sqrt(1.0 - encodedLengthSquared * 0.25),
            -(1.0 - encodedLengthSquared * 0.5));
        float3 negativePositionView = -positionView;
        float3 viewDirection =
            negativePositionView *
            rsqrt(dot(negativePositionView, negativePositionView));
        float reflectionScale = 2.0 * dot(viewDirection, normalView);
        float3 reflectionView =
            normalView * -reflectionScale + viewDirection;
        float3 reflectionWorld;
        reflectionWorld.x = dot(ViewToWorld_row0.xyz, reflectionView);
        reflectionWorld.y = dot(ViewToWorld_row1.xyz, reflectionView);
        reflectionWorld.z = dot(ViewToWorld_row2.xyz, reflectionView);

        float mipLevel = (1.0 - shadingData.x) * 6.0;
        mipLevel = positionView.z * 0.001953125 + mipLevel;
        float arraySlice = floor(material.y * 255.0 - 1.0);
        float3 cubeSample = FO4_SAMPLE_ENVIRONMENT(
            g_tIblProbeCube,
            g_sIblProbeCube,
            reflectionWorld,
            mipLevel,
            hasIbl,
            arraySlice);
        float luminance = dot(cubeSample, float3(0.299, 0.587, 0.114));
        iblColor = lerp(
            cubeSample, luminance.xxx, IblDesaturation.y * 0.9);
#ifdef WETNESS_EFFECTS
        float3 wetReflectionView = WetnessEffects::GetFilmReflectionView(
            wetSurface.normalView, viewDirection);
        float3 wetReflectionWorld;
        wetReflectionWorld.x = dot(ViewToWorld_row0.xyz, wetReflectionView);
        wetReflectionWorld.y = dot(ViewToWorld_row1.xyz, wetReflectionView);
        wetReflectionWorld.z = dot(ViewToWorld_row2.xyz, wetReflectionView);
        float wetMipLevel = WetnessEffects::GetFilmMipRoughness(
            1.0 - shadingData.x, wetSurface.wetness) * 6.0;
        wetMipLevel = positionView.z * 0.001953125 + wetMipLevel;
        float3 wetCubeSample = FO4_SAMPLE_ENVIRONMENT(
            g_tIblProbeCube,
            g_sIblProbeCube,
            wetReflectionWorld,
            wetMipLevel,
            hasIbl,
            arraySlice);
        float wetLuminance = dot(wetCubeSample, float3(0.299, 0.587, 0.114));
        wetIblColor = lerp(
            wetCubeSample, wetLuminance.xxx, IblDesaturation.y * 0.9);
        wetFilmWeight = WetnessEffects::GetEnvironmentFilmWeight(
            wetSurface.normalView, viewDirection, wetSurface.wetness);
#endif
    }

    float materialId = shadingData.z * 255.0;
#if FO4_SKIN_BLUR
    bool isMaterial5 = abs(materialId - 5.0) < 0.25;
#endif
    bool isMaterial2 = abs(materialId - 2.0) < 0.25;
    bool isMaterial3 = abs(materialId - 3.0) < 0.25;

#if FO4_SKIN_BLUR
    float3 ambientAccum = blurSourceCenter;
    if (isMaterial5)
    {
        float3 skinAux =
            g_tSkinAuxColor.Sample(g_sSkinAuxColor, uv).xyz;
        float blurDepthScale =
            (isNearPath ? 1.0 : 0.0) * ScreenBlurParameters.z + 1.0;
        float centerRef =
            g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, uv, 0).x *
            blurDepthScale;
        float2 tapBase =
            ScreenBlurParameters.xx * float2(0.078125, 0.138890) / centerRef;
        float3 blurAccum = blurSourceCenter * SSSS_CENTER_WEIGHT;
        [unroll]
        for (int i = 0; i < 10; ++i)
        {
            float2 tapUv = uv + tapBase * SSSS_RING_OFFSETS[i];
            float tapMaterialId = g_tGbufferShadingData.SampleLevel(
                g_sGbufferShadingData, tapUv, 0).w * 255.0 - 5.0;
            float3 tapColor = blurSourceCenter;
            if (abs(tapMaterialId) < 0.25)
            {
                float3 sampledColor =
                    g_tBlurSource.SampleLevel(g_sBlurSource, tapUv, 0).xyz;
                float tapDepth =
                    g_tBlurDepthRef.SampleLevel(g_sBlurDepthRef, tapUv, 0).x;
                float blurDepthWeightScale = ScreenBlurParameters.y * 0.1;
                float depthWeight = min(
                    blurDepthWeightScale *
                        abs(-tapDepth * blurDepthScale + centerRef),
                    1.0);
                tapColor = lerp(sampledColor, blurSourceCenter, depthWeight);
            }
            blurAccum += tapColor * SSSS_RING_WEIGHTS[i];
        }

        float3 probeA =
            g_tAmbientProbeA.SampleLevel(g_sAmbientProbeA, uv, 0).xyz;
#ifdef TILELIGHT
        float3 blurredSslr =
            g_tBlurredSslr.SampleLevel(g_sBlurredSslr, uv, 0).xyz;
        float3 skinLighting = probeA + blurredSslr + skinAux;
#else
        float3 skinLighting = probeA + skinAux;
#endif
        ambientAccum = blurAccum + skinLighting;
    }
#else
    float3 ambientAccum;
#endif

    if (!(isMaterial2 || isMaterial3))
    {
    float3 ambientPair =
        g_tAmbientDiffuseA.SampleLevel(g_sAmbientDiffuseA, uv, 0).xyz;
#ifdef TILELIGHT
    ambientPair +=
        g_tAmbientDiffuseB.SampleLevel(g_sAmbientDiffuseB, uv, 0).xyz;
#endif
    ambientPair *= 3.0;
#if !FO4_SKIN_BLUR
    blurSourceCenter =
        g_tBlurSource.SampleLevel(g_sBlurSource, uv, 0).xyz;
    ambientAccum = blurSourceCenter;
#endif
    float glossFactor =
        shadingData.y * 3.0 *
        min(1.0, 1.0 / rsqrt(saturate(shadingData.x - 0.3)));
    float glossSquaredScaled = material.z * material.z * 50.0;
    float2 litSceneUv = min(uv, LitSceneUvClamp.xy);
    float4 litScene = g_tLitScene.Sample(g_sLitScene, litSceneUv);
    float litAlpha = min(litScene.w * LitSceneAlpha.z, 1.0);
    float3 iblLitBlend = lerp(
        iblColor, litScene.xyz * LitSceneWeight.x, litAlpha);
#ifdef WETNESS_EFFECTS
    float3 wetIblLitBlend = lerp(
        wetIblColor, litScene.xyz * LitSceneWeight.x, litAlpha);
    float3 reflectionBlend = lerp(iblLitBlend, wetIblLitBlend, wetFilmWeight);
#ifdef SSGI
    float3 directComponent =
        glossFactor * reflectionBlend * glossSquaredScaled * ambientPair;
    float3 ambientComponent = ambientAccum;
#else
    float3 modulated =
        ambientAccum + glossFactor * reflectionBlend * glossSquaredScaled * ambientPair;
#endif
#else
#ifdef SSGI
    float3 directComponent =
        glossFactor * iblLitBlend * glossSquaredScaled * ambientPair;
    float3 ambientComponent = ambientAccum;
#else
    float3 modulated =
        ambientAccum + glossFactor * iblLitBlend * glossSquaredScaled * ambientPair;
#endif
#endif
#if FO4_AMBIENT_OCCLUSION
    float ao = g_tSsao.Sample(g_sSsao, litSceneUv).x;
#else
    const float ao = 1.0;
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
#ifdef WETNESS_EFFECTS
    float3 dynamicReflectionBlend =
        lerp(iblColor, wetIblColor, wetFilmWeight);
#else
    float3 dynamicReflectionBlend = iblColor;
#endif
    float3 dynamicReflectionContribution =
        dynamicReflectionBlend * (1.0 - litAlpha) * glossFactor *
        glossSquaredScaled * ambientPair * ao;
#endif
#ifdef SSGI
    float3 ssgiViewNormal = ScreenSpaceGI::DecodeViewNormal(
        g_tGbufferNormal.SampleLevel(g_sGbufferNormal, uv, 0).xy);
    float3 aoColor = ScreenSpaceGI::ComposeAmbient(
        input.position.xy,
        ssgiViewNormal,
        float3x3(ViewToWorld_row0.xyz, ViewToWorld_row1.xyz, ViewToWorld_row2.xyz),
        directComponent,
        ambientComponent,
        ao
#ifdef WETNESS_EFFECTS
        , wetSurface.wetness
#endif
        );
#else
    float3 aoColor = modulated * ao;
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
    aoColor = DynamicCubemaps::ApplyFullscreenDebug(
        aoColor, dynamicReflectionContribution);
#endif

    float fogPlaneDistance =
        dot(ViewToWorld_row2, float4(positionView, 1.0)) + CameraPosAdjust.z;
    float positionLengthSquared = dot(positionView, positionView);
    float distanceRamp =
        sqrt(positionLengthSquared) * FogDistanceRamp.x - FogDistanceRamp.z;
    float distanceFactor = saturate(distanceRamp);
    float2 fogRemapPair =
        saturate(fogPlaneDistance.xx * FogHeightRamp.xy - FogHeightRamp.zw);
#ifdef EXPONENTIAL_HEIGHT_FOG
    float exponentialDistance;
    float2 exponentialHeight;
    bool exponentialValid = ExponentialHeightFog::TryEvaluate(
        sqrt(positionLengthSquared),
        fogPlaneDistance,
        FogDistanceRamp,
        FogHeightRamp,
        exponentialDistance,
        exponentialHeight);
    if (exponentialValid)
    {
        fogRemapPair = exponentialHeight;
    }
#endif
    float fogBlend = lerp(
        fogRemapPair.x, fogRemapPair.y, distanceFactor);

    float fogIntensityClamp;
    if (distanceRamp > 0.75)
    {
        float distanceTail = (distanceFactor - 0.75) * 4.0;
        fogIntensityClamp = min(
            distanceTail * (1.0 - FogNearHighColorAndClamp.w) +
                FogNearHighColorAndClamp.w,
            1.0);
    }
    else
    {
        fogIntensityClamp = FogNearHighColorAndClamp.w;
    }

    float nearEscape =
        distanceRamp < 0.015 ? distanceFactor * 66.666672 : 1.0;
    float distancePow = pow(distanceFactor, FogNearLowColorAndPower.w);
#ifdef EXPONENTIAL_HEIGHT_FOG
    if (exponentialValid)
    {
        distancePow = exponentialDistance;
    }
#endif
    float fogIntensity = min(distancePow, fogIntensityClamp);
    float unfoggedWeight = 1.0 - fogBlend;
    float fogBlendWeight = mad(
        fogBlend,
        FogFarLowColorAndHighDensityScale.w,
        unfoggedWeight);
    float3 fogColorLow = lerp(
        FogNearLowColorAndPower.xyz,
        FogFarLowColorAndHighDensityScale.xyz,
        fogIntensity);
    float3 fogColorHigh = lerp(
        FogNearHighColorAndClamp.xyz,
        FogFarHighColor.xyz,
        fogIntensity);
    float3 fogColor = lerp(fogColorLow, fogColorHigh, fogBlend);
    float combinedFog = fogIntensity * fogBlendWeight;
    float fogMixFactor = combinedFog * nearEscape;

    float3 viewDirection =
        positionView * rsqrt(positionLengthSquared);
    float sunFactor = pow(
        max(dot(viewDirection, SunDirectionAndIntensity.xyz), 0.0),
        SunColorAndSpecPower.w) * SunDirectionAndIntensity.w;
    float3 sunlitFog =
        lerp(fogColor, SunColorAndSpecPower.xyz, sunFactor);
    bool useGrayscale =
        fogMixFactor < FogNearHighColorAndClamp.w;
    float grayscale = dot(aoColor, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    float3 grayscaleStack =
        sunlitFog + grayscale * (grayscale.xxx - sunlitFog);
    float3 colorStack = useGrayscale ? grayscaleStack : sunlitFog;

    output.color.xyz = lerp(aoColor, colorStack, fogMixFactor);
    output.color.w = 1.0;
#ifdef EXPONENTIAL_HEIGHT_FOG
    if (ExponentialHeightFog::IsFogFactorDebug())
    {
        output.color = float4(fogMixFactor.xxx, 1.0);
    }
#endif
    }
    else
    {
        output.color = float4(0.0, 0.0, 0.0, 0.0);
    }
    return output;
}

#endif

#ifdef BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

#ifdef EXPONENTIAL_HEIGHT_FOG
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#endif

#ifdef SSGI
#include "ScreenSpaceGI/ScreenSpaceGI.hlsli"
#endif

#ifndef TILELIGHT
#define TILELIGHT 0
#endif
#ifndef FOGSTACK
#define FOGSTACK 0
#endif
#ifndef OUTPUTMASK
#define OUTPUTMASK 0
#endif

#if FOGSTACK || defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
#define AMBIENT_FRAME_COUNT 47
#else
#define AMBIENT_FRAME_COUNT 31
#endif

#if OUTPUTMASK
#define SCREEN_SETUP_COUNT 6
#elif FOGSTACK
#define SCREEN_SETUP_COUNT 3
#else
#define SCREEN_SETUP_COUNT 1
#endif

cbuffer AmbientFrame : register(b12)
{
    float4 ambientFrame[AMBIENT_FRAME_COUNT];
};

cbuffer AmbientPass : register(b0)
{
    float4 ambientPass[1];
};

cbuffer ScreenSetup : register(b2)
{
    float4 screenSetup[SCREEN_SETUP_COUNT];
};

SamplerState normalSampler      : register(s1);
SamplerState materialSampler    : register(s2);
SamplerState surfaceSampler     : register(s3);
SamplerState skinAuxSampler     : register(s4);
SamplerState lightingASampler   : register(s5);
SamplerState skinLightSampler   : register(s6);
SamplerState depthSampler       : register(s7);
SamplerState environmentSampler : register(s8);
#if OUTPUTMASK
SamplerState outputMaskSampler  : register(s9);
#endif
SamplerState ambientBaseSampler : register(s10);
#if TILELIGHT
SamplerState lightingBSampler   : register(s11);
SamplerState skinProbeSampler   : register(s12);
#endif
SamplerState linearDepthSampler : register(s15);

Texture2D<float4>        normalTexture      : register(t1);
Texture2D<float4>        materialTexture    : register(t2);
Texture2D<float4>        surfaceTexture     : register(t3);
Texture2D<float4>        skinAuxTexture     : register(t4);
Texture2D<float4>        lightingATexture   : register(t5);
Texture2D<float4>        skinLightTexture   : register(t6);
Texture2D<float4>        depthTexture       : register(t7);
TextureCubeArray<float4> environmentTexture : register(t8);
#if OUTPUTMASK
Texture2D<float4>        outputMaskTexture  : register(t9);
#endif
Texture2D<float4>        ambientBaseTexture : register(t10);
#if TILELIGHT
Texture2D<float4>        lightingBTexture   : register(t11);
Texture2D<float4>        skinProbeTexture   : register(t12);
#endif
Texture2D<float4>        linearDepthTexture : register(t15);

float3 sampleSkinTap(float2 coordinate, float depthScale, float centerDepth, float3 centerColor)
{
    float materialId = surfaceTexture.SampleLevel(surfaceSampler, coordinate, 0.0).w;
    float3 tapColor;

    if (abs(materialId * 255.0 - 5.0) < 0.25)
    {
        tapColor = ambientBaseTexture.SampleLevel(ambientBaseSampler, coordinate, 0.0).xyz;
        float tapDepth = linearDepthTexture.SampleLevel(linearDepthSampler, coordinate, 0.0).x;
        float depthWeightScale = ambientPass[0].y * 0.1;
        float depthWeight = min(depthWeightScale * abs(centerDepth - tapDepth * depthScale), 1.0);
        tapColor = lerp(tapColor, centerColor, depthWeight);
    }
    else
    {
        tapColor = centerColor;
    }

    return tapColor;
}

float3 reconstructViewPosition(float2 coordinate, float linearizedDepth,
                               float4 row0, float4 row1, float4 row2, float4 row3)
{
    float2 clipPosition = float2(
        coordinate.x * screenSetup[0].z,
        1.0 - coordinate.y * screenSetup[0].w
    ) * 2.0 - 1.0;
    float4 projectedPosition = float4(clipPosition, linearizedDepth, 1.0);
    float4 viewPosition;
    viewPosition.x = dot(row0, projectedPosition);
    viewPosition.y = dot(row1, projectedPosition);
    viewPosition.z = dot(row2, projectedPosition);
    viewPosition.w = dot(row3, projectedPosition);
    return viewPosition.xyz / viewPosition.w;
}

float3 sampleDirectLighting(float2 coordinate)
{
    float3 directLighting = lightingATexture.SampleLevel(lightingASampler, coordinate, 0.0).xyz;
#if TILELIGHT
    directLighting += lightingBTexture.SampleLevel(lightingBSampler, coordinate, 0.0).xyz;
#endif
    return directLighting * 3.0;
}

float3 composeAmbient(float2 coordinate, float3 directLighting, float glossFactor,
                      float gloss, float3 environment, float3 centerColor
#ifdef SSGI
                      , float2 screenPosition, float3 viewNormal
#endif
#ifdef WETNESS_EFFECTS
                      , float3 wetEnvironment, float wetFilmWeight, float wetness
#endif
                      )
{
#ifdef WETNESS_EFFECTS
    float3 reflectionBlend = lerp(environment, wetEnvironment, wetFilmWeight);
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
#ifdef WETNESS_EFFECTS
    float3 dynamicReflectionContribution =
        reflectionBlend * glossFactor * gloss * directLighting;
#else
    float3 dynamicReflectionContribution =
        environment * glossFactor * gloss * directLighting;
#endif
#endif
#ifdef SSGI
    // this family carries no engine ambient-occlusion texture
    float3 color = ScreenSpaceGI::ComposeAmbient(
        screenPosition,
        viewNormal,
        float3x3(ambientFrame[12].xyz, ambientFrame[13].xyz, ambientFrame[14].xyz),
#ifdef WETNESS_EFFECTS
        reflectionBlend * glossFactor * gloss * directLighting,
#else
        environment * glossFactor * gloss * directLighting,
#endif
        centerColor,
        1.0
#ifdef WETNESS_EFFECTS
        , wetness
#endif
        );
#else
#ifdef WETNESS_EFFECTS
    float3 color = reflectionBlend * glossFactor;
#else
    float3 color = environment * glossFactor;
#endif
    color *= gloss;
    color = color * directLighting + centerColor;
#endif
#if OUTPUTMASK
    float outputMask =
        outputMaskTexture.Sample(
            outputMaskSampler, min(coordinate, screenSetup[5].xy)).x;
    color *= outputMask;
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
    dynamicReflectionContribution *= outputMask;
#endif
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
    color = DynamicCubemaps::ApplyFullscreenDebug(
        color, dynamicReflectionContribution);
#endif
    return color;
}

float4 main(float4 position : SV_POSITION) : SV_Target0
{
    float2 coordinate = position.xy * screenSetup[0].xy;
#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        position.xy,
        ambientFrame[14]);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
        return wetnessDebugColor;
#endif
    float3 wetEnvironment = 0.0;
    float wetFilmWeight = 0.0;
#endif
#ifdef SSGI
    // the composition needs the view normal outside the environment branch
    float3 viewNormal = ScreenSpaceGI::DecodeViewNormal(
        normalTexture.SampleLevel(normalSampler, coordinate, 0.0).xy);
#endif
    float3 surface = surfaceTexture.SampleLevel(surfaceSampler, coordinate, 0.0).xyw;
#if !FOGSTACK

    float3 directLighting = sampleDirectLighting(coordinate);
#endif

    float deviceDepth = depthTexture.SampleLevel(depthSampler, coordinate, 0.0).x;
    bool nearDepth = deviceDepth <= 0.01;
    float linearizedDepth;
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;

    if (nearDepth)
    {
        linearizedDepth = deviceDepth * 100.0;
        row0 = ambientFrame[24];
        row1 = ambientFrame[25];
        row2 = ambientFrame[26];
        row3 = ambientFrame[27];
    }
    else
    {
        linearizedDepth = deviceDepth * 1.01 - 0.01;
        row0 = ambientFrame[20];
        row1 = ambientFrame[21];
        row2 = ambientFrame[22];
        row3 = ambientFrame[23];
    }

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float3 terrainViewPosition =
        reconstructViewPosition(coordinate, linearizedDepth, row0, row1, row2, row3);
#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            terrainViewPosition,
            TerrainShadows::TerrainShadowsSampler,
            ambientFrame[12],
            ambientFrame[13],
            ambientFrame[14],
            ambientFrame[35],
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            terrainViewPosition,
            ambientFrame[12],
            ambientFrame[13],
            ambientFrame[14],
            ambientFrame[35],
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
#endif

#if FOGSTACK

    float3 viewPosition = reconstructViewPosition(coordinate, linearizedDepth, row0, row1, row2, row3);
#endif

    float3 centerColor = ambientBaseTexture.SampleLevel(ambientBaseSampler, coordinate, 0.0).xyz;
#if !FOGSTACK
    float glossFactor = surface.y * 3.0;
    float roughFactor = min(1.0 / rsqrt(saturate(surface.x - 0.3)), 1.0);
    glossFactor *= roughFactor;
#endif
    float2 material = materialTexture.SampleLevel(materialSampler, coordinate, 0.0).yz;
#if !FOGSTACK
    float gloss = material.y * material.y * 50.0;
#endif

    float3 environment = 0.0;
    if (material.x > 0.0019607844296842813)
    {
#ifdef SSGI
        float3 normal = viewNormal;
#else
        float2 encodedNormal = normalTexture.SampleLevel(normalSampler, coordinate, 0.0).xy * 4.0 - 2.0;
        float normalLengthSquared = dot(encodedNormal, encodedNormal);
        float2 normalFactors = 1.0 - normalLengthSquared * float2(0.25, 0.5);
        float3 normal = float3(encodedNormal * sqrt(normalFactors.x), -normalFactors.y);
#endif
#if !FOGSTACK

        float3 viewPosition = reconstructViewPosition(coordinate, linearizedDepth, row0, row1, row2, row3);
#endif

        float3 reflected = reflect(normalize(-viewPosition), normal);
        float3 environmentCoordinate = float3(
            dot(ambientFrame[12].xyz, reflected),
            dot(ambientFrame[13].xyz, reflected),
            dot(ambientFrame[14].xyz, reflected)
        );
        float mipLevel = (1.0 - surface.x) * 6.0;
        mipLevel = viewPosition.z * 0.001953125 + mipLevel;
        float arraySlice = floor(material.x * 255.0 - 1.0);
        environment = FO4_SAMPLE_ENVIRONMENT(
            environmentTexture,
            environmentSampler,
            environmentCoordinate,
            mipLevel,
            true,
            arraySlice);
        float luminance = dot(environment, float3(0.299, 0.587, 0.114));
        environment = lerp(environment, luminance.xxx, ambientFrame[30].y * 0.9);
#ifdef WETNESS_EFFECTS
        float3 wetViewDirection = normalize(-viewPosition);
        float3 wetReflected = WetnessEffects::GetFilmReflectionView(
            wetSurface.normalView, wetViewDirection);
        float3 wetEnvironmentCoordinate = float3(
            dot(ambientFrame[12].xyz, wetReflected),
            dot(ambientFrame[13].xyz, wetReflected),
            dot(ambientFrame[14].xyz, wetReflected)
        );
        float wetMipLevel = WetnessEffects::GetFilmMipRoughness(
            1.0 - surface.x, wetSurface.wetness) * 6.0;
        wetMipLevel = viewPosition.z * 0.001953125 + wetMipLevel;
        wetEnvironment = FO4_SAMPLE_ENVIRONMENT(
            environmentTexture,
            environmentSampler,
            wetEnvironmentCoordinate,
            wetMipLevel,
            true,
            arraySlice);
        float wetLuminance = dot(wetEnvironment, float3(0.299, 0.587, 0.114));
        wetEnvironment = lerp(
            wetEnvironment, wetLuminance.xxx, ambientFrame[30].y * 0.9);
        wetFilmWeight = WetnessEffects::GetEnvironmentFilmWeight(
            wetSurface.normalView, wetViewDirection, wetSurface.wetness);
#endif
    }

#if FOGSTACK
    bool3 materialMatches = abs(surface.z * 255.0 - float3(5.0, 2.0, 3.0)) < 0.25;
    bool isSkin = materialMatches.x;
#else
    bool isSkin = abs(surface.z * 255.0 - 5.0) < 0.25;
#endif

    if (isSkin)
    {
        float3 skinAux = skinAuxTexture.Sample(skinAuxSampler, coordinate).xyz;
        float depthScale = 1.0 + (float)nearDepth * ambientPass[0].z;
        float centerDepth = linearDepthTexture.SampleLevel(linearDepthSampler, coordinate, 0.0).x * depthScale;
        float2 tapStep = ambientPass[0].x * float2(0.078125, 0.13889) / centerDepth;

        float4 tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(-2.0, -2.0, -1.28, -1.28);
        float3 firstTap = sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor);
        float3 blurred = firstTap * float3(0.00471690995618701, 0.0001847709936555475, 0.00005075660010334104);

        blurred = centerColor * float3(0.560479, 0.669086, 0.784728) + asfloat(asuint(blurred));
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.019283099099993706, 0.002820180030539632, 0.000842139997985214);

        tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(-0.72, -0.72, -0.32, -0.32);
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.036390, 0.01309990044683218, 0.006436849944293499);
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.08219040185213089, 0.03586079925298691, 0.0209260992705822);

        tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(-0.08, -0.08, 0.08, 0.08);
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.07718019932508469, 0.113491, 0.07938030362129211);
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.07718019932508469, 0.113491, 0.07938030362129211);

        tapCoordinates = coordinate.xyxy + tapStep.xyxy * float4(0.32, 0.32, 0.72, 0.72);
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.08219040185213089, 0.03586079925298691, 0.0209260992705822);
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.036390, 0.01309990044683218, 0.006436849944293499);

        tapCoordinates.zw = coordinate + tapStep * 1.28;
        blurred += sampleSkinTap(tapCoordinates.zw, depthScale, centerDepth, centerColor)
            * float3(0.019283099099993706, 0.002820180030539632, 0.000842139997985214);
        tapCoordinates.xy = coordinate + tapStep * 2.0;
        blurred += sampleSkinTap(tapCoordinates.xy, depthScale, centerDepth, centerColor)
            * float3(0.00471690995618701, 0.0001847709936555475, 0.000050756498239934444);

        float3 skinBase = skinLightTexture.SampleLevel(skinLightSampler, coordinate, 0.0).xyz;
#if TILELIGHT
        skinBase += skinProbeTexture.SampleLevel(skinProbeSampler, coordinate, 0.0).xyz;
#endif
        skinBase += skinAux;
        centerColor = blurred + skinBase;
    }

#if !FOGSTACK
    return float4(composeAmbient(coordinate, directLighting, glossFactor, gloss, environment, centerColor
#ifdef SSGI
        , position.xy, viewNormal
#endif
#ifdef WETNESS_EFFECTS
        , wetEnvironment, wetFilmWeight, wetSurface.wetness
#endif
        ), 1.0);
#else
    float4 output;
    if (!(materialMatches.y || materialMatches.z))
    {
        float3 directLighting = sampleDirectLighting(coordinate);
        float glossFactor = surface.y * 3.0;
        float roughFactor = min(1.0 / rsqrt(saturate(surface.x - 0.3)), 1.0);
        glossFactor *= roughFactor;
        float gloss = material.y * material.y * 50.0;
        float3 color = composeAmbient(coordinate, directLighting, glossFactor, gloss, environment, centerColor
#ifdef SSGI
            , position.xy, viewNormal
#endif
#ifdef WETNESS_EFFECTS
            , wetEnvironment, wetFilmWeight, wetSurface.wetness
#endif
            );

        float height = dot(ambientFrame[14], float4(viewPosition, 1.0)) + ambientFrame[35].z;
        float distanceSquared = dot(viewPosition, viewPosition);
        float distance = sqrt(distanceSquared);
        float distanceCoordinate = distance * ambientFrame[41].x - ambientFrame[41].z;
        float distanceSaturated = saturate(distanceCoordinate);
        float2 heightWeights = saturate(height * ambientFrame[46].xy - ambientFrame[46].zw);
#ifdef EXPONENTIAL_HEIGHT_FOG
        float exponentialDistance;
        float2 exponentialHeight;
        bool exponentialValid = ExponentialHeightFog::TryEvaluate(
            distance,
            height,
            ambientFrame[41],
            ambientFrame[46],
            exponentialDistance,
            exponentialHeight);
        if (exponentialValid)
        {
            heightWeights = exponentialHeight;
        }
#endif
        float heightWeight = lerp(heightWeights.x, heightWeights.y, distanceSaturated);

        float fogLimit = ambientFrame[43].w;
        if (distanceCoordinate > 0.75)
        {
            fogLimit = min(
                ambientFrame[43].w + (distanceSaturated - 0.75) * 4.0 * (1.0 - ambientFrame[43].w),
                1.0);
        }
        float nearDistanceScale = distanceCoordinate < 0.015 ? distanceSaturated * 66.666672 : 1.0;
        float fogCurve = min(pow(distanceSaturated, ambientFrame[42].w), fogLimit);
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (exponentialValid)
        {
            fogCurve = min(exponentialDistance, fogLimit);
        }
#endif
        float heightAlpha = 1.0 - heightWeight + heightWeight * ambientFrame[44].w;
        float3 lowFog = lerp(ambientFrame[42].xyz, ambientFrame[44].xyz, fogCurve);
        float3 highFog = lerp(ambientFrame[43].xyz, ambientFrame[45].xyz, fogCurve);
        float3 fogColor = lerp(lowFog, highFog, heightWeight);
        float fogAmount = fogCurve * heightAlpha;
        fogAmount *= nearDistanceScale;

        float3 worldDirection = normalize(viewPosition);
        float sunAmount = pow(max(dot(worldDirection, screenSetup[1].xyz), 0.0), screenSetup[2].w)
            * screenSetup[1].w;
        fogColor = lerp(fogColor, screenSetup[2].xyz, sunAmount);
        if (fogAmount < ambientFrame[43].w)
        {
            float gray = dot(color, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
            fogColor = lerp(fogColor, gray.xxx, gray);
        }

        output = float4(lerp(color, fogColor, fogAmount), 1.0);
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (ExponentialHeightFog::IsFogFactorDebug())
        {
            output = float4(fogAmount.xxx, 1.0);
        }
#endif
    }
    else
    {
        output = 0.0;
    }
    return output;
#endif
}
#endif

#ifdef BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

#ifdef EXPONENTIAL_HEIGHT_FOG
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#endif

#ifndef OUTPUTMASK
#define OUTPUTMASK 0
#endif

cbuffer PerFrame : register(b12)
{
    float4 g_PF[47];
};

cbuffer PerPass : register(b2)
{
    float4 g_PixelToUV;
    float4 g_DirAndScale;
    float4 g_TintAndExp;
#if OUTPUTMASK
    float4 g_Unused3;
    float4 g_Unused4;
    float4 g_UVClamp;
#endif
};

Texture2D<float4>        TexNormal    : register(t1);
Texture2D<float4>        TexParam     : register(t2);
Texture2D<float4>        TexSurface   : register(t3);
Texture2D<float4>        TexA         : register(t4);
Texture2D<float4>        TexB         : register(t5);
Texture2D<float4>        TexC         : register(t6);
Texture2D<float4>        TexDepth     : register(t7);
TextureCubeArray<float4> TexCube      : register(t8);

SamplerState SampNormal  : register(s1);
SamplerState SampParam   : register(s2);
SamplerState SampSurface : register(s3);
SamplerState SampA       : register(s4);
SamplerState SampB       : register(s5);
SamplerState SampC       : register(s6);
SamplerState SampDepth   : register(s7);
SamplerState SampCube    : register(s8);

#if OUTPUTMASK
Texture2D<float4> TexMask  : register(t9);
SamplerState      SampMask : register(s9);
#endif

float4 main(float4 svpos : SV_POSITION) : SV_Target
{
    float2 uv = svpos.xy * g_PixelToUV.xy;
#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        svpos.xy,
        g_PF[14]);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
        return wetnessDebugColor;
#endif
    float3 wetCube = 0.0;
    float wetFilmWeight = 0.0;
#endif

    float3 surf  = TexSurface.SampleLevel(SampSurface, uv, 0).xyw;
    float  depth = TexDepth.SampleLevel(SampDepth, uv, 0).x;

    float4 pos;
    float4 m0, m1, m2, m3;
    if (depth <= 0.01)
    {
        pos.z = depth * 100.0;
        m0 = g_PF[24]; m1 = g_PF[25]; m2 = g_PF[26]; m3 = g_PF[27];
    }
    else
    {
        pos.z = depth * 1.01 - 0.01;
        m0 = g_PF[20]; m1 = g_PF[21]; m2 = g_PF[22]; m3 = g_PF[23];
    }

    float2 sn = float2(uv.x * g_PixelToUV.z, 1.0 - uv.y * g_PixelToUV.w);
    pos.xy = sn * 2.0 - 1.0;
    pos.w  = 1.0;

    float3 pxyz = float3(dot(m0, pos), dot(m1, pos), dot(m2, pos));
    float  pw   = dot(m3, pos);
    pos.xyz = pxyz / pw;

#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            pos.xyz,
            TerrainShadows::TerrainShadowsSampler,
            g_PF[12],
            g_PF[13],
            g_PF[14],
            g_PF[35],
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            pos.xyz,
            g_PF[12],
            g_PF[13],
            g_PF[14],
            g_PF[35],
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif

    float2 prm = TexParam.SampleLevel(SampParam, uv, 0).yz;

    float3 cube = 0.0;
    if (prm.x > 0.5 / 255.0)
    {
        float4 nn;
        nn.xy = TexNormal.SampleLevel(SampNormal, uv, 0).xy * 4.0 - 2.0;
        float f = dot(nn.xy, nn.xy);
        nn.zw = 1.0 - f * float2(0.25, 0.5);
        nn.xy = nn.xy * sqrt(nn.z);
        nn.z  = -nn.w;

        float3 v   = normalize(-pos.xyz);
        float  ndv = dot(v, nn.xyz);
        ndv = ndv + ndv;
        float3 r = nn.xyz * -ndv + v;

        float3 rw = float3(dot(g_PF[12].xyz, r),
                           dot(g_PF[13].xyz, r),
                           dot(g_PF[14].xyz, r));

        float lod = (1.0 - surf.x) * 6.0;
        lod = pos.z * 0.001953125 + lod;
        float idx = floor(prm.x * 255.0 - 1.0);

        cube = FO4_SAMPLE_ENVIRONMENT(
            TexCube, SampCube, rw, lod, true, idx);
        float lum = dot(cube, float3(0.299, 0.587, 0.114));
        cube = lerp(cube, lum.xxx, g_PF[30].y * 0.9);
#ifdef WETNESS_EFFECTS
        float3 wetR = WetnessEffects::GetFilmReflectionView(
            wetSurface.normalView, v);
        float3 wetRw = float3(dot(g_PF[12].xyz, wetR),
                              dot(g_PF[13].xyz, wetR),
                              dot(g_PF[14].xyz, wetR));
        float wetLod = WetnessEffects::GetFilmMipRoughness(
            1.0 - surf.x, wetSurface.wetness) * 6.0;
        wetLod = pos.z * 0.001953125 + wetLod;
        wetCube = FO4_SAMPLE_ENVIRONMENT(
            TexCube, SampCube, wetRw, wetLod, true, idx);
        float wetLum = dot(wetCube, float3(0.299, 0.587, 0.114));
        wetCube = lerp(wetCube, wetLum.xxx, g_PF[30].y * 0.9);
        wetFilmWeight = WetnessEffects::GetEnvironmentFilmWeight(
            wetSurface.normalView, v, wetSurface.wetness);
#endif
    }

    float4 result;

    float2 idt = surf.z * 255.0 - float2(2.0, 3.0);
    bool2  hit = abs(idt) < 0.25;
    if (!(hit.x || hit.y))
    {
        float3 b   = TexB.SampleLevel(SampB, uv, 0).xyz;
        float3 b3  = b * 3.0;
        float3 a   = TexA.Sample(SampA, uv).xyz;
        float3 c   = TexC.SampleLevel(SampC, uv, 0).xyz;
        float3 col = c + a;
        col = b * 1.5 + col;

        float k  = surf.y * 3.0;
        float s  = min(1.0 / rsqrt(saturate(surf.x - 0.3)), 1.0);
        k = k * s;
        float g2 = (prm.y * prm.y) * 50.0;
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
#ifdef WETNESS_EFFECTS
        float3 dynamicReflectionContribution =
            lerp(cube, wetCube, wetFilmWeight) * k * g2 * b3;
#else
        float3 dynamicReflectionContribution = cube * k * g2 * b3;
#endif
#endif
#ifdef WETNESS_EFFECTS
        col = ((lerp(cube, wetCube, wetFilmWeight) * k) * g2) * b3 + col;
#else
        col = ((cube * k) * g2) * b3 + col;
#endif

#if OUTPUTMASK
        float mask = TexMask.Sample(SampMask, min(uv, g_UVClamp.xy)).x;
        col = col * mask;
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
        dynamicReflectionContribution *= mask;
#endif
#endif

        pos.w = 1.0;
        float  h    = dot(g_PF[14], pos) + g_PF[35].z;
        float  dd   = dot(pos.xyz, pos.xyz);
        float  dist = sqrt(dd) * g_PF[41].x - g_PF[41].z;
        float  ds   = saturate(dist);
        float2 hh   = saturate(h * g_PF[46].xy - g_PF[46].zw);
#ifdef EXPONENTIAL_HEIGHT_FOG
        float exponentialDistance;
        float2 exponentialHeight;
        bool exponentialValid = ExponentialHeightFog::TryEvaluate(
            sqrt(dd),
            h,
            g_PF[41],
            g_PF[46],
            exponentialDistance,
            exponentialHeight);
        if (exponentialValid)
        {
            hh = exponentialHeight;
        }
#endif
        float  fogH = ds * (hh.y - hh.x) + hh.x;

        float w43 = g_PF[43].w;
        float t1v = (0.75 < dist) ? min(((ds - 0.75) * 4.0) * (1.0 - w43) + w43, 1.0) : w43;
        float t2v = (dist < 0.015) ? (ds * 66.666672) : 1.0;
        float fk  = min(pow(ds, g_PF[42].w), t1v);
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (exponentialValid)
        {
            fk = min(exponentialDistance, t1v);
        }
#endif

        float alpha = 1.0 - fogH;
        alpha = fogH * g_PF[44].w + alpha;

        float3 cA   = lerp(g_PF[42].xyz, g_PF[44].xyz, fk);
        float3 cB   = lerp(g_PF[43].xyz, g_PF[45].xyz, fk);
        float3 fogC = lerp(cA, cB, fogH);
        float  amt  = (fk * alpha) * t2v;

        float3 dir = pos.xyz * rsqrt(dd);
        float  sun = pow(max(dot(dir, g_DirAndScale.xyz), 0.0), g_TintAndExp.w) * g_DirAndScale.w;
        fogC = lerp(fogC, g_TintAndExp.xyz, sun);

        if (amt < g_PF[43].w)
        {

            float lum2 = dot(col, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
            fogC = lerp(fogC, lum2.xxx, lum2);
        }

        result = float4(lerp(col, fogC, amt), 0.5);
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (ExponentialHeightFog::IsFogFactorDebug())
        {
            result = float4(amt.xxx, 1.0);
        }
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
        result.xyz = DynamicCubemaps::ApplyFullscreenDebug(
            result.xyz, dynamicReflectionContribution);
#endif
    }
    else
    {
        result = float4(0.0, 0.0, 0.0, 0.0);
    }

    return result;
}
#endif

#ifdef BSDFCOMPOSITE_PS_2D_ACCUMULATOR

#if defined(TERRAIN_SHADOWS) || defined(WETNESS_EFFECTS) || defined(WATER_EFFECTS)
cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 terrain_cb12_pad_28_34[7];
    float4 CameraPosAdjust;
#endif
};
#endif

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

cbuffer ScreenData : register(b2)
{
    float4 screenData[COMPOSITE_CB2_COUNT];
};

Texture2D<float4> baseTexture : register(t0);
#if COMPOSITE_MATERIAL_5
Texture2D<float4> typeTexture : register(t3);
#endif
Texture2D<float4> secondaryTexture : register(t4);
Texture2D<float4> directTexture : register(t5);
Texture2D<float4> ambientTexture : register(t6);
#if COMPOSITE_MODULATION
Texture2D<float4> modulationTexture : register(t9);
#endif
#if TILED_LIGHTS
Texture2D<float4> tileDirectTexture : register(t11);
Texture2D<float4> tileAmbientTexture : register(t12);
#endif

SamplerState baseSampler : register(s0);
#if COMPOSITE_MATERIAL_5
SamplerState typeSampler : register(s3);
#endif
SamplerState secondarySampler : register(s4);
SamplerState directSampler : register(s5);
SamplerState ambientSampler : register(s6);
#if COMPOSITE_MODULATION
SamplerState modulationSampler : register(s9);
#endif
#if TILED_LIGHTS
SamplerState tileDirectSampler : register(s11);
SamplerState tileAmbientSampler : register(s12);
#endif

float4 main(float4 position : SV_POSITION) : SV_Target0
{
#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        position.xy,
        ViewToWorld_row2);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
        return wetnessDebugColor;
#endif
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
    float2 uv = position.xy * screenData[0].xy;
    float4 base = baseTexture.SampleLevel(baseSampler, uv, 0.0);
#ifdef WETNESS_EFFECTS
    base.xyz = WetnessEffects::WetAlbedo(base.xyz, wetSurface.wetness);
#endif
#if COMPOSITE_MATERIAL_5
    float material = typeTexture.SampleLevel(typeSampler, uv, 0.0).w;
#endif
    float3 direct = directTexture.SampleLevel(directSampler, uv, 0.0).xyz;
#if TILED_LIGHTS
    direct += tileDirectTexture.SampleLevel(tileDirectSampler, uv, 0.0).xyz;
#endif
#if !COMPOSITE_MATERIAL_5 && !COMPOSITE_MODULATION && !TILED_LIGHTS
    float3 color = direct * base.xyz;
#else
    base.xyz *= direct;
    float3 color = base.xyz;
#endif
    color *= 3.0;

#if COMPOSITE_MATERIAL_5
    if (abs(material * 255.0 - 5.0) >= 0.25)
#endif
    {
#if TILED_LIGHTS
#if COMPOSITE_MATERIAL_5
        float3 secondary =
            secondaryTexture.Sample(secondarySampler, uv).xyz;
        float3 ambient =
            ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
        float3 tileAmbient =
            tileAmbientTexture.SampleLevel(tileAmbientSampler, uv, 0.0).xyz;
        float3 combinedAmbient = ambient + tileAmbient;
        ambient = combinedAmbient + secondary;
#else
        float3 ambient =
            ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
        ambient +=
            tileAmbientTexture.SampleLevel(tileAmbientSampler, uv, 0.0).xyz;
        ambient += secondaryTexture.Sample(secondarySampler, uv).xyz;
#endif
#else
#if COMPOSITE_MATERIAL_5
        float3 secondary =
            secondaryTexture.Sample(secondarySampler, uv).xyz;
        float3 explicitAmbient =
            ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
        float3 ambient = explicitAmbient + secondary;
#else
        float3 ambient = secondaryTexture.Sample(secondarySampler, uv).xyz +
            ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
#endif
#endif
        color += ambient;
    }

#if COMPOSITE_MODULATION
    float2 modulationUv = min(uv, screenData[5].xy);
    color *= modulationTexture.Sample(modulationSampler, modulationUv).x;
#endif
    return float4(color, base.w);
}
#endif

#ifdef BSDFCOMPOSITE_PS_2D_FOG

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif
#ifdef EXPONENTIAL_HEIGHT_FOG
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
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

#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        input.position.xy,
        ViewToWorld_row2);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        return output;
    }
#endif
#endif

#if !COMPOSITE_HAS_TYPE || COMPOSITE_MATERIAL_5
    float4 baseSample = g_tHdrBaseColor.SampleLevel(g_sBaseColor, uv, 0);
    float3 baseColor = baseSample.xyz;
#ifdef WETNESS_EFFECTS
    baseColor = WetnessEffects::WetAlbedo(baseColor, wetSurface.wetness);
#endif
#if COMPOSITE_MATERIAL_5
    float matIdRaw =
        g_tMaterialIdBuffer.SampleLevel(g_sMaterialId, uv, 0).w;
    float matIdByte = matIdRaw * 255.0;
#endif
    float3 directDiff =
        g_tDirectDiffuse.SampleLevel(g_sDirectDiffuse, uv, 0).xyz;
#if TILED_LIGHTS
    float3 directSpec =
        g_tDirectSpecular.SampleLevel(g_sDirectSpecular, uv, 0).xyz;
    float3 directTotal = directDiff + directSpec;
#else
    float3 directTotal = directDiff;
#endif
    float3 litColor = baseColor * directTotal;
#endif

#if COMPOSITE_HAS_TYPE && !COMPOSITE_MATERIAL_5
    float matIdRaw = g_tMaterialIdBuffer.SampleLevel(g_sMaterialId, uv, 0).w;
    float matIdByte = matIdRaw * 255.0;
#endif
    float depth    = g_tLinearDepth.SampleLevel(g_sDepth, uv, 0).x;
#if !COMPOSITE_HAS_TYPE
    float3 secondaryColor =
        g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;
#endif

    float linearizedDepth;
    float4 reprojRow0;
    float4 reprojRow1;
    float4 reprojRow2;
    float4 reprojRow3;
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

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 terrainUvRemapped =
        float4(uv.x, 0.0, -uv.y, 0.0)
        * float4(ScreenSize.z, 0.0, ScreenSize.w, 0.0);
    terrainUvRemapped.z += 1.0;
    float4 terrainPosition =
        float4(terrainUvRemapped.xz * 2.0 - 1.0, linearizedDepth, 1.0);
    float4 terrainPositionViewH = float4(
        dot(reprojRow0, terrainPosition),
        dot(reprojRow1, terrainPosition),
        dot(reprojRow2, terrainPosition),
        dot(reprojRow3, terrainPosition));
    float3 terrainViewPosition =
        terrainPositionViewH.xyz / terrainPositionViewH.w;
#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            terrainViewPosition,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust_for_fog_height,
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        return output;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            terrainViewPosition,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust_for_fog_height,
            waterDebugColor))
    {
        output.color = waterDebugColor;
        return output;
    }
#endif
#endif

#if COMPOSITE_MATERIAL_5
    float3 ambientWeighted = litColor * 3.0;
    if (abs(matIdByte - 5.0) >= 0.25)
    {
        float3 materialSecondary =
            g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;
#if COMPOSITE_HAS_LIGHT
        float3 materialAmbient =
            g_tAmbientLight.SampleLevel(g_sAmbientLight, uv, 0).xyz;
#if TILED_LIGHTS && COMPOSITE_TILE_AMBIENT
        materialAmbient +=
            g_tAmbientLightSecondary.SampleLevel(
                g_sAmbientLightSecondary, uv, 0).xyz;
#endif
        materialSecondary = materialAmbient + materialSecondary;
#endif
        ambientWeighted = mad(litColor, 3.0, materialSecondary);
    }
#endif

#if COMPOSITE_MATERIAL_EXCLUSION
    bool  isMatId2  = abs(matIdByte - 2.0) < 0.25;
    bool  isMatId3  = abs(matIdByte - 3.0) < 0.25;
    bool  isSkinOrHair = isMatId2 || isMatId3;

    if (!isSkinOrHair)
#endif
    {

#if COMPOSITE_HAS_TYPE && !COMPOSITE_MATERIAL_5
        float4 baseSample   = g_tHdrBaseColor.SampleLevel(g_sBaseColor, uv, 0);
        float3 baseColor    = baseSample.xyz;
#ifdef WETNESS_EFFECTS
        baseColor = WetnessEffects::WetAlbedo(baseColor, wetSurface.wetness);
#endif
        float3 directDiff   = g_tDirectDiffuse.SampleLevel(g_sDirectDiffuse, uv, 0).xyz;
#if TILED_LIGHTS
        float3 directSpec   = g_tDirectSpecular.SampleLevel(g_sDirectSpecular, uv, 0).xyz;
        float3 directTotal  = directDiff + directSpec;
#else
        float3 directTotal  = directDiff;
#endif
        float3 litColor     = baseColor * directTotal;

#if !COMPOSITE_MATERIAL_5
        float3 secondaryColor = g_tSecondaryColor.Sample(g_sSecondaryColor, uv).xyz;
#endif
#endif

        float4 uvRemapped =
            float4(uv.x, 0.0, -uv.y, 0.0)
            * float4(ScreenSize.z, 0.0, ScreenSize.w, 0.0);
        uvRemapped.z += 1.0;
        float2 uvNDC = uvRemapped.xz * 2.0 - 1.0;

        float4 pos4 = float4(uvNDC, linearizedDepth, 1.0);
        float4 posViewH;
        posViewH.x = dot(reprojRow0, pos4);
        posViewH.y = dot(reprojRow1, pos4);
        posViewH.z = dot(reprojRow2, pos4);
        posViewH.w = dot(reprojRow3, pos4);
        float3 posView = posViewH.xyz / posViewH.www;

#if COMPOSITE_HAS_LIGHT && !COMPOSITE_MATERIAL_5
        float3 ambientLight =
            g_tAmbientLight.SampleLevel(g_sAmbientLight, uv, 0).xyz;
#endif
#if TILED_LIGHTS && COMPOSITE_TILE_AMBIENT && !COMPOSITE_MATERIAL_5
        float3 tileAmbient =
            g_tAmbientLightSecondary.SampleLevel(
            g_sAmbientLightSecondary, uv, 0).xyz;
        ambientLight = ambientLight + tileAmbient;
#endif
#if COMPOSITE_HAS_LIGHT && !COMPOSITE_MATERIAL_5
        secondaryColor = ambientLight + secondaryColor;
#endif
#if COMPOSITE_HAS_LIGHT && !COMPOSITE_MATERIAL_5
        float3 ambientWeighted =
            mad(litColor, 3.0, secondaryColor);
#if COMPOSITE_MODULATION
        float2 modulationUv = min(uv, ModulationUvClamp.xy);
        ambientWeighted *=
            g_tModulation.Sample(g_sModulation, modulationUv).x;
#endif
#endif

        float fogPlaneDistance = dot(ViewToWorld_row2, float4(posView, 1.0));
        fogPlaneDistance += CameraPosAdjust_for_fog_height.z;

        float posViewLenSq = dot(posView, posView);
        float posViewLen   = sqrt(posViewLenSq);
        float distanceRamp = posViewLen * FogDistanceRamp_and_lowHeightRamp.x
                             - FogDistanceRamp_and_lowHeightRamp.z;
        float distanceFactor = saturate(distanceRamp);

        float2 fogRemapPair = saturate(fogPlaneDistance.xx
                                       * FogHeightRampScaleBiasPair.xy
                                       - FogHeightRampScaleBiasPair.zw);
#ifdef EXPONENTIAL_HEIGHT_FOG
        float exponentialDistance;
        float2 exponentialHeight;
        bool exponentialValid = ExponentialHeightFog::TryEvaluate(
                posViewLen,
                fogPlaneDistance,
                FogDistanceRamp_and_lowHeightRamp,
                FogHeightRampScaleBiasPair,
                exponentialDistance,
                exponentialHeight);
        if (exponentialValid)
        {
            fogRemapPair = exponentialHeight;
        }
#endif
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
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (exponentialValid)
        {
            distancePow = exponentialDistance;
        }
#endif
        float fogIntensity  = min(distancePow, fogIntensityClamp);

        float unfoggedWeight = 1.0 - fogBlend;
        float fogBlendWeight = mad(
            fogBlend,
            FogFarLowColor_and_highDensityScale.w,
            unfoggedWeight);

        float3 fogColorAC = lerp(FogNearLowColor_and_power.xyz,
                                 FogFarLowColor_and_highDensityScale.xyz,
                                 fogIntensity);
        float3 fogColorBD = lerp(FogNearHighColor_and_clamp.xyz,
                                 FogFarHighColor_and_padding.xyz,
                                 fogIntensity);
        float3 fogColor   = lerp(fogColorAC, fogColorBD, fogBlend);

        float combinedFog = fogIntensity * fogBlendWeight;

        float fogMixFactor = combinedFog * nearEscape;
        float3 viewDirUnit = posView * rsqrt(posViewLenSq);

        float NdotL    = max(dot(viewDirUnit, SunDirection_and_intensity.xyz), 0.0);
        float specular = pow(NdotL, SunColor_and_SpecPower.w)
                         * SunDirection_and_intensity.w;

        float3 sunlitFogColor = lerp(fogColor, SunColor_and_SpecPower.xyz,
                                     specular);

#if !COMPOSITE_HAS_LIGHT && !COMPOSITE_MATERIAL_5
        float3 ambientWeighted = litColor * 3.0;
        ambientWeighted += secondaryColor;
#endif
        bool useGraySaturated = (fogMixFactor < FogNearHighColor_and_clamp.w);
        float  gray            = dot(ambientWeighted, float3(1.0/3.0, 1.0/3.0, 1.0/3.0));
        float3 graySaturated   = sunlitFogColor + gray * (gray.xxx - sunlitFogColor);

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
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (ExponentialHeightFog::IsFogFactorDebug())
        {
            output.color = float4(fogMixFactor.xxx, 1.0);
        }
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

#endif

#ifdef BSDFCOMPOSITE_PS_NO_SRV_POSITION_TEXCOORD

#if defined(TERRAIN_SHADOWS) || defined(WETNESS_EFFECTS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS)
cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 terrain_cb12_pad_28_34[7];
    float4 CameraPosAdjust;
#endif
};
#endif

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 texCoord : TEXCOORD0;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 secondary : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    output.color = 0.0;
    output.secondary = 0.0;
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row2,
            wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        return output;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        return output;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        output.color = waterDebugColor;
        return output;
    }
#endif
    return output;
}
#endif

#ifdef BSDFCOMPOSITE_PS_NO_SRV_POSITION

#if defined(TERRAIN_SHADOWS) || defined(WETNESS_EFFECTS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS)
cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 terrain_cb12_pad_28_34[7];
    float4 CameraPosAdjust;
#endif
};
#endif

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 secondary : SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    output.color = 0.0;
    output.secondary = 0.0;
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row2,
            wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        return output;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        return output;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        output.color = waterDebugColor;
        return output;
    }
#endif
    return output;
}
#endif

#ifdef BSDFCOMPOSITE_PS_CUBE_IBL

#ifdef WETNESS_EFFECTS
#define WETNESS_COMPOSITE_CONSUMER 1
#include "WetnessEffects/WetnessEffects.hlsli"
#endif

#ifdef EXPONENTIAL_HEIGHT_FOG
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#endif

#ifndef COMPOSITE_CB12_COUNT
#define COMPOSITE_CB12_COUNT 47
#endif
#if (defined(TERRAIN_SHADOWS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS_FULLSCREEN_DEBUG)) && COMPOSITE_CB12_COUNT < 36
#undef COMPOSITE_CB12_COUNT
#define COMPOSITE_CB12_COUNT 36
#endif
#ifndef COMPOSITE_CB2_COUNT
#define COMPOSITE_CB2_COUNT 6
#endif
#ifndef COMPOSITE_FOG_STACK
#define COMPOSITE_FOG_STACK 1
#endif
#ifndef COMPOSITE_MODULATION
#define COMPOSITE_MODULATION 1
#endif
#ifndef COMPOSITE_MATERIAL_EXCLUSION
#define COMPOSITE_MATERIAL_EXCLUSION 1
#endif

cbuffer SceneData : register(b12)
{
    float4 scene[COMPOSITE_CB12_COUNT];
};

cbuffer ScreenData : register(b2)
{
    float4 screenData[COMPOSITE_CB2_COUNT];
};

Texture2D<float4> baseTexture : register(t0);
Texture2D<float4> normalTexture : register(t1);
Texture2D<float4> materialTexture : register(t2);
Texture2D<float4> typeTexture : register(t3);
Texture2D<float4> ambientTexture : register(t4);
Texture2D<float4> diffuseTexture : register(t5);
Texture2D<float4> lightTexture : register(t6);
Texture2D<float4> depthTexture : register(t7);
TextureCubeArray<float4> probeTexture : register(t8);
#if COMPOSITE_MODULATION
Texture2D<float4> modulationTexture : register(t9);
#endif
#ifdef TILED_LIGHTS
Texture2D<float4> tileDiffuseTexture : register(t11);
Texture2D<float4> tileLightTexture : register(t12);
#endif

SamplerState baseSampler : register(s0);
SamplerState normalSampler : register(s1);
SamplerState materialSampler : register(s2);
SamplerState typeSampler : register(s3);
SamplerState ambientSampler : register(s4);
SamplerState diffuseSampler : register(s5);
SamplerState lightSampler : register(s6);
SamplerState depthSampler : register(s7);
SamplerState probeSampler : register(s8);
#if COMPOSITE_MODULATION
SamplerState modulationSampler : register(s9);
#endif
#ifdef TILED_LIGHTS
SamplerState tileDiffuseSampler : register(s11);
SamplerState tileLightSampler : register(s12);
#endif

struct PSInput
{
    float4 position : SV_POSITION;
#ifdef COMPOSITE_UNUSED_TEXCOORD
    float3 texcoord : TEXCOORD0;
#endif
};

float4 main(PSInput input) : SV_Target0
{
    float4 result;
    float2 uv = input.position.xy * screenData[0].xy;
#ifdef WETNESS_EFFECTS
    WetnessEffects::Surface wetSurface =
        WetnessEffects::GetSurfaceFromViewToWorldRow2(
        input.position.xy,
        scene[14]);
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColor(wetSurface, wetnessDebugColor))
        return wetnessDebugColor;
#endif
    float3 wetProbeColor = 0.0;
    float wetFilmWeight = 0.0;
#endif
#if !COMPOSITE_MATERIAL_EXCLUSION
    float4 base = baseTexture.SampleLevel(baseSampler, uv, 0.0);
#ifdef WETNESS_EFFECTS
    base.xyz = WetnessEffects::WetAlbedo(base.xyz, wetSurface.wetness);
#endif
#endif
    float3 typeData = typeTexture.SampleLevel(typeSampler, uv, 0.0).xyw;
#if !COMPOSITE_MATERIAL_EXCLUSION
    float3 diffuse = diffuseTexture.SampleLevel(diffuseSampler, uv, 0.0).xyz;
#ifdef TILED_LIGHTS
    diffuse += tileDiffuseTexture.SampleLevel(tileDiffuseSampler, uv, 0.0).xyz;
#endif
    diffuse *= 3.0;
#endif
    float hardwareDepth = depthTexture.SampleLevel(depthSampler, uv, 0.0).x;
#if !COMPOSITE_MATERIAL_EXCLUSION
    float3 ambient = ambientTexture.Sample(ambientSampler, uv).xyz;
#endif
    bool nearDepth = hardwareDepth <= 0.01;

    float linearDepth;
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
    if (nearDepth)
    {
        linearDepth = hardwareDepth * 100.0;
        row0 = scene[24];
        row1 = scene[25];
        row2 = scene[26];
        row3 = scene[27];
    }
    else
    {
        linearDepth = hardwareDepth * 1.01 - 0.01;
        row0 = scene[20];
        row1 = scene[21];
        row2 = scene[22];
        row3 = scene[23];
    }

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float2 terrainProjectedXY = float2(
        uv.x * screenData[0].z,
        1.0 - uv.y * screenData[0].w) * 2.0 - 1.0;
    float4 terrainProjected =
        float4(terrainProjectedXY, linearDepth, 1.0);
    float4 terrainPositionViewH = float4(
        dot(row0, terrainProjected),
        dot(row1, terrainProjected),
        dot(row2, terrainProjected),
        dot(row3, terrainProjected));
    float3 terrainViewPosition =
        terrainPositionViewH.xyz / terrainPositionViewH.w;
#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            terrainViewPosition,
            TerrainShadows::TerrainShadowsSampler,
            scene[12],
            scene[13],
            scene[14],
            scene[35],
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            terrainViewPosition,
            scene[12],
            scene[13],
            scene[14],
            scene[35],
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
#endif

#if COMPOSITE_MATERIAL_EXCLUSION || COMPOSITE_FOG_STACK
    float2 projectedXY = float2(
        uv.x * screenData[0].z,
        1.0 - uv.y * screenData[0].w) * 2.0 - 1.0;
    float4 projected = float4(projectedXY, linearDepth, 1.0);
    float3 worldNumerator = float3(
        dot(row0, projected),
        dot(row1, projected),
        dot(row2, projected));
    float worldDenominator = dot(row3, projected);
    float3 worldPosition = worldNumerator / worldDenominator;
#else
    float4 projected;
    float3 worldPosition;
#endif

#if COMPOSITE_MATERIAL_EXCLUSION
    float2 material = materialTexture.SampleLevel(materialSampler, uv, 0.0).yz;
    float3 probeColor = 0.0;
    if (material.x > (0.5 / 255.0))
    {
        float2 encodedNormal =
            normalTexture.SampleLevel(normalSampler, uv, 0.0).xy * 4.0 - 2.0;
        float encodedLengthSquared = dot(encodedNormal, encodedNormal);
        float normalScale = sqrt(1.0 - encodedLengthSquared * 0.25);
        float3 normal = float3(
            encodedNormal * normalScale,
            -(1.0 - encodedLengthSquared * 0.5));
        float3 reflected = reflect(normalize(-worldPosition), normal);
        float3 probeDirection = float3(
            dot(scene[12].xyz, reflected),
            dot(scene[13].xyz, reflected),
            dot(scene[14].xyz, reflected));
        float probeLod = mad(
            1.0 - typeData.x,
            6.0,
            worldPosition.z * 0.001953125);
        float probeSlice = floor(material.x * 255.0 - 1.0);
        probeColor = FO4_SAMPLE_ENVIRONMENT(
            probeTexture,
            probeSampler,
            probeDirection,
            probeLod,
            true,
            probeSlice);
        float probeLuma = dot(probeColor, float3(0.299, 0.587, 0.114));
        probeColor = lerp(probeColor, probeLuma.xxx, scene[30].y * 0.9);
#ifdef WETNESS_EFFECTS
        float3 wetViewDirection = normalize(-worldPosition);
        float3 wetReflected = WetnessEffects::GetFilmReflectionView(
            wetSurface.normalView, wetViewDirection);
        float3 wetProbeDirection = float3(
            dot(scene[12].xyz, wetReflected),
            dot(scene[13].xyz, wetReflected),
            dot(scene[14].xyz, wetReflected));
        float wetProbeLod = mad(
            WetnessEffects::GetFilmMipRoughness(
                1.0 - typeData.x, wetSurface.wetness),
            6.0,
            worldPosition.z * 0.001953125);
        wetProbeColor = FO4_SAMPLE_ENVIRONMENT(
            probeTexture,
            probeSampler,
            wetProbeDirection,
            wetProbeLod,
            true,
            probeSlice);
        float wetProbeLuma = dot(wetProbeColor, float3(0.299, 0.587, 0.114));
        wetProbeColor = lerp(
            wetProbeColor, wetProbeLuma.xxx, scene[30].y * 0.9);
        wetFilmWeight = WetnessEffects::GetEnvironmentFilmWeight(
            wetSurface.normalView, wetViewDirection, wetSurface.wetness);
#endif
    }
#else
    float2 material;
    float3 probeColor = 0.0;
#endif

#if COMPOSITE_MATERIAL_EXCLUSION
    bool2 excludedType =
        abs(typeData.z * 255.0 - float2(2.0, 3.0)) < 0.25;
    if (!(excludedType.x || excludedType.y))
    {
#endif

#if COMPOSITE_MATERIAL_EXCLUSION
    float4 base = baseTexture.SampleLevel(baseSampler, uv, 0.0);
#ifdef WETNESS_EFFECTS
    base.xyz = WetnessEffects::WetAlbedo(base.xyz, wetSurface.wetness);
#endif
    float3 diffuse = diffuseTexture.SampleLevel(diffuseSampler, uv, 0.0).xyz;
#ifdef TILED_LIGHTS
    diffuse += tileDiffuseTexture.SampleLevel(tileDiffuseSampler, uv, 0.0).xyz;
#endif
    diffuse *= 3.0;

    float3 ambientBase = ambientTexture.Sample(ambientSampler, uv).xyz;
    float3 light = lightTexture.SampleLevel(lightSampler, uv, 0.0).xyz;
#ifdef TILED_LIGHTS
    float3 tileLight =
        tileLightTexture.SampleLevel(tileLightSampler, uv, 0.0).xyz;
    light += tileLight;
#endif
#if COMPOSITE_MODULATION
    float3 ambient = ambientBase + light;
#else
    float3 ambient = light + ambientBase;
#endif
#else
    float3 light = lightTexture.SampleLevel(lightSampler, uv, 0.0).xyz;
#ifdef TILED_LIGHTS
    float3 tileLight =
        tileLightTexture.SampleLevel(tileLightSampler, uv, 0.0).xyz;
    light += tileLight;
#endif
    ambient += light;
#endif

    float3 color = mad(diffuse, base.xyz, ambient);
#if COMPOSITE_MATERIAL_EXCLUSION
    float gloss = typeData.y * 3.0;
    float roughFactor = min(
        1.0,
        1.0 / rsqrt(saturate(typeData.x - 0.3)));
    gloss *= roughFactor;
    float specularScale = material.y * material.y * 50.0;
#else
    float gloss = typeData.y * 3.0;
    float roughFactor = min(
        1.0,
        1.0 / rsqrt(saturate(typeData.x - 0.3)));
    gloss *= roughFactor;
    material = materialTexture.SampleLevel(materialSampler, uv, 0.0).yz;
    float specularScale = material.y * material.y * 50.0;
    if (material.x > (0.5 / 255.0))
    {
        float2 encodedNormal =
            normalTexture.SampleLevel(normalSampler, uv, 0.0).xy * 4.0 - 2.0;
        float encodedLengthSquared = dot(encodedNormal, encodedNormal);
        float normalScale = sqrt(1.0 - encodedLengthSquared * 0.25);
        float3 normal = float3(
            encodedNormal * normalScale,
            -(1.0 - encodedLengthSquared * 0.5));
        float2 projectedXY = float2(
            uv.x * screenData[0].z,
            1.0 - uv.y * screenData[0].w) * 2.0 - 1.0;
        projected = float4(projectedXY, linearDepth, 1.0);
        float3 worldNumerator = float3(
            dot(row0, projected),
            dot(row1, projected),
            dot(row2, projected));
        float worldDenominator = dot(row3, projected);
        worldPosition = worldNumerator / worldDenominator;
        float3 reflected = reflect(normalize(-worldPosition), normal);
        float3 probeDirection = float3(
            dot(scene[12].xyz, reflected),
            dot(scene[13].xyz, reflected),
            dot(scene[14].xyz, reflected));
        float probeLod = mad(
            1.0 - typeData.x,
            6.0,
            worldPosition.z * 0.001953125);
        float probeSlice = floor(material.x * 255.0 - 1.0);
        probeColor = FO4_SAMPLE_ENVIRONMENT(
            probeTexture,
            probeSampler,
            probeDirection,
            probeLod,
            true,
            probeSlice);
        float probeLuma = dot(probeColor, float3(0.299, 0.587, 0.114));
        probeColor = lerp(probeColor, probeLuma.xxx, scene[30].y * 0.9);
#ifdef WETNESS_EFFECTS
        float3 wetViewDirection = normalize(-worldPosition);
        float3 wetReflected = WetnessEffects::GetFilmReflectionView(
            wetSurface.normalView, wetViewDirection);
        float3 wetProbeDirection = float3(
            dot(scene[12].xyz, wetReflected),
            dot(scene[13].xyz, wetReflected),
            dot(scene[14].xyz, wetReflected));
        float wetProbeLod = mad(
            WetnessEffects::GetFilmMipRoughness(
                1.0 - typeData.x, wetSurface.wetness),
            6.0,
            worldPosition.z * 0.001953125);
        wetProbeColor = FO4_SAMPLE_ENVIRONMENT(
            probeTexture,
            probeSampler,
            wetProbeDirection,
            wetProbeLod,
            true,
            probeSlice);
        float wetProbeLuma = dot(wetProbeColor, float3(0.299, 0.587, 0.114));
        wetProbeColor = lerp(
            wetProbeColor, wetProbeLuma.xxx, scene[30].y * 0.9);
        wetFilmWeight = WetnessEffects::GetEnvironmentFilmWeight(
            wetSurface.normalView, wetViewDirection, wetSurface.wetness);
#endif
    }
#endif
#ifdef WETNESS_EFFECTS
    float3 reflectionColor =
        lerp(probeColor, wetProbeColor, wetFilmWeight);
    color = mad(
        reflectionColor * gloss * specularScale,
        diffuse,
        color);
#else
    float3 reflectionColor = probeColor;
    color = mad(reflectionColor * gloss * specularScale, diffuse, color);
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
    float3 dynamicReflectionContribution =
        reflectionColor * gloss * specularScale * diffuse;
#endif

#if COMPOSITE_MODULATION
    float2 modulationUV = min(uv, screenData[5].xy);
    float modulation =
        modulationTexture.Sample(modulationSampler, modulationUV).x;
    color *= modulation;
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
    dynamicReflectionContribution *= modulation;
#endif
#endif

#if COMPOSITE_FOG_STACK
    float height =
        dot(scene[14], float4(worldPosition, 1.0)) + scene[35].z;
    float distanceSquared = dot(worldPosition, worldPosition);
    float distance = sqrt(distanceSquared);
    float distanceCoordinate = distance * scene[41].x - scene[41].z;
    float distanceSaturated = saturate(distanceCoordinate);
    float2 heightWeights =
        saturate(height * scene[46].xy - scene[46].zw);
#ifdef EXPONENTIAL_HEIGHT_FOG
    float exponentialDistance;
    float2 exponentialHeight;
    bool exponentialValid = ExponentialHeightFog::TryEvaluate(
        distance,
        height,
        scene[41],
        scene[46],
        exponentialDistance,
        exponentialHeight);
    if (exponentialValid)
    {
        heightWeights = exponentialHeight;
    }
#endif
    float heightWeight =
        lerp(heightWeights.x, heightWeights.y, distanceSaturated);

    float fogLimit = scene[43].w;
    if (distanceCoordinate > 0.75)
    {
        fogLimit = min(
            scene[43].w +
                (distanceSaturated - 0.75) * 4.0 * (1.0 - scene[43].w),
            1.0);
    }
    float nearDistanceScale = distanceCoordinate < 0.015
        ? distanceSaturated * 66.666672
        : 1.0;
    float fogCurve =
        min(pow(distanceSaturated, scene[42].w), fogLimit);
#ifdef EXPONENTIAL_HEIGHT_FOG
    if (exponentialValid)
    {
        fogCurve = min(exponentialDistance, fogLimit);
    }
#endif
    float heightAlpha =
        1.0 - heightWeight + heightWeight * scene[44].w;
    float3 lowFog =
        lerp(scene[42].xyz, scene[44].xyz, fogCurve);
    float3 highFog =
        lerp(scene[43].xyz, scene[45].xyz, fogCurve);
    float3 fogColor = lerp(lowFog, highFog, heightWeight);
    float fogAmount = fogCurve * heightAlpha * nearDistanceScale;

    float3 worldDirection = normalize(worldPosition);
    float sunAmount = pow(
        max(dot(worldDirection, screenData[1].xyz), 0.0),
        screenData[2].w) * screenData[1].w;
    fogColor = lerp(fogColor, screenData[2].xyz, sunAmount);
    if (fogAmount < scene[43].w)
    {
        float gray =
            dot(color, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
        fogColor = lerp(fogColor, gray.xxx, gray);
    }

    color = lerp(color, fogColor, fogAmount);
#ifdef EXPONENTIAL_HEIGHT_FOG
    if (ExponentialHeightFog::IsFogFactorDebug())
    {
        color = fogAmount.xxx;
    }
#endif
#endif

#ifdef COMPOSITE_ALPHA_ONE
        result = float4(color, 1.0);
#else
        result = float4(color, base.w);
#endif
#ifdef DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG
        result.xyz = DynamicCubemaps::ApplyFullscreenDebug(
            result.xyz, dynamicReflectionContribution);
#endif
#if COMPOSITE_MATERIAL_EXCLUSION
    }
    else
    {
#ifdef COMPOSITE_ALPHA_ONE
        result = float4(0.0, 0.0, 0.0, 1.0);
#else
        result = 0.0;
#endif
    }
#endif
    return result;
}
#endif

#ifdef BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR

#if defined(TERRAIN_SHADOWS) || defined(WETNESS_EFFECTS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS)
cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float4 terrain_cb12_pad_28_34[7];
    float4 CameraPosAdjust;
#endif
};
#endif

#if !defined(WAVE5A_ACCUMULATOR_SHAPE)
#error WAVE5A_ACCUMULATOR_SHAPE is required
#endif

#if WAVE5A_ACCUMULATOR_SHAPE == 1

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[6];
};

SamplerState g_sAmbientPrimary : register(s6);
SamplerState g_sAmbientSecondary : register(s12);
SamplerState g_sLighting : register(s4);
SamplerState g_sColorPrimary : register(s5);
SamplerState g_sColorSecondary : register(s11);
SamplerState g_sOcclusion : register(s9);

Texture2D<float4> g_tAmbientPrimary : register(t6);
Texture2D<float4> g_tAmbientSecondary : register(t12);
Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColorPrimary : register(t5);
Texture2D<float4> g_tColorSecondary : register(t11);
Texture2D<float4> g_tOcclusion : register(t9);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row2,
            wetnessDebugColor))
    {
        return wetnessDebugColor;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
    float2 screenUv = input.position.xy * cb2[0].xy;
    float3 ambient = g_tAmbientPrimary.SampleLevel(g_sAmbientPrimary, screenUv, 0).xyz;
    ambient += g_tAmbientSecondary.SampleLevel(g_sAmbientSecondary, screenUv, 0).xyz;
    ambient += g_tLighting.Sample(g_sLighting, screenUv).xyz;
    float3 color = g_tColorPrimary.SampleLevel(g_sColorPrimary, screenUv, 0).xyz;
    color += g_tColorSecondary.SampleLevel(g_sColorSecondary, screenUv, 0).xyz;
    float2 occlusionUv = min(screenUv, cb2[5].xy);
    float occlusion = g_tOcclusion.Sample(g_sOcclusion, occlusionUv).x;
    float3 result = color * 1.5 + ambient;
    return float4(result * occlusion, 0.5);
}

#elif WAVE5A_ACCUMULATOR_SHAPE == 2

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[1];
};

SamplerState g_sShading : register(s3);
SamplerState g_sLighting : register(s4);
SamplerState g_sColor : register(s5);
SamplerState g_sAmbient : register(s6);

Texture2D<float4> g_tShading : register(t3);
Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColor : register(t5);
Texture2D<float4> g_tAmbient : register(t6);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row2,
            wetnessDebugColor))
    {
        return wetnessDebugColor;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
    float2 screenUv = input.position.xy * cb2[0].xy;
    float material = g_tShading.SampleLevel(g_sShading, screenUv, 0).w;
    float3 color = g_tColor.SampleLevel(g_sColor, screenUv, 0).xyz;
    float3 result = color * 1.5;
    if (abs(material * 255.0 - 5.0) >= 0.25)
    {
        float3 lighting = g_tLighting.Sample(g_sLighting, screenUv).xyz;
        float3 tail = g_tAmbient.SampleLevel(g_sAmbient, screenUv, 0).xyz;
        float3 ambient = tail + lighting;
        result = color * 1.5 + ambient;
    }
    return float4(result, 0.5);
}

#elif WAVE5A_ACCUMULATOR_SHAPE == 3

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[1];
};

SamplerState g_sShading : register(s3);
SamplerState g_sLighting : register(s4);
SamplerState g_sColorPrimary : register(s5);
SamplerState g_sAmbientPrimary : register(s6);
SamplerState g_sColorSecondary : register(s11);
SamplerState g_sAmbientSecondary : register(s12);

Texture2D<float4> g_tShading : register(t3);
Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColorPrimary : register(t5);
Texture2D<float4> g_tAmbientPrimary : register(t6);
Texture2D<float4> g_tColorSecondary : register(t11);
Texture2D<float4> g_tAmbientSecondary : register(t12);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row2,
            wetnessDebugColor))
    {
        return wetnessDebugColor;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
    float2 screenUv = input.position.xy * cb2[0].xy;
    float material = g_tShading.SampleLevel(g_sShading, screenUv, 0).w;
    float3 color = g_tColorPrimary.SampleLevel(g_sColorPrimary, screenUv, 0).xyz;
    color += g_tColorSecondary.SampleLevel(g_sColorSecondary, screenUv, 0).xyz;
    float3 result = color * 1.5;
    if (abs(material * 255.0 - 5.0) >= 0.25)
    {
        float3 lighting = g_tLighting.Sample(g_sLighting, screenUv).xyz;
        float3 ambient = g_tAmbientPrimary.SampleLevel(g_sAmbientPrimary, screenUv, 0).xyz;
        ambient += g_tAmbientSecondary.SampleLevel(g_sAmbientSecondary, screenUv, 0).xyz;
        ambient += lighting;
        result = color * 1.5 + ambient;
    }
    return float4(result, 0.5);
}

#elif WAVE5A_ACCUMULATOR_SHAPE == 4

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[6];
};

SamplerState g_sLighting : register(s4);
SamplerState g_sColor : register(s5);
SamplerState g_sAmbient : register(s6);
SamplerState g_sOcclusion : register(s9);

Texture2D<float4> g_tLighting : register(t4);
Texture2D<float4> g_tColor : register(t5);
Texture2D<float4> g_tAmbient : register(t6);
Texture2D<float4> g_tOcclusion : register(t9);

struct PS_INPUT
{
    float4 position : SV_POSITION;
};

float4 main(PS_INPUT input) : SV_Target0
{
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row2,
            wetnessDebugColor))
    {
        return wetnessDebugColor;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            TerrainShadows::TerrainShadowsSampler,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            ViewToWorld_row0,
            ViewToWorld_row1,
            ViewToWorld_row2,
            CameraPosAdjust,
            FarReproj_row0,
            FarReproj_row1,
            FarReproj_row2,
            FarReproj_row3,
            NearReproj_row0,
            NearReproj_row1,
            NearReproj_row2,
            NearReproj_row3,
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
    float2 screenUv = input.position.xy * cb2[0].xy;
    float3 ambient = g_tLighting.Sample(g_sLighting, screenUv).xyz;
    ambient += g_tAmbient.SampleLevel(g_sAmbient, screenUv, 0).xyz;
    float3 color = g_tColor.SampleLevel(g_sColor, screenUv, 0).xyz;
    float2 occlusionUv = min(screenUv, cb2[5].xy);
    float occlusion = g_tOcclusion.Sample(g_sOcclusion, occlusionUv).x;
    float3 result = (color * 1.5 + ambient) * occlusion;
    return float4(result, 0.5);
}

#else
#error Unsupported no-t0 accumulator shape
#endif
#endif

#ifdef BSDFCOMPOSITE_PS_NO_T0_FOG

#ifdef EXPONENTIAL_HEIGHT_FOG
#include "ExponentialHeightFog/ExponentialHeightFog.hlsli"
#endif

#if !defined(WAVE5A_FOG_SHAPE)
#error WAVE5A_FOG_SHAPE is required
#endif

#if WAVE5A_FOG_SHAPE == 1
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 0
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 0
#elif WAVE5A_FOG_SHAPE == 2
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 0
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 1
#elif WAVE5A_FOG_SHAPE == 3
#define WAVE5A_FOG_CB2_COUNT 6
#define WAVE5A_FOG_TILED 0
#define WAVE5A_FOG_MODULATION 1
#define WAVE5A_FOG_MATERIAL5 0
#elif WAVE5A_FOG_SHAPE == 4
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 1
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 0
#elif WAVE5A_FOG_SHAPE == 5
#define WAVE5A_FOG_CB2_COUNT 3
#define WAVE5A_FOG_TILED 1
#define WAVE5A_FOG_MODULATION 0
#define WAVE5A_FOG_MATERIAL5 1
#elif WAVE5A_FOG_SHAPE == 6
#define WAVE5A_FOG_CB2_COUNT 6
#define WAVE5A_FOG_TILED 1
#define WAVE5A_FOG_MODULATION 1
#define WAVE5A_FOG_MATERIAL5 0
#else
#error Unsupported no-t0 fog shape
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 scene[47];
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 screenData[WAVE5A_FOG_CB2_COUNT];
};

Texture2D<float4> typeTexture : register(t3);
Texture2D<float4> secondaryTexture : register(t4);
Texture2D<float4> directTexture : register(t5);
Texture2D<float4> ambientTexture : register(t6);
Texture2D<float4> depthTexture : register(t7);
#if WAVE5A_FOG_MODULATION
Texture2D<float4> modulationTexture : register(t9);
#endif
#if WAVE5A_FOG_TILED
Texture2D<float4> directSecondaryTexture : register(t11);
Texture2D<float4> ambientSecondaryTexture : register(t12);
#endif

SamplerState typeSampler : register(s3);
SamplerState secondarySampler : register(s4);
SamplerState directSampler : register(s5);
SamplerState ambientSampler : register(s6);
SamplerState depthSampler : register(s7);
#if WAVE5A_FOG_MODULATION
SamplerState modulationSampler : register(s9);
#endif
#if WAVE5A_FOG_TILED
SamplerState directSecondarySampler : register(s11);
SamplerState ambientSecondarySampler : register(s12);
#endif

float4 main(float4 position : SV_POSITION) : SV_Target0
{
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            position.xy,
            scene[14],
            wetnessDebugColor))
    {
        return wetnessDebugColor;
    }
#endif
    float2 uv = position.xy * screenData[0].xy;
    float material = typeTexture.SampleLevel(typeSampler, uv, 0.0).w;

#if WAVE5A_FOG_MATERIAL5
    float3 direct = directTexture.SampleLevel(directSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
    direct += directSecondaryTexture.SampleLevel(directSecondarySampler, uv, 0.0).xyz;
#endif
#endif

    float depth = depthTexture.SampleLevel(depthSampler, uv, 0.0).x;
    float projectedDepth;
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
    if (depth <= 0.01)
    {
        projectedDepth = depth * 100.0;
        row0 = scene[24];
        row1 = scene[25];
        row2 = scene[26];
        row3 = scene[27];
    }
    else
    {
        projectedDepth = depth * 1.01 - 0.01;
        row0 = scene[20];
        row1 = scene[21];
        row2 = scene[22];
        row3 = scene[23];
    }

#if defined(TERRAIN_SHADOWS) || defined(WATER_EFFECTS)
    float2 terrainProjectedUv = float2(
        uv.x * screenData[0].z,
        1.0 - uv.y * screenData[0].w);
    float4 terrainProjected =
        float4(terrainProjectedUv * 2.0 - 1.0, projectedDepth, 1.0);
    float4 terrainReconstructed = float4(
        dot(row0, terrainProjected),
        dot(row1, terrainProjected),
        dot(row2, terrainProjected),
        dot(row3, terrainProjected));
    terrainReconstructed.xyz /= terrainReconstructed.w;
#ifdef TERRAIN_SHADOWS
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            terrainReconstructed.xyz,
            TerrainShadows::TerrainShadowsSampler,
            scene[12],
            scene[13],
            scene[14],
            scene[35],
            terrainDebugColor))
    {
        return terrainDebugColor;
    }
#endif
#ifdef WATER_EFFECTS
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            terrainReconstructed.xyz,
            scene[12],
            scene[13],
            scene[14],
            scene[35],
            waterDebugColor))
    {
        return waterDebugColor;
    }
#endif
#endif

#if WAVE5A_FOG_MATERIAL5
    float3 composite = direct * 1.5;
    if (abs(material * 255.0 - 5.0) >= 0.25)
    {
        float3 secondary = secondaryTexture.Sample(secondarySampler, uv).xyz;
        float3 ambient = ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
        ambient += ambientSecondaryTexture.SampleLevel(ambientSecondarySampler, uv, 0.0).xyz;
#endif
        float3 secondaryAmbient = secondary + ambient;
        composite = direct * 1.5 + secondaryAmbient;
    }
#endif

    bool isMaterial2 = abs(material * 255.0 - 2.0) < 0.25;
    bool isMaterial3 = abs(material * 255.0 - 3.0) < 0.25;
    float4 result;
    if (!(isMaterial2 || isMaterial3))
    {

#if !WAVE5A_FOG_MATERIAL5
        float3 direct = directTexture.SampleLevel(directSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
        direct += directSecondaryTexture.SampleLevel(directSecondarySampler, uv, 0.0).xyz;
#endif
        float3 secondary = secondaryTexture.Sample(secondarySampler, uv).xyz;
#endif

        float2 projectedUv = float2(
            uv.x * screenData[0].z,
            1.0 - uv.y * screenData[0].w);
        float4 projected = float4(projectedUv * 2.0 - 1.0, projectedDepth, 1.0);
        float4 reconstructed = float4(
            dot(row0, projected),
            dot(row1, projected),
            dot(row2, projected),
            dot(row3, projected));
        reconstructed.xyz /= reconstructed.w;

#if !WAVE5A_FOG_MATERIAL5
        float3 ambient = ambientTexture.SampleLevel(ambientSampler, uv, 0.0).xyz;
#if WAVE5A_FOG_TILED
        ambient += ambientSecondaryTexture.SampleLevel(ambientSecondarySampler, uv, 0.0).xyz;
#endif
        ambient = mad(secondary, 1.0, ambient);
        float3 composite = direct * 1.5 + ambient;
#endif

#if WAVE5A_FOG_MODULATION
        composite *= modulationTexture.Sample(
            modulationSampler, min(uv, screenData[5].xy)).x;
#endif

        float fogPlane = dot(scene[14], float4(reconstructed.xyz, 1.0)) + scene[35].z;
        float distanceSquared = dot(reconstructed.xyz, reconstructed.xyz);
        float distanceRampRaw = sqrt(distanceSquared) * scene[41].x - scene[41].z;
        float distanceRamp = saturate(distanceRampRaw);
        float2 heightRemaps = saturate(fogPlane * scene[46].xy - scene[46].zw);
#ifdef EXPONENTIAL_HEIGHT_FOG
        float exponentialDistance;
        float2 exponentialHeight;
        bool exponentialValid = ExponentialHeightFog::TryEvaluate(
            sqrt(distanceSquared),
            fogPlane,
            scene[41],
            scene[46],
            exponentialDistance,
            exponentialHeight);
        if (exponentialValid)
        {
            heightRemaps = exponentialHeight;
        }
#endif
        float heightFactor = lerp(heightRemaps.x, heightRemaps.y, distanceRamp);

        float fogLimit = scene[43].w;
        if (distanceRampRaw > 0.75)
        {
            fogLimit = min(
                scene[43].w + (distanceRamp - 0.75) * 4.0 * (1.0 - scene[43].w),
                1.0);
        }

        float nearEscape = distanceRampRaw < 0.015 ? distanceRamp * 66.666672 : 1.0;
        float fogCurve = min(pow(distanceRamp, scene[42].w), fogLimit);
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (exponentialValid)
        {
            fogCurve = min(exponentialDistance, fogLimit);
        }
#endif
        float heightScale = 1.0 - heightFactor + heightFactor * scene[44].w;
        float3 lowFog = lerp(scene[42].xyz, scene[44].xyz, fogCurve);
        float3 highFog = lerp(scene[43].xyz, scene[45].xyz, fogCurve);
        float3 fogColor = lerp(lowFog, highFog, heightFactor);
        float fogMix = fogCurve * heightScale * nearEscape;

        float3 viewDirection = normalize(reconstructed.xyz);
        float sun = pow(max(dot(viewDirection, screenData[1].xyz), 0.0), screenData[2].w)
            * screenData[1].w;
        float3 sunlitFog = lerp(fogColor, screenData[2].xyz, sun);
        bool useGrayFog = fogMix < scene[43].w;
        float gray = dot(composite, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
        float3 graySaturated = sunlitFog + gray * (gray - sunlitFog);
        float3 selectedFog = useGrayFog ? graySaturated : sunlitFog;
        float3 outputColor = lerp(composite, selectedFog, fogMix);
        result = float4(outputColor, 0.5);
#ifdef EXPONENTIAL_HEIGHT_FOG
        if (ExponentialHeightFog::IsFogFactorDebug())
        {
            result = float4(fogMix.xxx, 1.0);
        }
#endif
    }
    else
    {
        result = float4(0.0, 0.0, 0.0, 0.0);
    }
    return result;
}
#endif

#ifdef BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL

#if defined(WAVE5B_SSS_SURFACE_CONTACT_SHAPE)
#error WAVE5B_SSS_SURFACE_CONTACT_SHAPE selects the surface/contact root, not this one
#endif

#if !defined(WAVE5B_SSS_RECORD_NORMAL_SHAPE)
#error WAVE5B_SSS_RECORD_NORMAL_SHAPE is required
#endif

#if WAVE5B_SSS_RECORD_NORMAL_SHAPE == 1
#define WAVE5B_RECORD_NORMAL_CB12_COUNT 28
#define WAVE5B_RECORD_NORMAL_CB2_COUNT 4
#define WAVE5B_RECORD_NORMAL_HAS_T2 0
#define WAVE5B_RECORD_NORMAL_HAS_WETNESS 0
#elif WAVE5B_SSS_RECORD_NORMAL_SHAPE == 2
#define WAVE5B_RECORD_NORMAL_CB12_COUNT 31
#define WAVE5B_RECORD_NORMAL_CB2_COUNT 7
#define WAVE5B_RECORD_NORMAL_HAS_T2 0
#define WAVE5B_RECORD_NORMAL_HAS_WETNESS 1
#define WAVE5B_RECORD_NORMAL_WETNESS_INDEX 6
#elif WAVE5B_SSS_RECORD_NORMAL_SHAPE == 3
#define WAVE5B_RECORD_NORMAL_CB12_COUNT 28
#define WAVE5B_RECORD_NORMAL_CB2_COUNT 7
#define WAVE5B_RECORD_NORMAL_HAS_T2 1
#define WAVE5B_RECORD_NORMAL_HAS_WETNESS 0
#elif WAVE5B_SSS_RECORD_NORMAL_SHAPE == 4
#define WAVE5B_RECORD_NORMAL_CB12_COUNT 31
#define WAVE5B_RECORD_NORMAL_CB2_COUNT 8
#define WAVE5B_RECORD_NORMAL_HAS_T2 1
#define WAVE5B_RECORD_NORMAL_HAS_WETNESS 1
#define WAVE5B_RECORD_NORMAL_WETNESS_INDEX 7
#else
#error Unsupported SSS MRT record-normal shape
#endif

#if defined(TERRAIN_SHADOWS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS_FULLSCREEN_DEBUG)
#undef WAVE5B_RECORD_NORMAL_CB12_COUNT
#define WAVE5B_RECORD_NORMAL_CB12_COUNT 36
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12[WAVE5B_RECORD_NORMAL_CB12_COUNT];
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[WAVE5B_RECORD_NORMAL_CB2_COUNT];
};

SamplerState g_sNormal : register(s1);
#if WAVE5B_RECORD_NORMAL_HAS_T2
SamplerState g_sMarch : register(s2);
#endif
SamplerState g_sDepth : register(s7);
SamplerState g_sDecalColor : register(s9);
SamplerState g_sDecalNormal : register(s11);
SamplerState g_sDecalAux : register(s12);

Texture2D<float4> g_tNormal : register(t1);
#if WAVE5B_RECORD_NORMAL_HAS_T2
Texture2D<float4> g_tMarch : register(t2);
#endif
Texture2D<float4> g_tDepth : register(t7);

struct DecalRecord
{
    float4 projectionRow0;
    float4 projectionRow1;
    float4 projectionRow2;
    float4 pad0x30;
    float4 pad0x40;
    float4 pad0x50;
    float4 pad0x60;
    float4 pad0x70;
    float4 marchFrame;
    float3 decalDirection;
    float pad0x9c;
    float4 recordNormal;
    float4 pad0xb0;
    float4 uvBounds;
    float4 pad0xd0;
};

StructuredBuffer<DecalRecord> g_decalRecords : register(t8);
Texture2D<float4> g_tDecalColor : register(t9);
Texture2D<float4> g_tDecalNormal : register(t11);
Texture2D<float4> g_tDecalAux : register(t12);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    nointerpolation uint decalIndex : COLOR1;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 auxiliary : SV_Target3;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    float4 projectionRow0 = g_decalRecords[input.decalIndex].projectionRow0;
    float4 projectionRow1 = g_decalRecords[input.decalIndex].projectionRow1;
    float4 projectionRow2 = g_decalRecords[input.decalIndex].projectionRow2;
#if WAVE5B_RECORD_NORMAL_HAS_T2
    float3 marchFrame = g_decalRecords[input.decalIndex].marchFrame.xyz;
#endif
    float3 decalDirection = g_decalRecords[input.decalIndex].decalDirection;
    float3 recordNormal = g_decalRecords[input.decalIndex].recordNormal.xyz;
    float4 uvBounds = g_decalRecords[input.decalIndex].uvBounds;
    float decalOpacity = g_decalRecords[input.decalIndex].pad0xd0.w;

    float2 screenUv = input.position.xy * cb2[0].xy;
    float depth = g_tDepth.SampleLevel(g_sDepth, screenUv, 0.0).x;
    float2 encodedNormal = g_tNormal.SampleLevel(g_sNormal, screenUv, 0.0).xy;
    float2 octahedral = encodedNormal * 4.0 - 2.0;
    float normalLengthSq = dot(octahedral, octahedral);
    float2 octahedralFactors =
        1.0 - normalLengthSq * float2(0.25, 0.5);
    float3 surfaceNormal = float3(
        octahedral * sqrt(octahedralFactors.x),
        -octahedralFactors.y);

    float projectedDepth;
    float4 inverseRow0;
    float4 inverseRow1;
    float4 inverseRow2;
    float4 inverseRow3;
    if (depth <= 0.01)
    {
        projectedDepth = depth * 100.0;
        inverseRow0 = cb12[24];
        inverseRow1 = cb12[25];
        inverseRow2 = cb12[26];
        inverseRow3 = cb12[27];
    }
    else
    {
        projectedDepth = depth * 1.01 - 0.01;
        inverseRow0 = cb12[20];
        inverseRow1 = cb12[21];
        inverseRow2 = cb12[22];
        inverseRow3 = cb12[23];
    }

    float2 clipPosition = float2(
        screenUv.x * cb2[0].z,
        1.0 - screenUv.y * cb2[0].w) * 2.0 - 1.0;
    float4 homogeneousPosition = float4(clipPosition, projectedDepth, 1.0);
    float4 reconstructedPosition = float4(
        dot(inverseRow0, homogeneousPosition),
        dot(inverseRow1, homogeneousPosition),
        dot(inverseRow2, homogeneousPosition),
        dot(inverseRow3, homogeneousPosition));
    reconstructedPosition.xyz /= reconstructedPosition.w;
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            cb12[14],
            wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        output.normal = 0.0;
        output.material = 0.0;
        output.auxiliary = 0.0;
        return output;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            reconstructedPosition.xyz,
            TerrainShadows::TerrainShadowsSampler,
            cb12[12],
            cb12[13],
            cb12[14],
            cb12[35],
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        output.normal = 0.0;
        output.material = 0.0;
        output.auxiliary = 0.0;
        return output;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            reconstructedPosition.xyz,
            cb12[12],
            cb12[13],
            cb12[14],
            cb12[35],
            waterDebugColor))
    {
        output.color = waterDebugColor;
        output.normal = 0.0;
        output.material = 0.0;
        output.auxiliary = 0.0;
        return output;
    }
#endif
    float3 viewDirection = normalize(-reconstructedPosition.xyz);

#if WAVE5B_RECORD_NORMAL_HAS_WETNESS
    bool disableWetness = cb2[WAVE5B_RECORD_NORMAL_WETNESS_INDEX].y == -1.0;
    float wetness = min(
        pow(1.0 - dot(viewDirection, recordNormal), cb2[WAVE5B_RECORD_NORMAL_WETNESS_INDEX].y),
        1.0);
    wetness = 1.0 + cb12[30].x * (wetness - 1.0);
    wetness = disableWetness ? 1.0 : wetness;
#endif

    float4 worldPosition = float4(reconstructedPosition.xyz, 1.0);
    float3 projectedPosition = float3(
        dot(projectionRow0, worldPosition),
        dot(projectionRow1, worldPosition),
        dot(projectionRow2, worldPosition));

#if WAVE5B_RECORD_NORMAL_HAS_T2
    float3 localPosition = float3(
        dot(marchFrame.xyz, reconstructedPosition.xyz),
        dot(decalDirection, reconstructedPosition.xyz),
        dot(recordNormal, reconstructedPosition.xyz));
    localPosition = normalize(localPosition);
    float2 localDirection = normalize(localPosition.xy);
    float localRadius = sqrt(dot(localPosition, localPosition) - localPosition.z * localPosition.z);
    float2 marchDirection = localDirection * (localRadius / localPosition.z) * cb2[6].x;
    float facing = dot(recordNormal, viewDirection);
    float maxSteps = min(facing * -56.0 + 72.0, cb2[6].y);
    float inverseMaxSteps = 1.0 / maxSteps;
    float stopCounter = maxSteps + 1.0;
    float2 marchCursor = projectedPosition.xy;
    float marchCounter = 0.0;
    float2 previousTraceAndSample = float2(1.0, 1.0);
    float4 hit = 0.0;

    [loop]
    while (marchCounter < maxSteps)
    {
        marchCursor += inverseMaxSteps * marchDirection;
        float sampledMarch = g_tMarch.SampleLevel(g_sMarch, marchCursor, 0.0).x;
        float trace = previousTraceAndSample.x - inverseMaxSteps;
        bool crossed = trace < sampledMarch;
        marchCounter = crossed ? stopCounter : marchCounter + 1.0;
        hit = crossed
            ? float4(trace, sampledMarch, previousTraceAndSample.x, previousTraceAndSample.y)
            : hit;
        previousTraceAndSample = float2(trace, sampledMarch);
    }

    float previousGap = hit.z - hit.w;
    float currentGap = hit.x - hit.y;
    float denominator = previousGap - currentGap;
    float fraction = 1.0 - (hit.x * previousGap - currentGap * hit.z) / denominator;
    fraction = denominator == 0.0 ? 1.0 : fraction;
    projectedPosition.xy += marchDirection * fraction;
#endif

    clip(projectedPosition.xy - uvBounds.xy);
    clip(uvBounds.zw - projectedPosition.xy);
    clip(1.0 - projectedPosition.z);
    clip(projectedPosition.z);

    float decalLod = g_tDecalColor.CalculateLevelOfDetail(g_sDecalColor, projectedPosition.xy);
    float3 projectionDirection = normalize(-projectionRow2.xyz);
    float3 derivativeX = normalize(ddx_coarse(reconstructedPosition.xyz));
    float3 derivativeY = normalize(ddy_coarse(reconstructedPosition.xyz));
    float3 geometricNormal = normalize(cross(derivativeX, derivativeY));
    float geometricFacing = dot(geometricNormal, projectionDirection) - 0.3;
    bool geometricBack = geometricFacing < 0.0;
    float surfaceFacing = dot(projectionDirection, surfaceNormal) - 0.3;
    bool surfaceBack = surfaceFacing < 0.0;

    if (geometricBack && surfaceBack)
        discard;

    float4 decalColor = g_tDecalColor.SampleLevel(g_sDecalColor, projectedPosition.xy, decalLod);
    float2 auxiliary = g_tDecalAux.SampleLevel(g_sDecalAux, projectedPosition.xy, decalLod).xy;
    float2 decalNormalXy = g_tDecalNormal.SampleLevel(g_sDecalNormal, projectedPosition.xy, decalLod).xy;
    float angleFade = geometricBack ? min(max(surfaceFacing, 0.0), 0.25) : 0.25;
    float alpha = decalColor.w * angleFade * 4.0;
    clip(alpha - 4.0 / 255.0);
    alpha *= decalOpacity;

    float2 tangentNormalXy = decalNormalXy * 2.0 - 1.0;
    float tangentNormalZ = sqrt(max(1.0 - dot(tangentNormalXy, tangentNormalXy), 0.0));
    float3 tangentNormal = float3(tangentNormalXy, tangentNormalZ);
    float3 basisTangent = cross(recordNormal, decalDirection);
    float3 basisBitangent = cross(basisTangent, -recordNormal);
    float3 mappedNormal = normalize(
        mul(tangentNormal, float3x3(basisTangent, basisBitangent, recordNormal)));

    output.normal.z = -mappedNormal.z;
    output.normal.xy = mappedNormal.xy / sqrt(mappedNormal.z * -8.0 + 8.0) + 0.5;
#if WAVE5B_RECORD_NORMAL_HAS_WETNESS
    output.material.z = sqrt(wetness * cb2[3].x * 0.02);
#else
    output.material.z = sqrt(cb2[3].x * 0.02);
#endif
    output.material.w = saturate(cb2[3].y);
    output.color = float4(decalColor.xyz, alpha);
    output.normal.w = alpha;
    output.material.x = 0.0;
    output.material.y = cb2[3].y / 255.0;
    output.auxiliary = float4(auxiliary, 0.0, alpha);
    return output;
}
#endif

#ifdef BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT

#if defined(WAVE5B_SSS_RECORD_NORMAL_SHAPE)
#error WAVE5B_SSS_RECORD_NORMAL_SHAPE selects the record-normal root, not this one
#endif

#if !defined(WAVE5B_SSS_SURFACE_CONTACT_SHAPE)
#error WAVE5B_SSS_SURFACE_CONTACT_SHAPE is required
#endif

#if WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 1
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 28
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 4
#define WAVE5B_SURFACE_CONTACT_HAS_T2 0
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 0
#elif WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 2
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 31
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 7
#define WAVE5B_SURFACE_CONTACT_HAS_T2 0
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 1
#define WAVE5B_SURFACE_CONTACT_WETNESS_INDEX 6
#elif WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 3
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 28
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 7
#define WAVE5B_SURFACE_CONTACT_HAS_T2 1
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 0
#elif WAVE5B_SSS_SURFACE_CONTACT_SHAPE == 4
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 31
#define WAVE5B_SURFACE_CONTACT_CB2_COUNT 8
#define WAVE5B_SURFACE_CONTACT_HAS_T2 1
#define WAVE5B_SURFACE_CONTACT_HAS_WETNESS 1
#define WAVE5B_SURFACE_CONTACT_WETNESS_INDEX 7
#else
#error Unsupported SSS MRT surface/contact shape
#endif

#if defined(TERRAIN_SHADOWS_FULLSCREEN_DEBUG) || defined(WATER_EFFECTS_FULLSCREEN_DEBUG)
#undef WAVE5B_SURFACE_CONTACT_CB12_COUNT
#define WAVE5B_SURFACE_CONTACT_CB12_COUNT 36
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    float4 cb12[WAVE5B_SURFACE_CONTACT_CB12_COUNT];
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2[WAVE5B_SURFACE_CONTACT_CB2_COUNT];
};

SamplerState g_sNormal : register(s1);
#if WAVE5B_SURFACE_CONTACT_HAS_T2
SamplerState g_sMarch : register(s2);
#endif
SamplerState g_sDepth : register(s7);
SamplerState g_sDecalColor : register(s9);
SamplerState g_sDecalNormal : register(s11);
SamplerState g_sDecalAux : register(s12);

Texture2D<float4> g_tNormal : register(t1);
#if WAVE5B_SURFACE_CONTACT_HAS_T2
Texture2D<float4> g_tMarch : register(t2);
#endif
Texture2D<float4> g_tDepth : register(t7);

struct DecalRecord
{
    float4 projectionRow0;
    float4 projectionRow1;
    float4 projectionRow2;
    float4 pad0x30;
    float4 pad0x40;
    float4 pad0x50;
    float4 pad0x60;
    float4 pad0x70;
    float4 marchFrame;
    float3 decalDirection;
    float pad0x9c;
    float4 recordNormal;
    float4 pad0xb0;
    float4 uvBounds;
    float4 pad0xd0;
};

StructuredBuffer<DecalRecord> g_decalRecords : register(t8);
Texture2D<float4> g_tDecalColor : register(t9);
Texture2D<float4> g_tDecalNormal : register(t11);
Texture2D<float4> g_tDecalAux : register(t12);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    nointerpolation uint decalIndex : COLOR1;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
    float4 auxiliary : SV_Target3;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;
    float4 projectionRow0 = g_decalRecords[input.decalIndex].projectionRow0;
    float4 projectionRow1 = g_decalRecords[input.decalIndex].projectionRow1;
    float4 projectionRow2 = g_decalRecords[input.decalIndex].projectionRow2;
#if WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 marchFrame = g_decalRecords[input.decalIndex].marchFrame.xyz;
#endif
    float3 decalDirection = g_decalRecords[input.decalIndex].decalDirection;
#if WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 recordNormal = g_decalRecords[input.decalIndex].recordNormal.xyz;
#endif
    float4 uvBounds = g_decalRecords[input.decalIndex].uvBounds;
    float decalOpacity = g_decalRecords[input.decalIndex].pad0xd0.w;

    float2 screenUv = input.position.xy * cb2[0].xy;
    float depth = g_tDepth.SampleLevel(g_sDepth, screenUv, 0.0).x;
    float2 encodedNormal = g_tNormal.SampleLevel(g_sNormal, screenUv, 0.0).xy;
    float2 octahedral = encodedNormal * 4.0 - 2.0;
    float normalLengthSq = dot(octahedral, octahedral);
    float2 octahedralFactors =
        1.0 - normalLengthSq * float2(0.25, 0.5);
    float3 surfaceNormal = float3(
        octahedral * sqrt(octahedralFactors.x),
        -octahedralFactors.y);
#if !WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 recordNormal = surfaceNormal;
#endif

    float projectedDepth;
    float4 inverseRow0;
    float4 inverseRow1;
    float4 inverseRow2;
    float4 inverseRow3;
    if (depth <= 0.01)
    {
        projectedDepth = depth * 100.0;
        inverseRow0 = cb12[24];
        inverseRow1 = cb12[25];
        inverseRow2 = cb12[26];
        inverseRow3 = cb12[27];
    }
    else
    {
        projectedDepth = depth * 1.01 - 0.01;
        inverseRow0 = cb12[20];
        inverseRow1 = cb12[21];
        inverseRow2 = cb12[22];
        inverseRow3 = cb12[23];
    }

    float2 clipPosition = float2(
        screenUv.x * cb2[0].z,
        1.0 - screenUv.y * cb2[0].w) * 2.0 - 1.0;
    float4 homogeneousPosition = float4(clipPosition, projectedDepth, 1.0);
    float4 reconstructedPosition = float4(
        dot(inverseRow0, homogeneousPosition),
        dot(inverseRow1, homogeneousPosition),
        dot(inverseRow2, homogeneousPosition),
        dot(inverseRow3, homogeneousPosition));
    reconstructedPosition.xyz /= reconstructedPosition.w;
#ifdef WETNESS_EFFECTS_FULLSCREEN_DEBUG
    float4 wetnessDebugColor;
    if (WetnessEffects::TryGetDebugColorFromScreenPosition(
            input.position.xy,
            cb12[14],
            wetnessDebugColor))
    {
        output.color = wetnessDebugColor;
        output.normal = 0.0;
        output.material = 0.0;
        output.auxiliary = 0.0;
        return output;
    }
#endif
#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
    float4 terrainDebugColor;
    if (TerrainShadows::TryGetDebugColorFromViewPosition(
            reconstructedPosition.xyz,
            TerrainShadows::TerrainShadowsSampler,
            cb12[12],
            cb12[13],
            cb12[14],
            cb12[35],
            terrainDebugColor))
    {
        output.color = terrainDebugColor;
        output.normal = 0.0;
        output.material = 0.0;
        output.auxiliary = 0.0;
        return output;
    }
#endif
#ifdef WATER_EFFECTS_FULLSCREEN_DEBUG
    float4 waterDebugColor;
    if (WaterEffects::TryGetDebugColorFromViewPosition(
            reconstructedPosition.xyz,
            cb12[12],
            cb12[13],
            cb12[14],
            cb12[35],
            waterDebugColor))
    {
        output.color = waterDebugColor;
        output.normal = 0.0;
        output.material = 0.0;
        output.auxiliary = 0.0;
        return output;
    }
#endif
    float3 viewDirection = normalize(-reconstructedPosition.xyz);

#if WAVE5B_SURFACE_CONTACT_HAS_WETNESS
    bool disableWetness = cb2[WAVE5B_SURFACE_CONTACT_WETNESS_INDEX].y == -1.0;
    float wetness = min(
        pow(1.0 - dot(viewDirection, recordNormal), cb2[WAVE5B_SURFACE_CONTACT_WETNESS_INDEX].y),
        1.0);
    wetness = 1.0 + cb12[30].x * (wetness - 1.0);
    wetness = disableWetness ? 1.0 : wetness;
#endif

    float4 worldPosition = float4(reconstructedPosition.xyz, 1.0);
    float3 projectedPosition = float3(
        dot(projectionRow0, worldPosition),
        dot(projectionRow1, worldPosition),
        dot(projectionRow2, worldPosition));

#if WAVE5B_SURFACE_CONTACT_HAS_T2
    float3 localPosition = float3(
        dot(marchFrame.xyz, reconstructedPosition.xyz),
        dot(decalDirection, reconstructedPosition.xyz),
        dot(recordNormal, reconstructedPosition.xyz));
    localPosition = normalize(localPosition);
    float2 localDirection = normalize(localPosition.xy);
    float localRadius = sqrt(dot(localPosition, localPosition) - localPosition.z * localPosition.z);
    float2 marchDirection = localDirection * (localRadius / localPosition.z) * cb2[6].x;
    float facing = dot(recordNormal, viewDirection);
    float maxSteps = min(facing * -56.0 + 72.0, cb2[6].y);
    float inverseMaxSteps = 1.0 / maxSteps;
    float stopCounter = maxSteps + 1.0;
    float2 marchCursor = projectedPosition.xy;
    float marchCounter = 0.0;
    float2 previousTraceAndSample = float2(1.0, 1.0);
    float4 hit = 0.0;

    [loop]
    while (marchCounter < maxSteps)
    {
        marchCursor += inverseMaxSteps * marchDirection;
        float sampledMarch = g_tMarch.SampleLevel(g_sMarch, marchCursor, 0.0).x;
        float trace = previousTraceAndSample.x - inverseMaxSteps;
        bool crossed = trace < sampledMarch;
        marchCounter = crossed ? stopCounter : marchCounter + 1.0;
        hit = crossed
            ? float4(trace, sampledMarch, previousTraceAndSample.x, previousTraceAndSample.y)
            : hit;
        previousTraceAndSample = float2(trace, sampledMarch);
    }

    float previousGap = hit.z - hit.w;
    float currentGap = hit.x - hit.y;
    float denominator = previousGap - currentGap;
    float fraction = 1.0 - (hit.x * previousGap - currentGap * hit.z) / denominator;
    fraction = denominator == 0.0 ? 1.0 : fraction;
    projectedPosition.xy += marchDirection * fraction;
#endif

#if WAVE5B_SURFACE_CONTACT_HAS_T2
    const float kTapOffset[7] = { 0.22, 0.1925, 0.165, 0.1375, 0.11, 0.0825, 0.055 };
    const float kTapBias[7] = { 0.88, 0.77, 0.66, 0.55, 0.44, 0.33, 0.22 };
    const float kTapWeight[7] = { 1.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0 };
    float2 shadowStep = g_decalRecords[input.decalIndex].pad0xd0.xy;
    float centerOcclusion =
        g_tMarch.SampleLevel(g_sMarch, projectedPosition.xy, 0.0).x;
    float contact = 0.0;
    [unroll]
    for (int tap = 0; tap < 7; ++tap)
    {
        float2 tapUv = shadowStep * kTapOffset[tap] + projectedPosition.xy;
        float tapValue = g_tMarch.SampleLevel(g_sMarch, tapUv, 0.0).x;
        tapValue = (tapValue - centerOcclusion) - kTapBias[tap];
        tapValue *= kTapWeight[tap];
        contact = tap == 0 ? tapValue : max(tapValue, contact);
    }
    float contactShadow =
        saturate(1.0 - contact * cb2[6].x * 10.0) * 0.8 + 0.2;
#endif

    clip(projectedPosition.xy - uvBounds.xy);
    clip(uvBounds.zw - projectedPosition.xy);
    clip(1.0 - projectedPosition.z);
    clip(projectedPosition.z);

    float decalLod = g_tDecalColor.CalculateLevelOfDetail(g_sDecalColor, projectedPosition.xy);
    float3 projectionDirection = normalize(-projectionRow2.xyz);
    float3 derivativeX = normalize(ddx_coarse(reconstructedPosition.xyz));
    float3 derivativeY = normalize(ddy_coarse(reconstructedPosition.xyz));
    float3 geometricNormal = normalize(cross(derivativeX, derivativeY));
    float geometricFacing = dot(geometricNormal, projectionDirection) - 0.3;
    bool geometricBack = geometricFacing < 0.0;
    float surfaceFacing = dot(projectionDirection, surfaceNormal) - 0.3;
    bool surfaceBack = surfaceFacing < 0.0;

    if (geometricBack && surfaceBack)
        discard;

    float4 decalColor = g_tDecalColor.SampleLevel(g_sDecalColor, projectedPosition.xy, decalLod);
    float2 auxiliary = g_tDecalAux.SampleLevel(g_sDecalAux, projectedPosition.xy, decalLod).xy;
    float2 decalNormalXy = g_tDecalNormal.SampleLevel(g_sDecalNormal, projectedPosition.xy, decalLod).xy;
    float angleFade = geometricBack ? min(max(surfaceFacing, 0.0), 0.25) : 0.25;
    float alpha = decalColor.w * angleFade * 4.0;
    clip(alpha - 4.0 / 255.0);
    alpha *= decalOpacity;

    float2 tangentNormalXy = decalNormalXy * 2.0 - 1.0;
    float tangentNormalZ = sqrt(max(1.0 - dot(tangentNormalXy, tangentNormalXy), 0.0));
    float3 tangentNormal = float3(tangentNormalXy, tangentNormalZ);
    float3 basisTangent = cross(recordNormal, decalDirection);
    float3 basisBitangent = cross(basisTangent, -recordNormal);
    float3 mappedNormal = normalize(
        mul(tangentNormal, float3x3(basisTangent, basisBitangent, recordNormal)));

    output.normal.z = -mappedNormal.z;
    output.normal.xy = mappedNormal.xy / sqrt(mappedNormal.z * -8.0 + 8.0) + 0.5;
#if WAVE5B_SURFACE_CONTACT_HAS_WETNESS
    output.material.z = sqrt(wetness * cb2[3].x * 0.02);
#else
    output.material.z = sqrt(cb2[3].x * 0.02);
#endif
    output.material.w = saturate(cb2[3].y);
#if WAVE5B_SURFACE_CONTACT_HAS_T2
    output.color = float4(decalColor.xyz * contactShadow, alpha);
#else
    output.color = float4(decalColor.xyz, alpha);
#endif
    output.normal.w = alpha;
    output.material.x = 0.0;
    output.material.y = cb2[3].y / 255.0;
    output.auxiliary = float4(auxiliary, 0.0, alpha);
    return output;
}
#endif

#ifdef BSDFCOMPOSITE_VS
cbuffer Scene : register(b12)
{
    float4 scene[36];
};

cbuffer Geometry : register(b2)
{
    float4 geometry[5];
    uint4 geometryIndex;
};

struct InstanceData
{
    float4 data[14];
};

StructuredBuffer<InstanceData> instances : register(t8);

struct VSInput
{
    float4 position : POSITION0;
#ifdef TEXTURE
    float2 texcoord : TEXCOORD0;
#endif
#ifdef DECAL
    uint instanceID : SV_InstanceID;
#endif
};

struct VSOutput
{
    float4 position : SV_POSITION0;
#ifdef TEXTURE
    float3 texcoord : TEXCOORD0;
#endif
#ifdef DECAL
    uint instanceIndex : COLOR1;
#endif
};

VSOutput main(VSInput input)
{
    VSOutput output;
#ifdef DECAL
    uint index = input.instanceID + geometryIndex.x;
    float4 localPosition = float4(input.position.xyz, 1.0);
    float4 worldPosition;
    worldPosition.x = dot(instances[index].data[4], localPosition);
    worldPosition.y = dot(instances[index].data[5], localPosition);
    worldPosition.z = dot(instances[index].data[6], localPosition);
    worldPosition.w = dot(instances[index].data[7], localPosition);
    output.instanceIndex = index;
    output.position.x = dot(scene[8], worldPosition);
    output.position.y = dot(scene[9], worldPosition);
    output.position.z = dot(scene[10], worldPosition);
    output.position.w = dot(scene[11], worldPosition);
#else
#ifdef GEOMETRY
    float4 localPosition = float4(input.position.xyz, 1.0);
    float4 worldPosition;
    worldPosition.x = dot(float4(geometry[0].xyz, geometry[0].w - scene[35].x), localPosition);
    worldPosition.y = dot(float4(geometry[1].xyz, geometry[1].w - scene[35].y), localPosition);
    worldPosition.z = dot(float4(geometry[2].xyz, geometry[2].w - scene[35].z), localPosition);
    worldPosition.w = dot(geometry[3], localPosition);
    output.position.x = dot(scene[8], worldPosition);
    output.position.y = dot(scene[9], worldPosition);
    output.position.z = dot(scene[10], worldPosition);
    output.position.w = dot(scene[11], worldPosition);
#else
    output.position = float4(input.position.xyz, 1.0);
#endif
#endif
#ifdef TEXTURE
    output.texcoord = float3(input.texcoord, 1.0);
#endif
    return output;
}
#endif

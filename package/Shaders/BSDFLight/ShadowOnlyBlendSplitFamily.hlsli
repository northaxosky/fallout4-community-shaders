// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
#if !defined(DIRECTIONAL) || !defined(SHADOW_ONLY) || !defined(BLENDSPLIT)
#  error "this source is the native DIRECTIONAL + SHADOW_ONLY + BLENDSPLIT family; define all three"
#endif
#if !defined(SHADOW) || !defined(SPECULAR) || !defined(RGBSPEC)
#  error "every native SHADOW_ONLY + BLENDSPLIT blob carries SHADOW, SPECULAR and RGBSPEC"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 1
#  error "the reconstructed SHADOW_ONLY + BLENDSPLIT family is DIRSPLITS=1 only"
#endif
#if defined(POINTOMNI) || defined(HALFOMNI) || defined(SPOT) || defined(POINTSPOT)
#  error "mixed light kinds; this family is DIRECTIONAL"
#endif
#if defined(LIGHT_TYPE)
#  error "LIGHT_TYPE is the legacy adapter axis in DeferredFamily.hlsli, not a native macro"
#endif
#if defined(AMBIENT_IBL_IN_LIGHT)
#  error "AMBIENT_IBL_IN_LIGHT is a legacy adapter axis in DeferredFamily.hlsli, not a native macro"
#endif
#if defined(GOBOPROJECTION)
#  error "GOBOPROJECTION declares t7/s7 and is a different resource contract"
#endif
#if defined(FILTER_PCSS) || defined(FILTER_PCSSPOISSON)
#  error "no BLENDSPLIT blob carries a blocker search; the raw t4/s4 tap is absent from this family"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_POISSON)) != 1
#  error "define exactly one of FILTER_PCF1, FILTER_PCF9, FILTER_POISSON"
#endif

#include "../Common/DeferredContracts.hlsli"

#ifdef FILTER_POISSON
#  include "../Common/ShadowPoissonKernel.hlsli"
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 cb2_pad_1_5[5];

    float4 cb2_ambient_row0;
    float4 cb2_ambient_row1;
    float4 cb2_ambient_row2;

    float4 cb2_idx9_cascade_slice;

    float4 cb2_pad_10;

    float4 cb2_shadowproj_row0;
    float4 cb2_shadowproj_row1;
    float4 cb2_shadowproj_row2;

    float4 cb2_pad_14_19[6];

    float4 cb2_idx20_shadow_sample_param;

    float4 cb2_idx21_cascade_world_scale[3];

    float4 cb2_idx24_distance_fade;
};

#ifdef AMBIENT
Texture2D<float4> g_tGbufferNormal : register(t1);
SamplerState g_sGbufferNormal : register(s1);

Texture2D<float4> g_tGbufferMaterial : register(t2);
SamplerState g_sGbufferMaterial : register(s2);
#endif

Texture2D<float4> g_tMainDepth : register(t3);
SamplerState g_sMainDepth : register(s3);

Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);

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

    float depth = g_tMainDepth.SampleGrad(g_sMainDepth, uv4.xy,
                                          ddx_coarse(uv4.x).xx,
                                          ddy_coarse(uv4.y).xx).x;

    float  linearDepth;
    float4 reprojRow0, reprojRow1, reprojRow2, reprojRow3;
    [branch]
    if (0.01 >= depth)
    {
        linearDepth = depth * 100.0;
        reprojRow0 = NearReproj_row0;
        reprojRow1 = NearReproj_row1;
        reprojRow2 = NearReproj_row2;
        reprojRow3 = NearReproj_row3;
    }
    else
    {
        linearDepth = depth * 1.01 - 0.01;
        reprojRow0 = FarReproj_row0;
        reprojRow1 = FarReproj_row1;
        reprojRow2 = FarReproj_row2;
        reprojRow3 = FarReproj_row3;
    }

    float2 screenUV = uv4.zw * float2(1.0, -1.0) + float2(0.0, 1.0);
    float4 pos4 = float4(screenUV * 2.0 - 1.0, linearDepth, 1.0);
    float4 posViewH;
    posViewH.x = dot(reprojRow0, pos4);
    posViewH.y = dot(reprojRow1, pos4);
    posViewH.z = dot(reprojRow2, pos4);
    posViewH.w = dot(reprojRow3, pos4);
    float3 posView = posViewH.xyz / posViewH.www;

#ifdef AMBIENT
    float3 material = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv4.xy).xyw;
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv4.xy).xy * 4.0 - 2.0;

    float  encSq = dot(normalEnc, normalEnc);
    float3 normal;
    normal.xy = normalEnc * sqrt(1.0 - encSq * 0.25);
    normal.z = -(1.0 - encSq * 0.5);

    float3 ambientDiffuse;
    ambientDiffuse.x = dot(cb2_ambient_row0, float4(normal, 1.0));
    ambientDiffuse.y = dot(cb2_ambient_row1, float4(normal, 1.0));
    ambientDiffuse.z = dot(cb2_ambient_row2, float4(normal, 1.0));
    ambientDiffuse = pow(ambientDiffuse, 2.2);

    bool isMaterial1 = abs(material.z * 255.0 - 1.0) < 0.25;
#endif

    float4 posViewH1 = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_shadowproj_row0, posViewH1);
    shadowUV.y = dot(cb2_shadowproj_row1, posViewH1);
    float shadowZ = min(dot(cb2_shadowproj_row2, posViewH1), 0.999999);
    float slice = cb2_idx9_cascade_slice.y;

    float shadow;

#if defined(FILTER_PCF1)
    shadow = g_tCascadeShadowCmp.SampleCmpLevelZero(
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
    shadow = sum * (1.0 / 9.0);

#elif defined(FILTER_POISSON)
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
    uint  cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - 0.275 * rcpWorldRange;

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
    shadow = sum * 0.0625;
#else
#  error "unreachable: the filter guard above admits exactly PCF1, PCF9 or POISSON"
#endif

    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2 = distNorm * distNorm;
    float dist4 = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;

#ifndef AMBIENT
    float3 result = fadeFactor * (shadow - 1.0) + 1.0;

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
#else
    float shadowBlend = fadeFactor * (shadow - 1.0);
    float splitShadow = shadowBlend + 1.0;

    float3 ambientSpecular;
    [branch]
    if (isMaterial1)
    {
        ambientSpecular = 0.0;
    }
    else
    {
        float3 view = normalize(-posView);
        float  NdotV = dot(normal, view);
        float3 reflected = (NdotV + NdotV) * normal - view;
        float  fresnel = pow(1.0 - saturate(NdotV), 3.0 - material.x) * 0.25;

        float3 ambientReflected;
        ambientReflected.x = dot(cb2_ambient_row0, float4(reflected, 1.0));
        ambientReflected.y = dot(cb2_ambient_row1, float4(reflected, 1.0));
        ambientReflected.z = dot(cb2_ambient_row2, float4(reflected, 1.0));
        ambientSpecular = (fresnel * pow(ambientReflected, 2.2)) * material.y;
    }

    output.specular = float4(splitShadow.xxx, 1.0) + float4(ambientSpecular, 0.0);
    output.diffuse = float4(ambientDiffuse, 1.0) + float4(splitShadow.xxx, shadowBlend);
#endif
    return output;
}

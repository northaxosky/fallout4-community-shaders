// SPDX-License-Identifier: GPL-3.0-or-later WITH FO4-CS-Modding-Exception
#if !defined(DIRECTIONAL) || !defined(SHADOW_ONLY)
#  error "this source is the native DIRECTIONAL + SHADOW_ONLY family; define both"
#endif
#ifndef SHADOW
#  error "every native SHADOW_ONLY blob carries SHADOW=1"
#endif
#if !defined(DIRSPLITS) || DIRSPLITS != 1
#  error "the reconstructed SHADOW_ONLY family is DIRSPLITS=1 only"
#endif
#if defined(AMBIENT) || defined(BLENDSPLIT)
#  error "no DIRSPLITS=1 SHADOW_ONLY blob with a full FILTER_* set carries AMBIENT or BLENDSPLIT"
#endif
#if (defined(FILTER_PCF1) + defined(FILTER_PCF9) + defined(FILTER_PCSS) \
      + defined(FILTER_POISSON) + defined(FILTER_PCSSPOISSON)) > 1
#  error "FILTER_* macros are mutually exclusive"
#endif

#include "../Common/DeferredContracts.hlsli"

#if defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  include "../Common/ShadowPoissonKernel.hlsli"
#endif

#if !defined(FILTER_PCF1) && !defined(FILTER_PCF9) && !defined(FILTER_POISSON)
#  define FO4_SHADOW_RAW_TAP 1
#endif

#if defined(FILTER_PCF1) || defined(FILTER_PCF9) || defined(FILTER_PCSS) \
    || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  define FO4_SHADOW_CMP_TAP 1
#endif

#if defined(FILTER_PCSS) || defined(FILTER_POISSON) || defined(FILTER_PCSSPOISSON)
#  define FO4_SHADOW_DYNAMIC_CB2 1
#endif

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 ScreenSize;

    float4 cb2_pad_1_8[8];

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

Texture2D<float4> g_tGbufferNormal : register(t1);

Texture2D<float4> g_tGbufferMaterial : register(t2);

Texture2D<float4> g_tMainDepth : register(t3);

#ifdef FO4_SHADOW_RAW_TAP
Texture2DArray<float4> g_tCascadeShadowRaw : register(t4);
SamplerState g_sCascadeShadowRaw : register(s4);
#endif

#ifdef FO4_SHADOW_CMP_TAP
Texture2DArray<float4> g_tCascadeShadowCmp : register(t5);
SamplerComparisonState g_sCascadeShadowCmp : register(s5);
#endif

SamplerState g_sGbufferNormal   : register(s1);
SamplerState g_sGbufferMaterial : register(s2);
SamplerState g_sMainDepth       : register(s3);

float ComputeSlopeBias(float c)
{
    float sqrtTerm = sqrt(1.0 - c);
    float acosApprox = ((-0.0187293 * c + 0.0742610) * c - 0.2121144) * c + 1.5707288;
    acosApprox *= sqrtTerm;
    float sinA, cosA;
    sincos(acosApprox, sinA, cosA);
    return (sinA / cosA) * 0.08;
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

    float  materialCode = g_tGbufferMaterial.Sample(g_sGbufferMaterial, uv4.xy).w;
    float2 normalEnc = g_tGbufferNormal.Sample(g_sGbufferNormal, uv4.xy).xy * 4.0 - 2.0;

    float encodedZ = 1.0 - dot(normalEnc, normalEnc) * 0.5;
    bool  isMaterial1 = abs(materialCode * 255.0 - 1.0) < 0.25;
    float slopeBias = ComputeSlopeBias(max(encodedZ, 0.0));

#ifdef FILTER_POISSON
    float poissonBias = isMaterial1 ? slopeBias : 0.275;
#endif

    float4 posViewH1 = float4(posView, 1.0);
    float2 shadowUV;
    shadowUV.x = dot(cb2_shadowproj_row0, posViewH1);
    shadowUV.y = dot(cb2_shadowproj_row1, posViewH1);
    float shadowZ = min(dot(cb2_shadowproj_row2, posViewH1), 0.999999);
    float slice = cb2_idx9_cascade_slice.y;

#ifdef FILTER_POISSON
    float kernelScale = cb2_idx20_shadow_sample_param.z * 3.0;
#endif

#ifdef FO4_SHADOW_DYNAMIC_CB2
    uint   cascade = (uint)cb2_idx9_cascade_slice.y;
    float4 cascadeScale = cb2_idx21_cascade_world_scale[cascade];
#endif

    float shadow;

#if defined(FILTER_PCF1)
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    shadow = g_tCascadeShadowCmp.SampleCmpLevelZero(
        g_sCascadeShadowCmp, float3(shadowUV, slice), zRef);

#elif defined(FILTER_PCF9)
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
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
                g_sCascadeShadowCmp, float3(tapUV, slice), zRef);
        }
    }
    shadow = sum * (1.0 / 9.0);

#elif defined(FILTER_PCSS)
    float2 searchStep = 1.0 / cascadeScale.xy;
    float  zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);

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
            bool   isBlocker = tapDepth < zRef;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y != 0.0)
    {
        float centreDepth = g_tCascadeShadowRaw.Sample(
            g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
        float centreLit = (centreDepth >= zRef) ? 1.0 : 0.0;

        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * zRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
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
                float2 tapUV = (searchStep * float2(offsetX, offsetY)) * 0.5
                    + shadowUV;
                sum = sum + g_tCascadeShadowCmp.SampleCmpLevelZero(
                    g_sCascadeShadowCmp, float3(tapUV, slice), zRef);
            }
        }
        shadow = sum * 0.04;
    }
    else
    {
        shadow = 1.0;
    }

#elif defined(FILTER_POISSON)
    float rcpWorldRange = 1.0 / (cascadeScale.w - cascadeScale.z);
    float zRef = shadowZ - poissonBias * rcpWorldRange;

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

#elif defined(FILTER_PCSSPOISSON)
    float2 searchStep = 1.0 / cascadeScale.xy;
    float  bias = isMaterial1 ? -slopeBias : -0.275;
    float  blockerRef = shadowZ + bias;

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
            bool   isBlocker = tapDepth < blockerRef;
            float2 accumulated = float2(blocker.x + tapDepth, blocker.y + 1.0);
            blocker = isBlocker ? accumulated : blocker;
        }
    }

    if (blocker.y != 0.0)
    {
        float averageBlocker = blocker.x / blocker.y;
        float worldRange = cascadeScale.w - cascadeScale.z;
        float receiverWorld = worldRange * blockerRef + cascadeScale.z;
        float blockerWorld = worldRange * averageBlocker + cascadeScale.z;
        float separation = saturate((receiverWorld - blockerWorld) * (1.0 / 128.0));
        float penumbra = (blockerWorld < cascadeScale.z + 0.001)
            ? 1.9
            : (separation * 1.8 + 0.1);
        float kernelScale = penumbra * searchStep.x;
        float zRef = blockerRef + bias;

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
    }
    else
    {
        shadow = 1.0;
    }

#else
    float sampledDepth = g_tCascadeShadowRaw.Sample(
        g_sCascadeShadowRaw, float3(shadowUV, slice)).x;
    float zRef = shadowZ + (isMaterial1 ? -slopeBias : -0.275);
    shadow = (sampledDepth >= zRef) ? 1.0 : 0.0;
#endif

    float distNorm = saturate(dot(posView, posView) / cb2_idx24_distance_fade.x);
    float dist2 = distNorm * distNorm;
    float dist4 = dist2 * dist2;
    float fadeFactor = 1.0 - dist4 * dist4;

    float3 result = fadeFactor * (shadow - 1.0) + 1.0;

    output.diffuse = result.zzzz;
    output.specular = float4(result.xyz, 1.0);
    return output;
}

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#include "Common/DeferredContracts.hlsli"

cbuffer PerCall_CB0 : register(b0)
{
    float4 ScreenSize;
};

cbuffer PerLight_CB1 : register(b1)
{
    float4 cb1_idx0_color_a;

    float4 cb1_idx1_color_b;

    float4 cb1_pad_2_9[8];

    float4 cb1_idx10;

    float4 cb1_pad_11;

    float4 cb1_idx12_fade_range_a;

    float4 cb1_idx13_fade_range_b;
};

cbuffer PerCall_CB2 : register(b2)
{
    float4 cb2_pad_0_3[4];

    float4 cb2_idx4_scatter_axis_and_scale;
};

cbuffer PerFrame_CB12 : register(b12)
{
    DEFERRED_PERFRAME_CB12_SHARED_BLOCK;
};

Texture2D<float4> g_tLinearDepth : register(t7);

SamplerState g_sDepth : register(s7);

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 rayDir   : TEXCOORD0;
    float4 posUnused      : POSITION;
    float4 texCoord4Unused : TEXCOORD4;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
};

PS_OUTPUT main(PS_INPUT input)
{
    PS_OUTPUT output;

    float3 rayUnit = normalize(input.rayDir.xyz);

    float2 uv = input.position.xy * ScreenSize.xy;

    float depth = g_tLinearDepth.Sample(g_sDepth, uv).x;

    bool isNearPath = (depth <= 0.01);
    float linearizedDepth = isNearPath ? (depth * 100.0) : (depth * 1.01 - 0.01);
    float4 reprojRow0 = isNearPath ? NearReproj_row0 : FarReproj_row0;
    float4 reprojRow1 = isNearPath ? NearReproj_row1 : FarReproj_row1;
    float4 reprojRow2 = isNearPath ? NearReproj_row2 : FarReproj_row2;
    float4 reprojRow3 = isNearPath ? NearReproj_row3 : FarReproj_row3;

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

    float posViewLen = length(posView);

    float3 backRay = -posViewLen * rayUnit;

    float sunDot = dot(backRay, cb2_idx4_scatter_axis_and_scale.xyz);

    float backRayLen = length(backRay);

    float ratio = cb2_idx4_scatter_axis_and_scale.w / sunDot;
    float inv   = 1.0 - ratio;
    float distScaled = backRayLen * inv;

    float distNorm = distScaled / cb1_idx10.w;

    float dotScaled = abs(sunDot) * inv;
    float dotNorm   = dotScaled / cb1_idx10.w;

    float fadePrimary   = saturate(1.0 - distNorm);
    float fadeSecondary = saturate(1.0 - dotNorm);

    float rangeA = cb1_idx12_fade_range_a.x - cb1_idx12_fade_range_a.y;
    float t0     = saturate((fadePrimary - cb1_idx12_fade_range_a.y) / rangeA);

    float invSmoothA = 1.0 - (3.0 - 2.0 * t0) * (t0 * t0);
    float fadeA      = pow(invSmoothA, 0.33);

    output.color.w = fadeA *
        (cb1_idx12_fade_range_a.w - cb1_idx12_fade_range_a.z) +
        cb1_idx12_fade_range_a.z;

    float rangeB = cb1_idx13_fade_range_b.x - cb1_idx13_fade_range_b.y;
    float t1     = saturate((fadeSecondary - cb1_idx13_fade_range_b.y) / rangeB);

    float smoothB = (3.0 - 2.0 * t1) * (t1 * t1);

    float3 colorDelta = cb1_idx0_color_a.xyz - cb1_idx1_color_b.xyz;
    output.color.xyz  = smoothB * colorDelta + cb1_idx1_color_b.xyz;

    return output;
}

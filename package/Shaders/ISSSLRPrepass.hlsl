// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer PerFrame : register(b12)
{
	row_major float4x4 WorldToView;
	row_major float4x4 ViewToClip;
	row_major float4x4 WorldToClip;
	row_major float4x4 ViewToWorld;
	row_major float4x4 ClipToWorld;
	row_major float4x4 ClipToView;
	row_major float4x4 FirstPersonClipToView;
};

cbuffer SSLRConstants : register(b0)
{
	float4 ViewportSize;
	float4 SSLRParams0;
	float4 SSLRParams1;
};

Texture2D<float4> DepthTex : register(t0);
SamplerState DepthSampler : register(s0);
Texture2D<float4> NormalTex : register(t1);
SamplerState NormalSampler : register(s1);
Texture2D<float4> SSRMaskR_CubeMapG_EnvMapScaleBTex : register(t2);
SamplerState SSRMaskR_CubeMapG_EnvMapScaleBSampler : register(s2);

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float2 RayStart: SV_Target0;
	float4 Depth: SV_Target1;
};

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	float ssrMask = SSRMaskR_CubeMapG_EnvMapScaleBTex.Sample(
														 SSRMaskR_CubeMapG_EnvMapScaleBSampler, input.TexCoord)
	                    .x;
	clip(ssrMask * SSLRParams1.z - 0.01);

	float depth = DepthTex.SampleLevel(DepthSampler, input.TexCoord, 0).x;
	float clipZ;
	float4x4 clipToView;
	if (depth <= 0.01) {
		clipZ = depth * 100;
		clipToView = FirstPersonClipToView;
	} else {
		clipZ = depth * 1.01 - 0.01;
		clipToView = ClipToView;
	}
	float2 clipXY = float2(input.TexCoord.x, 1 - input.TexCoord.y) * 2 - 1;
	float4 viewPosition = mul(clipToView, float4(clipXY, clipZ, 1));
	viewPosition.xyz /= viewPosition.w;
	float3 viewDirection = normalize(-viewPosition.xyz);

	float2 encodedNormal = NormalTex.Sample(NormalSampler, input.TexCoord).xy * 4 - 2;
	float f = dot(encodedNormal, encodedNormal);
	float3 normal;
	normal.xy = encodedNormal * sqrt(1 - f / 4);
	normal.z = -(1 - f / 2);
	normal = normalize(normal);
	bool facing = dot(normal, viewDirection) >= 0;

	float3 worldNormal = mul((float3x3)ViewToWorld, normal);
	worldNormal.z *= SSLRParams1.x;
	normal = normalize(mul((float3x3)WorldToView, normalize(worldNormal)));
	float3 reflection = reflect(-viewDirection, normal);
	bool intoScene = reflection.z > SSLRParams0.y;

	float4 end = mul(ViewToClip, float4(viewPosition.xyz + reflection * 1000, 1));
	float3 endScreen = end.w == 0 ? 1 : end.xyz / end.w;
	endScreen.xy = endScreen.xy * float2(0.5, -0.5) + 0.5;
	float3 delta = endScreen - float3(input.TexCoord, depth);
	float3 result = float3(input.TexCoord - depth * (delta.xy / delta.z),
		viewPosition.z);
	result = intoScene ? result : 0;
	result = facing ? result : 0;
	psout.RayStart = result.xy;
	psout.Depth = float4(result.z, 0, 0, 0);
	return psout;
}
#endif

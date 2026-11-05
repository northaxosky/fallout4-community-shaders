// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
#	include "ISMapVS.hlsli"
#endif

#ifdef PSHADER
cbuffer ISNoiseConstants : register(b2)
{
	float2 fTexScroll0;
	float2 fTexScroll1;
	float2 fTexScroll2;
	float fNoiseScale;
	float3 fTexScale;
	float3 fAmplitude;
};

Texture2D<float4> NoiseMap : register(t0);
SamplerState NoiseMapSampler : register(s0);

struct PS_INPUT
{
	float4 Position: POSITION;
	float2 TexCoord: TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target0
{
#	ifdef SCROLL_AND_BLEND
	float noise0 = NoiseMap.Sample(NoiseMapSampler, input.TexCoord * fTexScale.x + fTexScroll0).z * 2 - 1;
	float noise1 = NoiseMap.Sample(NoiseMapSampler, input.TexCoord * fTexScale.y + fTexScroll1).y * 2 - 1;
	float noise2 = NoiseMap.Sample(NoiseMapSampler, input.TexCoord * fTexScale.z + fTexScroll2).x * 2 - 1;
	float blended = noise0 * fAmplitude.x + noise1 * fAmplitude.y + noise2 * fAmplitude.z;
	return float4(saturate(blended * 0.5 + 0.5), 0, 0, 1);
#	else
	static const float d = 1.0 / 256.0;
	float2 uv = input.TexCoord;
	float tl = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(-d, -d)).x);
	float l = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(-d, 0)).x);
	float bl = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(-d, d)).x);
	float t = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(0, -d)).x);
	float b = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(0, d)).x);
	float tr = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(d, -d)).x);
	float r = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(d, 0)).x);
	float br = fNoiseScale * abs(NoiseMap.Sample(NoiseMapSampler, uv + float2(d, d)).x);
	float gx = -tl - 2 * l - bl + tr + 2 * r + br;
	float gy = -tl - 2 * t - tr + bl + 2 * b + br;
	float3 normal = normalize(float3(-gx, gy, 1));
	return float4(normal * 0.5 + 0.5, abs(NoiseMap.Sample(NoiseMapSampler, uv).y));
#	endif
}
#endif

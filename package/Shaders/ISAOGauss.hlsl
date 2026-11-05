// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#define FO4CS_VS_GEOMETRY_OFFSET
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer ISAOGaussConstants : register(b2)
{
	float4 TexelSize;
	float4 CameraParams;
	float2 Direction;
	float Samples;
	float4 SampleWeights[4];
};

Texture2D<float4> AO : register(t0);
SamplerState AOSampler : register(s0);
Texture2D<float4> Depth : register(t1);
SamplerState DepthSampler : register(s1);
Texture2D<float4> Normals : register(t2);
SamplerState NormalsSampler : register(s2);

typedef VS_OUTPUT PS_INPUT;

float GetLinearDepth(float2 uv)
{
	float depth = Depth.Sample(DepthSampler, uv).x;
	return CameraParams.w / ((1 - depth) * CameraParams.z + CameraParams.y);
}

float3 GetNormal(float2 uv)
{
	float2 fenc = Normals.SampleGrad(NormalsSampler, uv, 0, 0).xy * 4 - 2;
	float f = dot(fenc, fenc);
	float g = sqrt(1 - f / 4);
	return float3(fenc * g, -(1 - f / 2));
}

float4 main(PS_INPUT input) : SV_Target0
{
	float centerDepth = GetLinearDepth(input.TexCoord);
	float3 centerNormal = GetNormal(input.TexCoord);
	float2 uv = input.TexCoord - Direction * TexelSize.zw * (Samples - 1) * 0.5;
	float4 sum = 0;
	float totalWeight = 0;
	for (uint i = 0; i < (uint)Samples; i++) {
		float depth = GetLinearDepth(uv);
		float3 normal = GetNormal(uv);
		if (abs(centerDepth - depth) < 10 && abs(dot(centerNormal, normal)) > 0.6) {
			float weight = SampleWeights[i >> 2][i & 3];
			sum += weight * AO.Sample(AOSampler, uv);
			totalWeight += weight;
		}
		uv += Direction * TexelSize.zw;
	}
	return sum / totalWeight;
}
#endif

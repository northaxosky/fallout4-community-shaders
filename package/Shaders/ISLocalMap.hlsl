// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#define FO4CS_VS_GEOMETRY_OFFSET
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer LocalMapConstants : register(b2)
{
	float4 TexelSize;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Normals : register(t2);
SamplerState NormalsSampler : register(s2);

typedef VS_OUTPUT PS_INPUT;

float3 Tap(float2 uv, int i, int j)
{
	return Normals.Sample(NormalsSampler,
					  uv + float2(i * TexelSize.x, j * TexelSize.y))
	    .xyz;
}

float4 main(PS_INPUT input) : SV_Target0
{
	float3 cornerWeight = float3(0.5, 0.25, 1);
	float3 edgeWeight = float3(1, 0.5, 2);
	float3 topLeft = Tap(input.TexCoord, -1, -1);
	float3 top = Tap(input.TexCoord, 0, -1);
	float3 topRight = Tap(input.TexCoord, 1, -1);
	float3 left = Tap(input.TexCoord, -1, 0);
	float3 right = Tap(input.TexCoord, 1, 0);
	float3 bottomLeft = Tap(input.TexCoord, -1, 1);
	float3 bottom = Tap(input.TexCoord, 0, 1);
	float3 bottomRight = Tap(input.TexCoord, 1, 1);
	float3 colorLR = topLeft * cornerWeight + left * edgeWeight +
	                 bottomLeft * cornerWeight - topRight * cornerWeight -
	                 right * edgeWeight - bottomRight * cornerWeight;
	float3 colorBT = topLeft * cornerWeight + top * edgeWeight +
	                 topRight * cornerWeight - bottomLeft * cornerWeight -
	                 bottom * edgeWeight - bottomRight * cornerWeight;

	float3 center = Normals.Sample(NormalsSampler, input.TexCoord).xyz;
	float luminance = dot(center, float3(0.3, 0.59, 0.11));
	float value =
		dot(4 * (pow(colorLR, 2) + pow(colorBT, 2)), 1.75) + luminance;
	float alpha = value * (1 - Image.Sample(ImageSampler, input.TexCoord).x);
	float2 edgeFade = 1 - pow(2 * abs(input.TexCoord - 0.5), 5);
	float4 color = float4(value.xxx, alpha * (edgeFade.x * edgeFade.y));
#	ifdef SINGLE_CHANNEL
	color = float4(color.rgb * color.a, 1);
#	endif
	return color;
}
#endif

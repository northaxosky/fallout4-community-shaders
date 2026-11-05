// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#define FO4CS_VS_GEOMETRY_OFFSET
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer ISSAOBlurConstants : register(b2)
{
	float4 g_ScreenInfos;
};

Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

#	ifdef AXIS_H
static const float2 Axis = float2(1, 0);
#	else
static const float2 Axis = float2(0, 1);
#	endif

static const int R = 3;
static const int SCALE = 2;
static const float EDGE_SHARPNESS = 1.0;
static const float gaussian[R + 1] = { 0.153170, 0.144893, 0.122649, 0.092902 };

float unpackKey(float2 p)
{
	return dot(p, float2(256.0 / 257.0, 1.0 / 257.0));
}

float4 main(PS_INPUT input) : SV_Target0
{
	float4 output = 0;
	float4 temp = source.Sample(sourceSampler, input.TexCoord);
	output.yz = temp.yz;
	float key = unpackKey(temp.yz);
	float sum = temp.x;
	if (key == 1.0) {
		return output;
	}
	float totalWeight = gaussian[0];
	sum *= totalWeight;
	[unroll] for (int r = -R; r <= R; ++r)
	{
		if (r != 0) {
			temp = source.Sample(sourceSampler, input.TexCoord + Axis * g_ScreenInfos.zw * (r * SCALE));
			float tapKey = unpackKey(temp.yz);
			float value = temp.x;
			float weight = 0.3 + gaussian[abs(r)];
			weight *= max(0.0, 1.0 - (EDGE_SHARPNESS * 2000.0) * abs(tapKey - key));
			sum += value * weight;
			totalWeight += weight;
		}
	}
	const float epsilon = 0.0001;
#	ifdef AXIS_H
	output.x = sum / (totalWeight + epsilon);
	return output;
#	else
	return sum / (totalWeight + epsilon);
#	endif
}
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISMotionBlurConstants : register(b2)
{
	float4 g_MotionBlurParam;
	float4 g_DynamicResolutionParam;
};

Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);
Texture2D<float4> MotionVectors : register(t1);
SamplerState MotionVectorsSampler : register(s1);

typedef VS_OUTPUT PS_INPUT;

static const int TapCount = 4;

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
	float2 velocity = MotionVectors.Sample(MotionVectorsSampler, uv).xy;
	float speed = length(velocity);
#	ifdef OG
	float2 screenCenter = g_DynamicResolutionParam.xy * 0.5;
#	else
	float2 screenCenter = g_DynamicResolutionParam.zw;
#	endif
	float falloff = saturate(length(uv - screenCenter) * 0.1 - 0.018);
	float blurLength = speed * falloff;
	if (blurLength < 0.001) {
		return source.Sample(sourceSampler, uv);
	}
	float2 blurStep = normalize(velocity) * min(blurLength, g_MotionBlurParam.y) *
	                  g_MotionBlurParam.x;
	float3 color = 0;
	float weightSum = 0;
	[unroll] for (int tap = 0; tap < TapCount; ++tap)
	{
#	ifdef OG
		// OG: integer division makes every tap sample the same UV.
		float2 tapUV = min(uv + blurStep * (tap / TapCount), g_DynamicResolutionParam.xy);
#	else
		float2 tapUV = min(uv + blurStep * (tap / (float)TapCount), g_DynamicResolutionParam.xy);
#	endif
		float tapSpeed = length(MotionVectors.Sample(MotionVectorsSampler, tapUV).xy);
		float weight = saturate(1 - abs(speed - tapSpeed) * g_MotionBlurParam.z);
		weightSum += weight;
		color += source.Sample(sourceSampler, tapUV).xyz * weight;
	}
	return float4(color / (weightSum + 0.001), 1);
}
#endif

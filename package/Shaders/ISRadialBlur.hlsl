// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer ISRadialBlurConstants : register(b2)
{
	float4 Params;
	float4 Center;
#	ifndef OG
	float4 DynamicResolutionParams;
#	endif
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
	float2 toCenter = Center.xy - uv;
	float distanceSquared = dot(toCenter, toCenter);
	float distance = sqrt(distanceSquared);

	float rampUp = max(distance - Params.z, 0);
	float strengthUp = rampUp > 0 ? (-1 / (Params.y * rampUp + 1) + 1) * Params.x : 0;
	float rampDown = max(distance - Center.z, 0);
	float strengthDown = rampDown > 0 ? (-1 / (Params.w * rampDown + 1) + 1) * Params.x : 0;

	float2 blurStep = toCenter * rsqrt(distanceSquared) *
	                  max(strengthUp - strengthDown, 0) * (1.0 / NUM_STEPS);
	float4 color = 0;
	[loop] for (float step = -NUM_STEPS; step <= NUM_STEPS; step += 1)
	{
#	ifdef OG
		float2 sampleUV = blurStep * step + uv;
		sampleUV.x = Center.w < sampleUV.x ? uv.x : sampleUV.x;
#	else
		float2 sampleUV = min(blurStep * step + uv, DynamicResolutionParams.xy);
#	endif
		color += Image.Sample(ImageSampler, sampleUV);
	}
	return color * (1.0 / (NUM_STEPS * 2 + 1));
}
#endif

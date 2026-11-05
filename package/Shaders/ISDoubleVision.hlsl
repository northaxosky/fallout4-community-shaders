// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISDoubleVisionConstants : register(b2)
{
	float4 blurParams;
	float4 doubleVisParams;
};

Texture2D<float4> Src0 : register(t0);
SamplerState Src0Sampler : register(s0);
Texture2D<float4> Src1 : register(t1);
SamplerState Src1Sampler : register(s1);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
#	ifdef OG
	float2 forwardUV = float2(min(uv.x + doubleVisParams.x, doubleVisParams.z),
		min(uv.y + doubleVisParams.y, 1));
	float2 backUV = float2(max(uv.x - doubleVisParams.x, 0),
		max(uv.y - doubleVisParams.y, 1 - doubleVisParams.w));
#	else
	float2 forwardUV = min(uv + doubleVisParams.xy, blurParams.xy);
	float2 backUV = max(uv - doubleVisParams.xy, 0);
#	endif
	float3 doubled = Src0.Sample(Src0Sampler, forwardUV).xyz +
	                 Src0.Sample(Src0Sampler, backUV).xyz;
#	ifdef OG
	float2 centered = uv - 0.5;
	float x = doubleVisParams.z / doubleVisParams.w * centered.x * 2;
	float y = centered.y * 2;
	float radius = sqrt(x * x + y * y);
#	else
	float radius = length((uv * doubleVisParams.zw - 0.5) * 2);
#	endif
	float blend = min(radius * blurParams.z, 1);
	float3 color = (doubled * (1 - blend)) * 0.5 +
	               blend * Src1.Sample(Src1Sampler, uv).xyz;
	return float4(color, 1);
}
#endif

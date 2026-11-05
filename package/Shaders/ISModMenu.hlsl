// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ModMenuConstants : register(b2)
{
	float4 Color;
	float4 HighlightColor;
	float4 Params0;
	float4 TexelSize;
};

Texture2D<float4> Albedo : register(t0);
SamplerState AlbedoSampler : register(s0);
Texture2D<float4> Mask : register(t1);
SamplerState MaskSampler : register(s1);
Texture2D<float4> Normals : register(t2);
SamplerState NormalsSampler : register(s2);

typedef VS_OUTPUT PS_INPUT;

#	ifdef OG
static const float MaskThreshold = 0.1;
#	else
static const float MaskThreshold = 0.05;
#	endif

float3 DecodeNormal(float2 encoded)
{
	float2 n = encoded * 4.0 - 2.0;
	float f = dot(n, n);
	float g = sqrt(1.0 - f * 0.25);
	return float3(n * g, -(1.0 - f * 0.5));
}

float4 main(PS_INPUT input) : SV_Target0
{
	float4 color = 0.0;
	float mask = Mask.Sample(MaskSampler, input.TexCoord).x;
	bool forced = Params0.y != 0.0;
	bool noPulse = Params0.z != 0.0;
	if (mask > MaskThreshold || forced) {
		float3 normal = DecodeNormal(Normals.Sample(NormalsSampler, input.TexCoord).xy);
		float albedo = Albedo.Sample(AlbedoSampler, input.TexCoord).y;
		float rim = 1.0 - abs(normal.z) * 0.9;
		float pulse = (sin(Params0.x * 5.0) + 1.0) * 0.5;
		float a = saturate(rim * 1.1 + albedo);
		float b = saturate(albedo * 0.5 + rim * 2.3);
		float value = lerp(a, b, noPulse ? 0.0 : pulse);
		color = float4(value * HighlightColor.rgb, saturate(rim + 0.6));
	} else {
		return 0.0;
	}
	return color;
}
#endif

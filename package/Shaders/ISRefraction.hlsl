// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer RefractionConstants : register(b2)
{
	float4 Tint;
};

Texture2D<float4> Src0 : register(t0);
SamplerState Src0Sampler : register(s0);
Texture2D<float4> Src1 : register(t1);
SamplerState Src1Sampler : register(s1);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	float4 refraction = Src1.Sample(Src1Sampler, input.TexCoord);
	float strength = refraction.z * 0.125;
	float2 normal = refraction.xy - 0.5;
	float2 uv = input.TexCoord;
	uv.x -= strength * normal.x * 2;
	uv.y += strength * normal.y * 2;
	float2 squeezed = uv > 0.85 ? (uv - 0.85) * 0.78 + 0.85 : uv;
	squeezed = uv < 0.15 ? 0.15 - (0.15 - uv) * 0.78 : squeezed;
	uv = lerp(uv, squeezed, refraction.z);
	float4 refracted = Src0.Sample(Src0Sampler, uv);
	float refracts = Src1.Sample(Src1Sampler, uv).w ? 1 : 0;
	float4 color = Src0.Sample(Src0Sampler, input.TexCoord) * (1 - refracts) +
	               refracted * refracts;
	float luminance = dot(float3(0.299, 0.587, 0.114), refracted.rgb);
	if (refraction.w > 0.8 && refraction.w < 1.0) {
		color.rgb = (1 - Tint.w) * color.rgb + luminance * Tint.w * Tint.rgb;
	}
	return color;
}
#endif

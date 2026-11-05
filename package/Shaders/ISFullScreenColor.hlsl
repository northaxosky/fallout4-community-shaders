// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISFullScreenColorConstants : register(b2)
{
	float4 FullScreenColor;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	float3 image = Image.Sample(ImageSampler, input.TexCoord).xyz;
	return float4(lerp(image, FullScreenColor.xyz, FullScreenColor.w), 1.0);
}
#endif

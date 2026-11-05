// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer AlphaBlendConstants : register(b2)
{
	float4 Color[16];
};

Texture2D<float4> TintMask[16] : register(t0);
SamplerState TintMaskSampler[16] : register(s0);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	float3 color = 0.5;
	[unroll] for (int i = 0; i < 16; ++i)
	{
		float alpha = TintMask[i].Sample(TintMaskSampler[i], input.TexCoord).x * Color[i].w;
		color = Color[i].rgb * alpha + color * (1.0 - alpha);
	}
	return float4(color, 1.0);
}
#endif

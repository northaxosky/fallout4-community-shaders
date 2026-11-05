// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer UpsampleDynamicResolutionConstants : register(b2)
{
	float4 g_UpsampleParameters;
};

Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = min(input.TexCoord * g_UpsampleParameters.xy,
		g_UpsampleParameters.zw);
	return source.Sample(sourceSampler, uv);
}
#endif

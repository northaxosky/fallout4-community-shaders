// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#define FO4CS_VS_GEOMETRY_OFFSET
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer ISSAOMinifyConstants : register(b2)
{
	float4 g_RenderTargetResolution;
};

Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

float4 main(PS_INPUT input) : SV_Target0
{
	int2 ssP = int2(input.TexCoord * g_RenderTargetResolution.xy);
	float2 offset = float2((ssP.y & 1) ^ 1, (ssP.x & 1) ^ 1);
	return source.Sample(sourceSampler, input.TexCoord + offset * g_RenderTargetResolution.zw);
}
#endif

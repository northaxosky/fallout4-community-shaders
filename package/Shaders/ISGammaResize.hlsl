// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer ISGammaResizeConstants : register(b2)
{
	float4 dynamicResolutionInfos;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

static const float GammaExponent = 1.0 / 2.2;

float4 main(PS_INPUT input) : SV_Target0
{
	float4 image = Image.Sample(
		ImageSampler, input.TexCoord * dynamicResolutionInfos.xy);
	return float4(pow(image.rgb, GammaExponent), image.a);
}
#endif

// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef HDR_OUTPUT
#	include "FO4/HDRDisplay.hlsli"
#endif

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
#	ifdef HDR_OUTPUT
	if (FO4HDR::IsActive())
		return float4(sign(image.rgb) * pow(abs(image.rgb), GammaExponent), image.a);
#	endif
	return float4(pow(image.rgb, GammaExponent), image.a);
}
#endif

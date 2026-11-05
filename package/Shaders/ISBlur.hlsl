// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
typedef VS_OUTPUT PS_INPUT;

cbuffer ISBlurConstants : register(b2)
{
	float4 BlurBrightPass;
	float4 BlurScale;
	float4 BlurOffsets[TEXTAP + 1];
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
#	ifdef BRIGHTPASS
Texture2D<float4> AvgLum : register(t1);
SamplerState AvgLumSampler : register(s1);
#	endif

float4 main(PS_INPUT input) : SV_Target0
{
	float4 color = 0;
#	ifdef BRIGHTPASS
	float avgLum = dot(AvgLum.Sample(AvgLumSampler, input.TexCoord).xyz,
		float3(0.2125, 0.7154, 0.0721));
#	endif
	for (int tap = 0; tap < TEXTAP; ++tap) {
		float4 imageColor = Image.Sample(ImageSampler,
			input.TexCoord + BlurOffsets[tap].xy);
#	ifdef BRIGHTPASS
		imageColor = max(imageColor - BlurBrightPass.x, 0) * BlurBrightPass.y;
#	endif
		color += imageColor * BlurOffsets[tap].z;
	}
#	ifdef BRIGHTPASS
	color.w = avgLum;
#	endif
	return color;
}
#endif

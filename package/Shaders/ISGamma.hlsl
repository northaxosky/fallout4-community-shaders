// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
#	ifdef LUT
// Row 0 is never read but keeps LUTWeights at its stock row.
cbuffer ISGammaConstants : register(b2)
{
	float4 dynamicResolutionInfos;
	float4 LUTWeights;
};
#	endif

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
#	ifdef LUT
Texture3D<float4> LUT0 : register(t3);
SamplerState LUT0Sampler : register(s3);
Texture3D<float4> LUT1 : register(t4);
SamplerState LUT1Sampler : register(s4);
Texture3D<float4> LUT2 : register(t5);
SamplerState LUT2Sampler : register(s5);
Texture3D<float4> LUT3 : register(t6);
SamplerState LUT3Sampler : register(s6);
#	endif

typedef VS_OUTPUT PS_INPUT;

#	ifdef LINEARIZE
static const float GammaExponent = 2.2;
#	else
static const float GammaExponent = 1.0 / 2.2;
#	endif

static const float LUTScale = 15.0 / 16.0;
static const float LUTBias = 0.5 / 16.0;

float4 main(PS_INPUT input) : SV_Target0
{
	float4 image = Image.Sample(ImageSampler, input.TexCoord);
	float3 encoded = pow(image.rgb, GammaExponent);
#	ifdef LUT
	float3 lutCoord = encoded * LUTScale + LUTBias;
	float3 blended = LUT0.Sample(LUT0Sampler, lutCoord).rgb * LUTWeights.x +
	                 LUT1.Sample(LUT1Sampler, lutCoord).rgb * LUTWeights.y +
	                 LUT2.Sample(LUT2Sampler, lutCoord).rgb * LUTWeights.z +
	                 LUT3.Sample(LUT3Sampler, lutCoord).rgb * LUTWeights.w;
	return float4(blended, image.a);
#	else
	return float4(encoded, image.a);
#	endif
}
#endif

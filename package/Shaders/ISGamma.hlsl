// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef HDR_OUTPUT
#	include "FO4/HDRDisplay.hlsli"
#endif

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

#	ifdef LUT
float3 SampleLUTs(float3 encoded)
{
	float3 lutCoord = encoded * LUTScale + LUTBias;
	return LUT0.Sample(LUT0Sampler, lutCoord).rgb * LUTWeights.x +
	       LUT1.Sample(LUT1Sampler, lutCoord).rgb * LUTWeights.y +
	       LUT2.Sample(LUT2Sampler, lutCoord).rgb * LUTWeights.z +
	       LUT3.Sample(LUT3Sampler, lutCoord).rgb * LUTWeights.w;
}
#	endif

float4 main(PS_INPUT input) : SV_Target0
{
	float4 image = Image.Sample(ImageSampler, input.TexCoord);
#	if defined(HDR_OUTPUT) && !defined(LINEARIZE)
	if (FO4HDR::IsActive()) {
		// Values above 1 pass through; the LUT only grades what it can represent.
		float3 hdrEncoded = sign(image.rgb) * pow(abs(image.rgb), GammaExponent);
#		ifdef LUT
		float3 positive = max(hdrEncoded, 0.0);
		float3 graded;
		if (FO4HDR::LutMethod() == 0) {
			float radius = max(1.0, max(positive.x, max(positive.y, positive.z)));
			graded = SampleLUTs(positive / radius) * radius;
		} else {
			// Vanilla grades the SDR range; its change scales the highlights.
			float3 sdrIn = saturate(positive);
			graded = positive * (SampleLUTs(sdrIn) / max(sdrIn, 1e-4));
		}
		return float4(graded, image.a);
#		else
		return float4(hdrEncoded, image.a);
#		endif
	}
#	endif
	float3 encoded = pow(image.rgb, GammaExponent);
#	ifdef LUT
	return float4(SampleLUTs(encoded), image.a);
#	else
	return float4(encoded, image.a);
#	endif
}
#endif

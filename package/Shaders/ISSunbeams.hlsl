// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#define FO4CS_VS_GEOMETRY_OFFSET
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer SunbeamsConstants : register(b2)
{
	float4 UnmappedRows[3];
	float4 SunParams;
	float4 GlareColor;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Normal : register(t1);
SamplerState NormalSampler : register(s1);

typedef VS_OUTPUT PS_INPUT;

static const int SunbeamSteps = 128;

float4 main(PS_INPUT input) : SV_Target0
{
	float2 sunPosition = float2(SunParams.x, 1 - SunParams.y);
	float2 step = (sunPosition - input.TexCoord) / SunbeamSteps;
	float3 color = Image.Sample(ImageSampler, input.TexCoord).rgb;
	float2 uv = input.TexCoord;
	float skySteps = 0;
	for (int i = 0; i < SunbeamSteps; ++i) {
		if (1 - Normal.SampleGrad(NormalSampler, uv, 0, 0).x == 0) {
			skySteps += 1;
		}
		uv += step;
	}
	color += skySteps * GlareColor.x * SunParams.z * 0.0015625;
	return float4(color, 1);
}
#endif

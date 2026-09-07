// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) Skyrim Community Shaders contributors

struct PS_INPUT
{
	float4 Position : SV_POSITION;
	float2 TexCoord : TEXCOORD;
};

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDimensions;
	float2 Padding;
};

Texture2D<float4> SourceColor : register(t0);
SamplerState LinearClampSampler : register(s0);

float4 main(PS_INPUT input) : SV_TARGET
{
	uint width;
	uint height;
	SourceColor.GetDimensions(width, height);
	const float2 allocationDimensions = float2(width, height);
	const float2 sourceScale =
		TrueSamplingDimensions / allocationDimensions;
	const float2 maxSourceUv =
		(TrueSamplingDimensions - 0.5f) / allocationDimensions;
	return SourceColor.SampleLevel(
		LinearClampSampler,
		min(input.TexCoord * sourceScale, maxSourceUv),
		0.0f);
}

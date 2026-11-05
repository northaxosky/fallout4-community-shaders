// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#define FO4CS_VS_GEOMETRY_OFFSET
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
cbuffer PipboyScreenConstants : register(b2)
{
	float4 TexelSize;
	float4 Params0;
	float4 Params1;
	float4 Color;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Scanlines : register(t1);
SamplerState ScanlinesSampler : register(s1);
Texture2D<float4> VScan : register(t2);
SamplerState VScanSampler : register(s2);

typedef VS_OUTPUT PS_INPUT;

static const float GlowTaps = 5.0;

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
	uv.y += Params0.z;
	if (uv.y > 1.0) {
		uv.y -= 1.0;
	}
	if (uv.y < 0.0) {
		uv.y += 1.0;
	}
	float4 image = Image.Sample(ImageSampler, uv);
	if (Params1.y != 0.0) {
		image *= Scanlines.Sample(ScanlinesSampler, float2(uv.x, uv.y * Params1.y));
	}
	float vscan = VScan.Sample(VScanSampler, float2(uv.x, (uv.y - Params0.y) * 3.0)).x;
	image = image * vscan + image;

	float2 texelStep = TexelSize.xy * 3.0;
	float glowScale = Params0.x + 1.0;
	float2 start = uv - texelStep * glowScale * 2.5;
	float4 sum = 0.0;
	float2 tap = start;
	for (int x = 0; x < GlowTaps; ++x) {
		for (int y = 0; y < GlowTaps; ++y) {
			sum += Image.Sample(ImageSampler, tap);
			tap.y += texelStep.y * glowScale;
		}
		tap.x += texelStep.x * glowScale;
		tap.y = start.y;
	}
	float4 color = image * 0.6 + glowScale * sum * 0.016;
	float luminance = dot(color.rgb, float3(0.3, 0.59, 0.11));
	color.rgb = luminance * Color.rgb;
	return color * Params0.w;
}
#endif

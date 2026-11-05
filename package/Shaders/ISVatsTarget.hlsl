// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer VatsTargetConstants : register(b2)
{
	float4 TexelSize;
	float4 Params;
	float4 Color;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Mask : register(t1);
SamplerState MaskSampler : register(s1);
#	ifdef VATS_DEBUG_COLOR
Texture2D<float4> Normals : register(t2);
SamplerState NormalsSampler : register(s2);
Texture2D<float4> Effects : register(t3);
SamplerState EffectsSampler : register(s3);
Texture2D<float4> VScan : register(t4);
SamplerState VScanSampler : register(s4);
#	else
Texture2D<float4> Effects : register(t2);
SamplerState EffectsSampler : register(s2);
Texture2D<float4> VScan : register(t3);
SamplerState VScanSampler : register(s3);
#	endif

typedef VS_OUTPUT PS_INPUT;

float3 DecodeNormal(float2 encoded)
{
	float2 n = encoded * 4.0 - 2.0;
	float f = dot(n, n);
	float g = sqrt(1.0 - f * 0.25);
	return float3(n * g, -(1.0 - f * 0.5));
}

float4 main(PS_INPUT input) : SV_Target0
{
	float4 image = Image.Sample(ImageSampler, input.TexCoord);
	float4 mask = Mask.Sample(MaskSampler, input.TexCoord);
#	ifdef VATS_DEBUG_COLOR
	float3 normal = DecodeNormal(Normals.Sample(NormalsSampler, input.TexCoord).xy);
	float3 maskColor = mask.rgb;
#	else
	float3 normal = normalize(mask.rgb * 2.0 - 1.0);
	float3 maskColor = mask.w;
#	endif
	float effects = Effects.Sample(EffectsSampler, input.TexCoord * Params.w).x;

	float2 texelStep = TexelSize.xy * 2.0;
	float scale = Params.x + 1.0;
	float2 start = input.TexCoord - texelStep * scale * 2.5;
	float coverage = 0.0;
	float2 tap = start;
	for (int x = 0; x < 5; ++x) {
		for (int y = 0; y < 5; ++y) {
			coverage += Mask.Sample(MaskSampler, tap).w;
			tap.y += scale * texelStep.y;
		}
		tap.x += scale * texelStep.x;
		tap.y = start.y;
	}
	float edge = 1.0 - abs(coverage * 0.04 - 0.5) * 2.0;
	float inside = coverage > 12.5 ? 1.0 : 0.0;
	float3 view = normalize(float3(0.5 - input.TexCoord.x, input.TexCoord.y - 0.5, -1.0));
	float rim = 1.0 - dot(normal, view);
	float alpha = mask.w * saturate(rim * inside + 0.2);
	float intensity = effects * inside + max(inside * rim, edge);
	float3 color = intensity * (maskColor * Color.rgb) * Params.z * (Params.x * 0.5 + 1.0);
	float vscan = VScan.Sample(VScanSampler, float2(input.TexCoord.x, (input.TexCoord.y - Params.y) * 3.0)).x;
	color *= vscan + 1.0;
	image.rgb += alpha * color;
	float3 edgeColor = edge * Color.rgb * Params.z;
	return float4(max(image.rgb, edgeColor), image.a);
}
#endif

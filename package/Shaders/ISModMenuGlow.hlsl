// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ModMenuGlowConstants : register(b2)
{
	float4 Color;
	float4 HighlightColor;
	float4 Params0;
	float4 TexelSize;
};

Texture2D<float4> Albedo : register(t0);
SamplerState AlbedoSampler : register(s0);
Texture2D<float4> Mask : register(t1);
SamplerState MaskSampler : register(s1);
Texture2D<float4> Normals : register(t2);
SamplerState NormalsSampler : register(s2);
Texture2D<float4> Image : register(t3);
SamplerState ImageSampler : register(s3);
Texture2D<float4> FX : register(t4);
SamplerState FXSampler : register(s4);

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
	float2 encodedNormal = Normals.Sample(NormalsSampler, input.TexCoord).xy;
	float3 normal = DecodeNormal(encodedNormal);
	bool forced = Params0.y != 0.0;
	bool noScan = Params0.z != 0.0;
	float mask = forced ? 1.0 : Mask.Sample(MaskSampler, input.TexCoord).x;
	float4 image = float4(Image.Sample(ImageSampler, input.TexCoord).rgb,
		dot(encodedNormal, 1.0) > 0.0 ? 1.0 : 0.0);
	float4 fx = FX.Sample(FXSampler, input.TexCoord);

	float3 sum = 0.0;
	float count = 0.0;
	for (int x = -4; x <= 4; ++x) {
		for (int y = -4; y <= 4; ++y) {
			sum += FX.Sample(FXSampler, input.TexCoord + float2(x, y) * TexelSize.xy * 3.0).rgb;
			count += 1.0;
		}
	}
	float3 glow = saturate(sum / count - mask);
	float4 base = mask < 0.01 ? image : 0.0;
	float scan = min(cos(input.TexCoord.y * 1600.0) * 0.45 + 1.05, 1.0);
	scan = noScan ? 1.0 : scan;
	float4 color = fx * scan + base;
	color.rgb += glow;
	float rim = 1.0 - abs(normal.z) * 0.9;
	if (Params0.w) {
		color.rgb = lerp(image.rgb, color.rgb, saturate(rim + 0.2));
	}
	return color;
}
#endif

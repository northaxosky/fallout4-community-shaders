// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
struct VS_INPUT
{
	float4 Position: POSITION0;
	float4 Color: COLOR0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
#	ifndef CLEAR
	float3 TexCoord: TEXCOORD0;
#	endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
#	ifndef CLEAR
	vsout.TexCoord.xy = input.Position.xy * float2(0.5, -0.5) + 0.5;
	vsout.TexCoord.z = input.Color.w;
#	endif
	return vsout;
}
#endif

#ifdef PSHADER
#	ifdef CLEAR
struct PS_INPUT
{
	float4 Position: SV_POSITION;
};
#	else
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float3 TexCoord: TEXCOORD0;
};
#	endif

#	if defined(BLURY) || defined(BLURX)
cbuffer ISHUDGlassConstants : register(b2)
{
	float4 TexelSize;
};
#	elif defined(MARKERS) || defined(COPY)
cbuffer ISHUDGlassConstants : register(b2)
{
	float4 Color;
};
#	endif

#	ifndef CLEAR
Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
#	endif
#	if !defined(DROPSHADOW) && !defined(BLURY) && !defined(BLURX) && \
		!defined(MARKERS) && !defined(CLEAR) && !defined(COPY)
Texture2D<float4> Blur : register(t1);
SamplerState BlurSampler : register(s1);
#	endif

float4 main(PS_INPUT input) : SV_Target0
{
#	ifdef CLEAR
	return 0.0;
#	elif defined(DROPSHADOW)
	float4 image = Image.Sample(ImageSampler, input.TexCoord.xy);
	float2 shadowUV = input.TexCoord.xy + float2(-0.0005, -0.001);
	float4 shadow = 0.0;
	for (int tap = -2; tap <= 2; ++tap) {
		shadow += Image.Sample(ImageSampler, shadowUV + tap * 0.0001);
	}
	float4 result;
	result.rgb = shadow.rgb * 0.02 + image.rgb;
	result.a = saturate(shadow.a * 2.5 + image.a) * input.TexCoord.z;
	return result;
#	elif defined(BLURY) || defined(BLURX)
	float4 sum = 0.0;
	for (int tap = -2; tap <= 2; ++tap) {
#		ifdef BLURX
		float2 offset = float2(tap * TexelSize.x, 0.0);
#		else
		float2 offset = float2(0.0, tap * TexelSize.y);
#		endif
		sum += Image.Sample(ImageSampler, input.TexCoord.xy + offset);
	}
	return sum * 0.2;
#	elif defined(MARKERS) || defined(COPY)
	return Image.Sample(ImageSampler, input.TexCoord.xy) * Color;
#	else
	float4 blur = Blur.Sample(BlurSampler, input.TexCoord.xy);
	blur.a = saturate(blur.a * 0.9);
	float4 glow = float4(blur.rgb * 2.0, blur.a * 0.5);
	float4 image = Image.Sample(ImageSampler, input.TexCoord.xy);
	return lerp(glow, image, image.a);
#	endif
}
#endif

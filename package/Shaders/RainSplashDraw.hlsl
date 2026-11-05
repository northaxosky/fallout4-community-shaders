// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float4 TexCoord: TEXCOORD0;
};

cbuffer PerFrame : register(b12)
{
	float4 WorldToView[3] : packoffset(c0);
	row_major float4x4 ViewToClip : packoffset(c4);
	float3 PosAdjust : packoffset(c35);
};

cbuffer PerGeometry : register(b2)
{
	float4 SideVecView : packoffset(c0);
	float4 SplashSize : packoffset(c1);
};

Texture2D<float4> Splash : register(t0);
SamplerState SplashSampler : register(s0);
Texture2D<float4> Illuminance : register(t1);
SamplerState IlluminanceSampler : register(s1);

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	float4 splash = Splash.SampleLevel(SplashSampler, input.TexCoord, 0);
	float3 world = input.Position.x * SideVecView.xyz * SplashSize.x + splash.xyz;
	world.z = input.Position.y * SplashSize.y + world.z;
	float4 relative = float4(world - PosAdjust, 1.0);
	float4 view = float4(dot(WorldToView[0], relative), dot(WorldToView[1], relative),
		dot(WorldToView[2], relative), 1.0);
	float4 clip = mul(ViewToClip, view);
	vsout.Position = splash.w <= 1.01 ? clip : float4(0, 0, 0, 1);
	float2 screen = vsout.Position.xy / vsout.Position.w * float2(0.5, -0.5) + 0.5;
	float3 illuminance = Illuminance.SampleLevel(IlluminanceSampler, screen, 0).xyz;
	float frame = splash.w * 16.0;
	vsout.TexCoord.x = (input.Position.x + 0.5) * 0.0625 + floor(frame) * 0.0625;
	vsout.TexCoord.y = 1.0 - input.Position.y;
	vsout.TexCoord.z = frac(frame);
	vsout.TexCoord.w = (illuminance.x + illuminance.y + illuminance.z) * (2.0 / 3.0);
	return vsout;
}
#endif

#ifdef PSHADER
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float4 TexCoord: TEXCOORD0;
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target0
{
	float4 frame0 = Image.Sample(ImageSampler, input.TexCoord.xy);
	float4 frame1 = Image.Sample(ImageSampler, input.TexCoord.xy);
	float4 color = frame0 * (1.0 - input.TexCoord.z) + frame1 * input.TexCoord.z;
	return float4(color.xyz * input.TexCoord.w, color.w);
}
#endif

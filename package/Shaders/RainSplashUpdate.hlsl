// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
struct VS_INPUT
{
	float4 Position: POSITION0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float3 TexCoord: TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
	vsout.TexCoord = input.Position.xyz;
	return vsout;
}
#endif

#ifdef PSHADER
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float3 TexCoord: TEXCOORD0;
};

cbuffer PerGeometry : register(b2)
{
	float4 GameSpeed;
};

Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord.xy * float2(0.5, -0.5) + 0.5;
	float4 splash = Source.Sample(SourceSampler, uv);
	splash.w = GameSpeed.x * 0.0625 + splash.w;
	return splash;
}
#endif

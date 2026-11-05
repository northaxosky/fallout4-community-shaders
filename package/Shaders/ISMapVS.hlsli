// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef OG
cbuffer PerGeometry : register(b2)
{
	float4 GeometryOffset;
};
#endif

struct VS_INPUT
{
	float4 Position: POSITION;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: POSITION;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = input.Position;
#ifdef OG
	vsout.Position.xy -= GeometryOffset.xy * float2(2.0, -2.0);
#endif
	vsout.TexCoord = input.TexCoord;
	return vsout;
}

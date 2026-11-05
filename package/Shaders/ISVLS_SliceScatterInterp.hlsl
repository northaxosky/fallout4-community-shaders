// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISVLSConstants : register(b2)
{
	float4 worldtolightr;
};

Texture2D<uint2> slice_interp : register(t3);
Texture2D<float> scatter : register(t4);

typedef VS_OUTPUT PS_INPUT;

float main(PS_INPUT input) : SV_Target0
{
	uint2 texel = input.TexCoord * worldtolightr.yx;
	uint2 sources = slice_interp.Load(uint3(texel, 0));
	float position = float(texel.x - sources.x) /
	                 float(max(sources.y - sources.x, 1));
	float source0 = scatter.Load(uint3(sources.x, texel.y, 0));
	float source1 = scatter.Load(uint3(sources.y, texel.y, 0));
	return lerp(source0, source1, position);
}
#endif

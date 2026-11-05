// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISVLSConstants : register(b2)
{
	float4 lightscreenpos_numslice_numsampperslice;
};

Texture2D<uint2> slice_interp : register(t3);

typedef VS_OUTPUT PS_INPUT;

void main(PS_INPUT input)
{
	uint2 sources = slice_interp.Load(uint3(
		input.TexCoord * lightscreenpos_numslice_numsampperslice.wz, 0));
	if (sources.x != sources.y)
		discard;
}
#endif

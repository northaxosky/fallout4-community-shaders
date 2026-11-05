// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "Common/DummyVSTexCoord.hlsl"

#ifdef PSHADER
cbuffer ISSAOCameraZConstants : register(b2)
{
	float4 g_ClipInfos;
	float4 g_ScreenInfos;
};

Texture2D<float4> Depth : register(t0);
SamplerState DepthSampler : register(s0);

typedef VS_OUTPUT PS_INPUT;

float main(PS_INPUT input) : SV_Target0
{
	float depth = Depth.Sample(DepthSampler, input.TexCoord).x;
	return g_ClipInfos.x / (g_ClipInfos.y * depth + g_ClipInfos.z);
}
#endif

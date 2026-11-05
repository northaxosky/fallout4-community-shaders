// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
#	include "ISMapVS.hlsli"
#endif

#ifdef PSHADER
cbuffer ISMapConstants : register(b2)
{
	float4 CameraPos;
};

Texture2D<float4> Src0 : register(t0);
SamplerState Src0Sampler : register(s0);

struct PS_INPUT
{
	float4 Position: POSITION;
	float2 TexCoord: TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target0
{
	float2 uv = input.TexCoord;
	float4 tl = Src0.Sample(Src0Sampler, uv + float2(-CameraPos.x, -CameraPos.y));
	float4 t = Src0.Sample(Src0Sampler, uv + float2(0, -CameraPos.y));
	float4 tr = Src0.Sample(Src0Sampler, uv + float2(CameraPos.x, -CameraPos.y));
	float4 l = Src0.Sample(Src0Sampler, uv + float2(-CameraPos.x, 0));
	float4 r = Src0.Sample(Src0Sampler, uv + float2(CameraPos.x, 0));
	float4 bl = Src0.Sample(Src0Sampler, uv + float2(-CameraPos.x, CameraPos.y));
	float4 b = Src0.Sample(Src0Sampler, uv + float2(0, CameraPos.y));
	float4 br = Src0.Sample(Src0Sampler, uv + float2(CameraPos.x, CameraPos.y));
	float4 gx = tl + 2 * l + bl - tr - 2 * r - br;
	float4 gy = tl + 2 * t + tr - bl - 2 * b - br;
	float4 g = gx * gx + gy * gy;
	float edge = dot(float3(0.2, 0.2, 0.15), g.xyz) + min(g.w * 10, 0.1);
	float3 color = min(edge, 0.275);
	return float4(color, dot(1, color) * 2);
}
#endif

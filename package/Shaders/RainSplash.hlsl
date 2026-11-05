// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: COLOR0;
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
	vsout.TexCoord = float3(input.TexCoord, 0.0);
	return vsout;
}
#endif

#ifdef PSHADER
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float3 TexCoord: TEXCOORD0;
};

cbuffer PerFrame : register(b12)
{
	row_major float4x4 ViewToWorld : packoffset(c12);
	row_major float4x4 ClipToView : packoffset(c20);
	row_major float4x4 FirstPersonClipToView : packoffset(c24);
	float3 PosAdjust : packoffset(c35);
};

cbuffer PerGeometry : register(b2)
{
	float4 RainDir;
	float4 DepthMod;
	float4 OccluTransform1;
	float4 OccluTransform2;
	float4 OccluTransform3;
	float4 OccluTransform4;
};

Texture2D<float4> Normal : register(t0);
SamplerState NormalSampler : register(s0);
Texture2D<float4> Depth : register(t1);
SamplerState DepthSampler : register(s1);
Texture2D<float4> OcclusionMap : register(t2);
SamplerState OcclusionMapSampler : register(s2);

float4 main(PS_INPUT input) : SV_Target0
{
	float2 encodedNormal = Normal.Sample(NormalSampler, input.TexCoord.xy).xy * 4.0 - 2.0;
	float encodedLengthSquared = dot(encodedNormal, encodedNormal);
	float3 normal = float3(
		encodedNormal * sqrt(1.0 - encodedLengthSquared * 0.25),
		-(1.0 - encodedLengthSquared * 0.5));
	float depth = Depth.Sample(DepthSampler, input.TexCoord.xy).x;

	bool facesRain = dot(-RainDir.xyz, normal) > 0.5;
	float viewDepth = DepthMod.y / (DepthMod.z - (depth * 2.0 - 1.0) * DepthMod.w);
	float splash = (facesRain && DepthMod.x < viewDepth) ? 0.0 : 2.0;

	float clipDepth;
	float4x4 clipToView;
	if (depth <= 0.01) {
		clipDepth = depth * 100.0;
		clipToView = FirstPersonClipToView;
	} else {
		clipDepth = depth * 1.01 - 0.01;
		clipToView = ClipToView;
	}
	float2 screen = float2(input.TexCoord.x, 1.0 - input.TexCoord.y);
	float4 clip = float4(screen * 2.0 - 1.0, clipDepth, 1.0);
	float4 viewH = mul(clipToView, clip);
	float4 view = float4(viewH.xyz / viewH.w, 1.0);
	float4 world = mul(ViewToWorld, view);

	float3 occlusion = float3(dot(OccluTransform1, world), dot(OccluTransform2, world),
		dot(OccluTransform3, world));
	float2 occlusionUV;
	occlusionUV.x = occlusion.x * 0.5 + 0.5;
	occlusionUV.y = occlusion.y * -0.5 + 0.5;
	float occluder = OcclusionMap.Sample(OcclusionMapSampler, occlusionUV).x;
	float4 result;
	result.w = occluder < occlusion.z ? 2.0 : splash;
	result.xyz = world.xyz + PosAdjust;
	return result;
}
#endif

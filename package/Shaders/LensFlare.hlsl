// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
#	ifdef VISIBILITY
struct VS_INPUT
{
	float4 Position: POSITION0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xy, 0.0, 1.0);
	return vsout;
}
#	else
struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float4 TexCoord: TEXCOORD0;
	float3 LightPos: TEXCOORD1;
};

cbuffer PerGeometry : register(b2)
{
	float4 ResolutionParams;
	float4 UV;
	float4 ScreenSpaceLightPos_PositionScale;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	float2 center = (ScreenSpaceLightPos_PositionScale.xy * 2.0 - 1.0) *
	                ScreenSpaceLightPos_PositionScale.w;
	float2 position = center + input.Position.x * UV.xy + input.Position.y * UV.zw;
	vsout.Position = float4(position, 0.0, 1.0);
	vsout.TexCoord = float4(input.TexCoord, length(position) * 0.1, 0.0);
	vsout.LightPos = ScreenSpaceLightPos_PositionScale.xyz;
	return vsout;
}
#	endif
#endif

#ifdef PSHADER
#	ifdef VISIBILITY
cbuffer PerGeometry : register(b2)
{
	float4 VisLightData[128];
};

Texture2D<float4> Depth : register(t0);
SamplerState DepthSampler : register(s0);

#		define PoissonTap(x, y) float2((x) * 0.02, (y) * 0.02)
static const float2 VisOffsets[16] = {
	PoissonTap(-0.94201624, -0.39906216),
	PoissonTap(0.94558609, -0.76890725),
	PoissonTap(-0.094184101, -0.92938870),
	PoissonTap(0.34495938, 0.29387760),
	PoissonTap(-0.91588581, 0.45771432),
	PoissonTap(-0.81544232, -0.87912464),
	PoissonTap(-0.38277543, 0.27676845),
	PoissonTap(0.97484398, 0.75648379),
	PoissonTap(0.44323325, -0.97511554),
	PoissonTap(0.53742981, -0.47373420),
	PoissonTap(-0.26496911, -0.41893023),
	PoissonTap(0.79197514, 0.19090188),
	PoissonTap(-0.24188840, 0.99706507),
	PoissonTap(-0.81409955, 0.91437590),
	PoissonTap(0.19984126, 0.78641367),
	PoissonTap(0.14383161, -0.14100790)
};

float4 main(float4 position : SV_POSITION) : SV_Target0
{
	uint light = (uint)position.x;
	float visible = 0.0;
	[unroll] for (int tap = 0; tap < 16; ++tap)
	{
		float depth = Depth.Sample(DepthSampler, VisLightData[light].xy + VisOffsets[tap]).x;
		visible += depth >= VisLightData[light].z ? 1.0 : 0.0;
	}
	return visible / 16.0;
}

#	else

struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float4 TexCoord: TEXCOORD0;
	float3 LightPos: TEXCOORD1;
};

cbuffer PerGeometry : register(b2)
{
	float4 Color;
	float4 LightIndex;
	float4 VisScale;
};

Texture2D<float4> LensFlare : register(t0);
SamplerState LensFlareSampler : register(s0);
Texture2D<float4> Vis : register(t1);

float4 main(PS_INPUT input) : SV_Target0
{
	float visibility = dot(VisScale, Vis.Load(int3((int)LightIndex.x, 0, 0)));
	float3 color = visibility * Color.xyz;
	float2 uv = input.TexCoord.xy;
	if (Color.w > 0.0) {
		uv = (uv - 0.5) / (visibility + 0.001) + 0.5;
	}
	float3 flare = LensFlare.Sample(LensFlareSampler, uv).xyz;
	return float4(color * flare, 0.0);
}

#	endif
#endif

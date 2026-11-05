// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
#	if defined(RIPPLE_MAKER_WADING) || defined(HEIGHTMAP_WADING) || defined(HEIGHTMAP_RAIN) || defined(HEIGHTMAP_SMOOTH)
struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

#		ifdef RIPPLE_MAKER_WADING
cbuffer ISWaterDisplacementConstants : register(b2)
{
	float4 ResolutionParams;
	float4 TranslationMatrix0;
	float4 TranslationMatrix1;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	float3 position = float3(input.Position.xy, 1.0);
	vsout.Position.x = dot(position, TranslationMatrix0.xyz);
	vsout.Position.y = dot(position, TranslationMatrix1.xyz);
	vsout.Position.z = input.Position.z;
	vsout.Position.w = 1.0;
	vsout.TexCoord = input.TexCoord;
	return vsout;
}

#		elif defined(HEIGHTMAP_WADING) || defined(HEIGHTMAP_RAIN) || defined(HEIGHTMAP_SMOOTH)

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
	float2 UpperLeft: TEXCOORD1;
	float2 LowerLeft: TEXCOORD2;
	float2 UpperRight: TEXCOORD3;
	float2 LowerRight: TEXCOORD4;
};

static const float TexelSize = 1.0 / 1024.0;

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
	vsout.TexCoord = input.TexCoord;
	vsout.UpperLeft = input.TexCoord + float2(-TexelSize, -TexelSize);
	vsout.LowerLeft = input.TexCoord + float2(-TexelSize, TexelSize);
	vsout.UpperRight = input.TexCoord + float2(TexelSize, -TexelSize);
	vsout.LowerRight = input.TexCoord + float2(TexelSize, TexelSize);
	return vsout;
}

#		endif
#	else
#		include "FO4/ImageSpaceVS.hlsli"
#	endif
#endif

#ifdef PSHADER
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
#	if defined(HEIGHTMAP_WADING) || defined(HEIGHTMAP_RAIN) || defined(HEIGHTMAP_SMOOTH)
	float2 UpperLeft: TEXCOORD1;
	float2 LowerLeft: TEXCOORD2;
	float2 UpperRight: TEXCOORD3;
	float2 LowerRight: TEXCOORD4;
#	endif
};

cbuffer ISWaterDisplacementConstants : register(b2)
{
	float Time;
	float BlendAmount;
	float2 TextureOffset;
	float fDamp;
	float3 RainVars;
	float3 WadingVars;
};

#	ifdef CLEAR_SIMULATION
float4 main(PS_INPUT input) : SV_Target0
{
	return 0.5;
}

#	elif defined(TEXCOORD_OFFSET)
Texture2D<float4> DisplaySampler : register(t0);
SamplerState DisplaySamplerSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target0
{
	float edge = smoothstep(0.4, 0.5, length(input.TexCoord - 0.5));
	float4 state = DisplaySampler.Sample(DisplaySamplerSampler, input.TexCoord + TextureOffset);
	return lerp(state, 0.5, edge);
}

#	elif defined(RIPPLE_MAKER_WADING) || defined(RIPPLE_MAKER_RAIN)

float4 main(PS_INPUT input) : SV_Target0
{
	return float4(1.0, 0.5, 0.5, 0.5);
}

#	elif defined(HEIGHTMAP_WADING) || defined(HEIGHTMAP_RAIN)

Texture2D<float4> HeightMap : register(t0);
SamplerState HeightMapSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target0
{
#		ifdef HEIGHTMAP_WADING
	float3 vars = WadingVars;
#		else
	float3 vars = RainVars;
#		endif
	float4 state = HeightMap.Sample(HeightMapSampler, input.TexCoord);
	float upperLeft = HeightMap.Sample(HeightMapSampler, input.UpperLeft).x;
	float lowerLeft = HeightMap.Sample(HeightMapSampler, input.LowerLeft).x;
	float upperRight = HeightMap.Sample(HeightMapSampler, input.UpperRight).x;
	float lowerRight = HeightMap.Sample(HeightMapSampler, input.LowerRight).x;
	float laplacian = (upperLeft + lowerLeft + upperRight + lowerRight) - 4.0 * state.x;
	state.xy -= 0.5;
	state.y += vars.x * laplacian;
	state.x += vars.y * state.y;
	state.xy *= vars.z;
	state.z = (upperLeft + lowerLeft - upperRight - lowerRight) * 0.5;
	state.w = (upperLeft - lowerLeft + upperRight - lowerRight) * 0.5;
	return state + 0.5;
}

#	elif defined(BLEND_HEIGHTMAPS)
Texture2D<float4> HeightMap01 : register(t0);
SamplerState HeightMap01Sampler : register(s0);
Texture2D<float4> HeightMap02 : register(t1);
SamplerState HeightMap02Sampler : register(s1);

float4 main(PS_INPUT input) : SV_Target0
{
	float scale = 0.8 / fDamp;
	float height1 = scale * abs(HeightMap01.Sample(HeightMap01Sampler, input.TexCoord).x);
	float height2 = abs(HeightMap02.Sample(HeightMap02Sampler, input.TexCoord).x);
	return float4(lerp(height1, height2, BlendAmount).xxx, 1.0);
}

#	elif defined(HEIGHTMAP_SMOOTH)
Texture2D<float4> HeightMap : register(t0);
SamplerState HeightMapSampler : register(s0);

float4 main(PS_INPUT input) : SV_Target0
{
	return HeightMap.Sample(HeightMapSampler, input.TexCoord);
}

#	elif defined(NORMALS)
Texture2D<float4> DisplaceMap : register(t0);
SamplerState DisplaceMapSampler : register(s0);

static const float TexelSize = 1.0 / 512.0;

float Tap(float2 uv, float2 offset)
{
	return fDamp * pow(abs(DisplaceMap.Sample(DisplaceMapSampler, uv + offset * TexelSize).x), 7.0);
}

float4 main(PS_INPUT input) : SV_Target0
{
	float topLeft = Tap(input.TexCoord, float2(-1, -1));
	float left = Tap(input.TexCoord, float2(-1, 0));
	float bottomLeft = Tap(input.TexCoord, float2(-1, 1));
	float top = Tap(input.TexCoord, float2(0, -1));
	float bottom = Tap(input.TexCoord, float2(0, 1));
	float topRight = Tap(input.TexCoord, float2(1, -1));
	float right = Tap(input.TexCoord, float2(1, 0));
	float bottomRight = Tap(input.TexCoord, float2(1, 1));
	float dX = -topLeft - 2 * left - bottomLeft + topRight + 2 * right + bottomRight;
	float dY = -topLeft - 2 * top - topRight + bottomLeft + 2 * bottom + bottomRight;
	float4 normal = float4(normalize(float3(-dX, dY, 1.0)), 1.0);
	return normal * 0.5 + 0.5;
}

#	endif
#endif

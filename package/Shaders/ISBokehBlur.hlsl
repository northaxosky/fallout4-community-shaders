// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef VSHADER
#	if defined(COC_GENERATION) || defined(COMPOSITE)
cbuffer BokehVertexConstants : register(b2)
{
	float4 ResolutionParams;
	float4 ScreenResolution;
};

struct VS_INPUT
{
	float4 Position: POSITION0;
	float2 TexCoord: TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
	float2 PixelCoord: TEXCOORD1;
#		ifdef COMPOSITE
	float2 BlurredPixelCoord: TEXCOORD2;
#		endif
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;
	vsout.Position = float4(input.Position.xyz, 1.0);
	vsout.TexCoord = input.TexCoord;
	vsout.PixelCoord = input.TexCoord * ScreenResolution.xy;
#		ifdef COMPOSITE
	vsout.BlurredPixelCoord = input.TexCoord * ScreenResolution.xy - 0.5;
#		endif
	return vsout;
}
#	else
#		include "FO4/ImageSpaceVS.hlsli"
#	endif
#endif

#ifdef PSHADER
#	if defined(COC_GENERATION) || defined(COMPOSITE)
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
	float2 PixelCoord: TEXCOORD1;
#		ifdef COMPOSITE
	float2 BlurredPixelCoord: TEXCOORD2;
#		endif
};
#	else
struct PS_INPUT
{
	float4 Position: SV_POSITION;
	float2 TexCoord: TEXCOORD0;
};
#	endif

cbuffer BokehConstants : register(b2)
{
	float4 params;
	float4 params2;
	float4 params3;
	float4 params4;
#	ifdef FOGGED
	float4 params5;
	float4 params6;
#	endif
};

static const int BlurTaps = 6;

#	if defined(COC_GENERATION) || defined(COMPOSITE)
Texture2D<float4> colorMap : register(t0);
#		ifdef COMPOSITE
Texture2D<float4> colorMap2 : register(t1);
#		endif
Texture2D<float4> depthMap : register(t2);

float GetCoC(float2 uv, int3 pixel, out float linearDepth)
{
	float vignette = saturate(length(0.5 - uv) * 1.41421356 - params3.x) *
	                 params3.y;
	float depth = depthMap.Load(pixel).x * 2.0 - 1.0;
	linearDepth = params4.x / (params4.y - depth * params4.z);
	float farCoC = saturate(max(linearDepth - params.y, 0.0) / params.w) *
	               params3.z;
	if (params3.w == 1.0 && linearDepth >= params4.w) {
		farCoC = 0.0;
	}
	float nearCoC = 2.0 * params3.z *
	                saturate(max(params.x - linearDepth, 0.0) / params.z);
	float coc = farCoC - nearCoC;
	if (vignette > 0.0) {
		coc = -max(max(farCoC, vignette), nearCoC);
	}
	return coc;
}

float4 main(PS_INPUT input) : SV_Target0
{
	int3 pixel = int3(input.PixelCoord, 0);
	float linearDepth;
#		ifdef COC_GENERATION
	float4 color = colorMap.Load(pixel);
	color.a = abs(GetCoC(input.TexCoord, pixel, linearDepth)) * BlurTaps;
	return color;
#		else
	float coc = GetCoC(input.TexCoord, pixel, linearDepth);
	float4 color = colorMap.Load(pixel);
	float4 blurred = colorMap2.Load(int3(input.BlurredPixelCoord, 0));
	float4 result = (abs(coc) == 0.0 || blurred.w == 0.0) ? color : blurred;
#			ifdef FOGGED
	float fogScale = 1.0 / (params6.x - params6.y);
	float fog = saturate(linearDepth * fogScale - params6.y * fogScale) *
	            params5.w;
	result.rgb = lerp(result.rgb, params5.rgb, fog);
#			endif
	return result;
#		endif
}

#	elif defined(HORIZONTAL_BLUR) || defined(VERTICAL_BLUR)
Texture2D<float4> colorMap : register(t0);
SamplerState colorMapSampler : register(s0);

float4 BokehBlur(float2 uv, float2 offset, float4 center)
{
	float4 sum = center;
	float weightSum = 1.0;
	float2 coord = uv;
	for (int i = 0; i < BlurTaps; ++i) {
		coord += offset;
		float weight = saturate(abs(center.w) - i);
		float cocWeight = center.w * weight;
		float4 tap = colorMap.Sample(colorMapSampler, coord);
		weight = (cocWeight * tap.w > 0.0) ? weight : 0.0;
		weightSum += weight;
		sum += tap * weight;
	}
	coord = uv;
	for (int j = 0; j < BlurTaps; ++j) {
		coord -= offset;
		float weight = saturate(abs(center.w) - j);
		float cocWeight = center.w * weight;
		float4 tap = colorMap.Sample(colorMapSampler, coord);
		weight = (cocWeight * tap.w > 0.0) ? weight : 0.0;
		weightSum += weight;
		sum += tap * weight;
	}
	return sum / weightSum;
}

float4 main(PS_INPUT input) : SV_Target0
{
	float4 center = colorMap.Sample(colorMapSampler, input.TexCoord);
#		ifdef HORIZONTAL_BLUR
	return BokehBlur(input.TexCoord, float2(params2.x, 0.0), center);
#		else
	float4 blurA = BokehBlur(input.TexCoord, params2.yz, center);
	float4 blurB = BokehBlur(input.TexCoord, params2.yz * float2(1.0, -1.0),
		center);
	return min(blurA, blurB);
#		endif
}

#	endif
#endif

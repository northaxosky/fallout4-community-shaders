// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky
#include "FO4/ImageSpaceVS.hlsli"

#ifdef PSHADER
typedef VS_OUTPUT PS_INPUT;

cbuffer DepthOfFieldConstants : register(b2)
{
	float4 params;
	float4 params2;
	float4 params3;
	float4 dynamicResolutionParams;
#	ifdef FOGGED
	float4 params4;
	float4 params5;
#	endif
};

Texture2D<float4> Image : register(t0);
SamplerState ImageSampler : register(s0);
Texture2D<float4> Blurred : register(t1);
SamplerState BlurredSampler : register(s1);
Texture2D<float4> Depth : register(t2);
SamplerState DepthSampler : register(s2);

float4 main(PS_INPUT input) : SV_Target0
{
	float invDepth = 1.0 - Depth.Sample(DepthSampler, input.TexCoord).x;
	float linearDepth = params3.w / (invDepth * params3.z + params3.y);
	float blur = 0.0;
	if (invDepth > 0.00001 || !params2.w) {
#	ifdef DISTANT
		blur = saturate((linearDepth - params.z) / params.y) * params2.x;
#	else
		if (linearDepth < params.z && params2.y) {
			blur = (params.z - linearDepth) / params.x;
		}
		if (params.z < linearDepth && params2.z) {
			blur = (linearDepth - params.z) / params.y;
		}
		blur = saturate(blur) * params2.x;
#	endif
	}
	float4 image = Image.Sample(ImageSampler, input.TexCoord);
	float4 blurred = Blurred.Sample(BlurredSampler,
		input.TexCoord * dynamicResolutionParams.zw);
	float4 color = lerp(image, blurred, blur);
#	ifdef FOGGED
	float fogScale = 1.0 / (params5.x - params5.y);
	float fog = saturate(linearDepth * fogScale - params5.y * fogScale) *
	            params4.w;
	color.rgb = lerp(color.rgb, params4.rgb, fog);
#	endif
	return color;
}
#endif

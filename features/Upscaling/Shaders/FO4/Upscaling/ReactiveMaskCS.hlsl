// SPDX-License-Identifier: GPL-3.0-or-later

#include "FO4/Upscaling/IsFinite.hlsli"
#include "FO4/Upscaling/MaterialClass.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float4> PreAlphaColor : register(t0);
Texture2D<float4> PostAlphaColor : register(t1);
Texture2D<float4> MaterialClassMask : register(t2);

RWTexture2D<float> ReactiveMask : register(u0);

// FSR3 autogen-reactive constants as hard-coded in AMD's sample.
static const float ReactiveScale = 1.0;
static const float ReactiveCutoff = 0.2;
static const float ReactiveBinaryValue = 0.9;

float3 Tonemap(float3 a_color)
{
	const float3 color = max(a_color, 0.0);
	return color / (max(color.r, max(color.g, color.b)) + 1.0);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	const float3 pre = PreAlphaColor[dispatchID.xy].rgb;
	const float3 post = PostAlphaColor[dispatchID.xy].rgb;

	// NaN deltas and water reflections must not mark the pixel reactive.
	float reactive = 0.0;
	if (IsFinite(pre) && IsFinite(post) && !IsWaterClass(MaterialClassMask[dispatchID.xy].a)) {
		const float3 delta = abs(Tonemap(post) - Tonemap(pre));
		reactive = max(delta.x, max(delta.y, delta.z)) * ReactiveScale;
		reactive = reactive < ReactiveCutoff ? 0.0 : ReactiveBinaryValue;
	}
	ReactiveMask[dispatchID.xy] = reactive;
}

#ifndef FO4_WATER_EFFECTS_CONSUMER
#define FO4_WATER_EFFECTS_CONSUMER

#include "FO4/FO4ShaderData.hlsli"

#ifndef WATER_EFFECTS_FULLSCREEN_DEBUG
// FO4: directional lighting leaves s14 free for the upstream sampler.
SamplerState SampColorSampler : register(s14);
#	include "WaterEffects/WaterCaustics.hlsli"
#endif

namespace WaterEffects
{
#ifndef WATER_EFFECTS_FULLSCREEN_DEBUG
	float3 GetCausticsMultFromViewPosition(
		float3 viewPosition,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust)
	{
		uint2 dimensions;
		WaterCaustics.GetDimensions(dimensions.x, dimensions.y);
		// FO4: an absent SRV keeps disabled or unready lighting neutral.
		if (any(dimensions == 0))
			return 1.0.xxx;
		// FO4: b4 supplies camera-relative coordinates, not the native draw anchor.
		float3 position = FrameBuffer::ViewToWorld(viewPosition);
		return ComputeCaustics(SharedData::GetWaterData(position), position);
	}
#else
	// FO4: composite s14 belongs to scene colour; sample the isolated debug result.
	bool TryGetDebugColor(float2 pixelPosition, out float4 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::WaterEffects || FO4SharedData::DebugMode == 0)
			return false;
		uint2 dimensions;
		FO4SharedData::DebugTexture.GetDimensions(dimensions.x, dimensions.y);
		if (any(dimensions == 0))
			return false;
		color = FO4SharedData::DebugTexture.Load(int3(min(uint2(pixelPosition), dimensions - 1), 0));
		return true;
	}

	bool TryGetDebugColorFromViewPosition(
		float3 viewPosition,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust,
		out float4 color)
	{
		float2 uv = FrameBuffer::ViewToUV(viewPosition) * FrameBuffer::DynamicResolutionParams1.xy;
		return TryGetDebugColor(uv * SharedData::BufferDim.xy, color);
	}

	bool TryGetDebugColorFromScreenPosition(
		float2 pixelPosition,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust,
		float4 farReprojRow0, float4 farReprojRow1, float4 farReprojRow2, float4 farReprojRow3,
		float4 nearReprojRow0, float4 nearReprojRow1, float4 nearReprojRow2, float4 nearReprojRow3,
		out float4 color)
	{
		return TryGetDebugColor(pixelPosition, color);
	}
#endif
}
#endif

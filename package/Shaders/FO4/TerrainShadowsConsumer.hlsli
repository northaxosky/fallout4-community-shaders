// SPDX-License-Identifier: GPL-3.0-only
#ifndef FO4_TERRAIN_SHADOWS_CONSUMER
#define FO4_TERRAIN_SHADOWS_CONSUMER

#include "FO4/Depth.hlsli"
#include "FO4/FO4ShaderData.hlsli"
#include "FO4/ForwardPosition.hlsli"
#include "FO4/ShadowSamplingConsumer.hlsli"

namespace TerrainShadows
{
	float GetTerrainShadowMultFromWorldPosition(float3 worldPosition)
	{
		[branch] if (!SharedData::terraOccSettings.EnableTerrainShadow) return 1.0;
		// FO4: b4 supplies the absolute origin for upstream heightfield sampling.
		return ShadowSampling::GetWorldShadow(worldPosition, FrameBuffer::CameraPosAdjust.xyz);
	}

	float GetTerrainShadowMultFromViewPosition(float3 viewPosition)
	{
		return GetTerrainShadowMultFromWorldPosition(FrameBuffer::ViewToWorld(viewPosition));
	}

	float GetShadowFromScreenPosition(float3 screenPosition)
	{
		return GetTerrainShadowMultFromViewPosition(FO4Forward::ViewPosition(screenPosition));
	}

#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
	bool TryGetDebugColorFromViewPosition(float3 viewPosition, out float4 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::TerrainShadows)
			return false;
		if (!SharedData::terraOccSettings.EnableTerrainShadow || FO4SharedData::DebugMode == 0)
			return false;
		float3 worldPosition = FrameBuffer::ViewToWorld(viewPosition) + FrameBuffer::CameraPosAdjust.xyz;
		float value = GetTerrainShadow(worldPosition, LinearSampler);
		if (FO4SharedData::DebugMode == 2) {
			float2 HeightRange = FO4SharedData::DebugParams.xy;
			float2 DebugHeightRange = FO4SharedData::DebugParams.zw;
			float height = FO4SharedData::DebugTexture.SampleLevel(LinearSampler, GetTerrainShadowUV(worldPosition.xy), 0).x;
			height = lerp(HeightRange.x, HeightRange.y, height);
			value = saturate((height - DebugHeightRange.x) / max(DebugHeightRange.y - DebugHeightRange.x, 1e-3));
		}
		color = float4(value.xxx, 1.0);
		return true;
	}

	bool TryGetDebugColorFromScreenPosition(float2 pixelPosition, out float4 color)
	{
		float2 uv = pixelPosition * SharedData::BufferDim.zw;
		uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uv);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), SharedData::GetDepth(uv), 1.0));
		return TryGetDebugColorFromViewPosition(view.xyz / view.w, color);
	}
#else
	bool TryGetDebugColorFromViewPosition(float3 viewPosition, out float4 color)
	{
		color = 0.0;
		return false;
	}
#endif
}
#endif

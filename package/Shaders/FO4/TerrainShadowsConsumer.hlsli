// SPDX-License-Identifier: GPL-3.0-only
#ifndef FO4_TERRAIN_SHADOWS_CONSUMER
#define FO4_TERRAIN_SHADOWS_CONSUMER

#include "FO4/Depth.hlsli"
#include "FO4/FO4ShaderData.hlsli"
#include "TerrainShadows/TerrainShadows.hlsli"

namespace TerrainShadows
{
	SamplerState TerrainShadowsSampler : register(s13);

	float GetWorldShadow(float3 cameraRelativePosition)
	{
		if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
			return 1.0;
		// FO4: b4 supplies the absolute origin for upstream heightfield sampling.
		return GetTerrainShadow(cameraRelativePosition + FrameBuffer::CameraPosAdjust.xyz, TerrainShadowsSampler);
	}

	float3 GetViewPosition(float3 screenPosition)
	{
		float2 uv = screenPosition.xy * SharedData::BufferDim.zw;
		uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uv);
		float depth = FO4Depth::ProjectionDepth(screenPosition.z);
		if (FO4Depth::IsFirstPerson(screenPosition.z))
			depth = SharedData::GetDepth(uv);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0));
		return view.xyz / view.w;
	}

	float GetShadowFromScreenPosition(float3 screenPosition)
	{
		return GetWorldShadow(FrameBuffer::ViewToWorld(GetViewPosition(screenPosition)));
	}

	float GetTerrainShadowMultFromViewPosition(
		float3 viewPosition, SamplerState textureSampler,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust)
	{
		if (SharedData::InInterior || SharedData::HideSky || SharedData::InMapMenu)
			return 1.0;
		return GetTerrainShadow(FrameBuffer::ViewToWorld(viewPosition) +
									FrameBuffer::CameraPosAdjust.xyz,
			textureSampler);
	}

#ifdef TERRAIN_SHADOWS_FULLSCREEN_DEBUG
	bool TryGetDebugColorFromViewPosition(
		float3 viewPosition, SamplerState textureSampler,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust, out float4 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::TerrainShadows)
			return false;
		if (!SharedData::terraOccSettings.EnableTerrainShadow || FO4SharedData::DebugMode == 0)
			return false;
		float3 worldPosition = FrameBuffer::ViewToWorld(viewPosition) + FrameBuffer::CameraPosAdjust.xyz;
		float value = GetTerrainShadow(worldPosition, textureSampler);
		if (FO4SharedData::DebugMode == 2) {
			float2 HeightRange = FO4SharedData::DebugParams.xy;
			float2 DebugHeightRange = FO4SharedData::DebugParams.zw;
			float height = FO4SharedData::DebugTexture.SampleLevel(textureSampler, GetTerrainShadowUV(worldPosition.xy), 0).x;
			height = lerp(HeightRange.x, HeightRange.y, height);
			value = saturate((height - DebugHeightRange.x) / max(DebugHeightRange.y - DebugHeightRange.x, 1e-3));
		}
		color = float4(value.xxx, 1.0);
		return true;
	}

	bool TryGetDebugColorFromScreenPosition(
		float2 pixelPosition, SamplerState textureSampler,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust, float4 farReprojRow0, float4 farReprojRow1,
		float4 farReprojRow2, float4 farReprojRow3, float4 nearReprojRow0,
		float4 nearReprojRow1, float4 nearReprojRow2, float4 nearReprojRow3,
		out float4 color)
	{
		float2 uv = pixelPosition * SharedData::BufferDim.zw;
		uv = FrameBuffer::GetDynamicResolutionUnadjustedScreenPosition(uv);
		float4 view = mul(FrameBuffer::CameraProjInverse,
			float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), SharedData::GetDepth(uv), 1.0));
		return TryGetDebugColorFromViewPosition(view.xyz / view.w, textureSampler,
			viewToWorldRow0, viewToWorldRow1, viewToWorldRow2, cameraPosAdjust, color);
	}
#else
	bool TryGetDebugColorFromViewPosition(
		float3 viewPosition, SamplerState textureSampler,
		float4 viewToWorldRow0, float4 viewToWorldRow1, float4 viewToWorldRow2,
		float4 cameraPosAdjust, out float4 color)
	{
		color = 0.0;
		return false;
	}
#endif
}
#endif

// SPDX-License-Identifier: GPL-3.0-only
#include "Common/DeferredPosition.hlsli"
#include "FO4/Common/SharedData.hlsli"

namespace TerrainShadows
{
	Texture2D<float2> ShadowHeightTexture : register(t30);
	Texture2D<float> SceneDepthTexture : register(t31);
	SamplerState TerrainShadowsSampler : register(s13);

	static const uint MODE_DISABLED = 0;
	static const uint MODE_NORMAL = 1;
	static const uint MODE_SHADOW_TERM = 2;
	static const uint MODE_HEIGHTMAP = 3;

	float2 GetTerrainShadowUV(float2 xy)
	{
		return xy * SharedData::terrainShadowsSettings.Scale.xy +
		       SharedData::terrainShadowsSettings.Offset.xy;
	}

	// Upstream height bias.
	float GetTerrainZ(float normalizedZ)
	{
		float z = lerp(
			SharedData::terrainShadowsSettings.ZRange.x,
			SharedData::terrainShadowsSettings.ZRange.y,
			normalizedZ);
		return z - 256;
	}

	float2 GetTerrainZ(float2 normalizedZ)
	{
		return float2(GetTerrainZ(normalizedZ.x), GetTerrainZ(normalizedZ.y));
	}

	float GetTerrainShadowMult(float3 worldPosition, SamplerState textureSampler)
	{
		if (SharedData::terrainShadowsSettings.TerrainShadowMode == MODE_DISABLED)
			return 1.0;
		float2 shadowHeight = GetTerrainZ(
			ShadowHeightTexture.SampleLevel(
				textureSampler, GetTerrainShadowUV(worldPosition.xy), 0));
		// Grazing light can collapse the penumbra span.
		float penumbra = max(shadowHeight.x - shadowHeight.y, 1e-3);
		return saturate((worldPosition.z - shadowHeight.y) / penumbra);
	}

	bool TryGetDebugValue(
		float3 worldPosition,
		SamplerState textureSampler,
		out float value)
	{
		value = 0.0;
		uint mode = SharedData::terrainShadowsSettings.TerrainShadowMode;
		if (mode == MODE_SHADOW_TERM) {
			value = GetTerrainShadowMult(worldPosition, textureSampler);
			return true;
		}
		if (mode == MODE_HEIGHTMAP) {
			float height = ShadowHeightTexture.SampleLevel(
												  textureSampler, GetTerrainShadowUV(worldPosition.xy), 0)
			                   .x;
			height = lerp(
				SharedData::terrainShadowsSettings.HeightRange.x,
				SharedData::terrainShadowsSettings.HeightRange.y,
				height);
			float p01 = SharedData::terrainShadowsSettings.DebugHeightRange.x;
			float p99 = SharedData::terrainShadowsSettings.DebugHeightRange.y;
			value = saturate((height - p01) / max(p99 - p01, 1e-3));
			return true;
		}
		return false;
	}

	float3 ViewToWorldPosition(
		float3 viewPosition,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust)
	{
		float4 positionView = float4(viewPosition, 1.0);
		float3 worldPosition;
		worldPosition.x =
			dot(viewToWorldRow0, positionView) + cameraPosAdjust.x;
		worldPosition.y =
			dot(viewToWorldRow1, positionView) + cameraPosAdjust.y;
		worldPosition.z =
			dot(viewToWorldRow2, positionView) + cameraPosAdjust.z;
		return worldPosition;
	}

	bool TryGetViewPositionFromScreenPosition(
		float2 pixelPosition,
		float4 farReprojRow0,
		float4 farReprojRow1,
		float4 farReprojRow2,
		float4 farReprojRow3,
		float4 nearReprojRow0,
		float4 nearReprojRow1,
		float4 nearReprojRow2,
		float4 nearReprojRow3,
		out float3 viewPosition)
	{
		return DeferredPosition::TryGetViewPositionFromScreenPosition(
			SceneDepthTexture, pixelPosition,
			float4x4(farReprojRow0, farReprojRow1, farReprojRow2, farReprojRow3),
			float4x4(nearReprojRow0, nearReprojRow1, nearReprojRow2, nearReprojRow3),
			viewPosition);
	}

	float GetTerrainShadowMultFromViewPosition(
		float3 viewPosition,
		SamplerState textureSampler,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust)
	{
		return GetTerrainShadowMult(
			ViewToWorldPosition(
				viewPosition,
				viewToWorldRow0,
				viewToWorldRow1,
				viewToWorldRow2,
				cameraPosAdjust),
			textureSampler);
	}

	bool TryGetDebugColorFromViewPosition(
		float3 viewPosition,
		SamplerState textureSampler,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust,
		out float4 color)
	{
		color = 0.0;
		uint mode = SharedData::terrainShadowsSettings.TerrainShadowMode;
		if (mode != MODE_SHADOW_TERM && mode != MODE_HEIGHTMAP)
			return false;

		float value;
		const bool active = TryGetDebugValue(
			ViewToWorldPosition(
				viewPosition,
				viewToWorldRow0,
				viewToWorldRow1,
				viewToWorldRow2,
				cameraPosAdjust),
			textureSampler,
			value);
		color = float4(value.xxx, 1.0);
		return active;
	}

	bool TryGetDebugColorFromScreenPosition(
		float2 pixelPosition,
		SamplerState textureSampler,
		float4 viewToWorldRow0,
		float4 viewToWorldRow1,
		float4 viewToWorldRow2,
		float4 cameraPosAdjust,
		float4 farReprojRow0,
		float4 farReprojRow1,
		float4 farReprojRow2,
		float4 farReprojRow3,
		float4 nearReprojRow0,
		float4 nearReprojRow1,
		float4 nearReprojRow2,
		float4 nearReprojRow3,
		out float4 color)
	{
		color = 0.0;
		float3 viewPosition;
		if (!TryGetViewPositionFromScreenPosition(
				pixelPosition,
				farReprojRow0,
				farReprojRow1,
				farReprojRow2,
				farReprojRow3,
				nearReprojRow0,
				nearReprojRow1,
				nearReprojRow2,
				nearReprojRow3,
				viewPosition))
			return false;

		return TryGetDebugColorFromViewPosition(
			viewPosition,
			textureSampler,
			viewToWorldRow0,
			viewToWorldRow1,
			viewToWorldRow2,
			cameraPosAdjust,
			color);
	}
}

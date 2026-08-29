// SPDX-License-Identifier: GPL-3.0-only
#include "Common/SharedData.hlsli"

namespace TerrainShadows
{
	Texture2D<float2> ShadowHeightTexture : register(t30);
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
				textureSampler, GetTerrainShadowUV(worldPosition.xy), 0).x;
			height = lerp(
				SharedData::terrainShadowsSettings.HeightRange.x,
				SharedData::terrainShadowsSettings.HeightRange.y,
				height);
			value = saturate(
				(height - SharedData::terrainShadowsSettings.ZRange.x) /
				(SharedData::terrainShadowsSettings.ZRange.y -
				 SharedData::terrainShadowsSettings.ZRange.x));
			return true;
		}
		return false;
	}

	bool TryGetDebugColor(
		float3 viewPosition,
		SamplerState textureSampler,
		out float4 color)
	{
		float value;
		const bool active = TryGetDebugValue(
			SharedData::ViewToWorldPosition(viewPosition),
			textureSampler,
			value);
		color = float4(value.xxx, 1.0);
		return active;
	}
}

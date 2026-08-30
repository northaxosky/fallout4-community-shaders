#pragma once

#include <cstddef>
#include <cstdint>

namespace cs
{
	// mirrors HLSL FeatureData at b6; blocks exist even when unloaded
	struct alignas(16) ScreenSpaceShadowsFeatureData
	{
		std::uint32_t EnableScreenSpaceShadows = 0;
		float         ShadowContrast = 0.0f;
		std::uint32_t pad0[2]{};
	};
	static_assert(sizeof(ScreenSpaceShadowsFeatureData) == 16);

	struct alignas(16) ScreenSpaceGIFeatureData
	{
		std::uint32_t EnableScreenSpaceGI = 0;
		std::uint32_t pad0 = 0;
		float         AoPower = 0.0f;
		float         BounceStrength = 0.0f;
	};
	static_assert(sizeof(ScreenSpaceGIFeatureData) == 16);

	struct alignas(16) WetnessEffectsFeatureData
	{
		float Wetness = 0.0f;
		float MaxRainWetness = 0.0f;
		float MinRainWetness = 0.0f;
		std::uint32_t DebugVisualization = 0;
	};
	static_assert(sizeof(WetnessEffectsFeatureData) == 16);

	struct alignas(16) TerrainShadowsFeatureData
	{
		std::uint32_t TerrainShadowMode = 0;
		float         Scale[3]{};
		float         ZRange[2]{};
		float         Offset[2]{};
		float         HeightRange[2]{};
		float         DebugHeightRange[2]{};
	};
	static_assert(sizeof(TerrainShadowsFeatureData) == 48);

	struct alignas(16) InverseSquareLightingFeatureData
	{
		std::uint32_t Mode = 0;
		float         ExteriorStrength = 0.0f;
		float         InteriorStrength = 0.0f;
		float         NearFieldDistance = 0.0f;
	};
	static_assert(sizeof(InverseSquareLightingFeatureData) == 16);

	struct alignas(16) WaterEffectsFeatureData
	{
		std::uint32_t Mode = 0;
		std::uint32_t HasWater = 0;
		// absolute world Z of the player cell's water plane
		float         WaterHeight = 0.0f;
		float         pad0 = 0.0f;
	};
	static_assert(sizeof(WaterEffectsFeatureData) == 16);

	struct alignas(16) FeatureDataCB
	{
		ScreenSpaceShadowsFeatureData     screenSpaceShadowsSettings;
		ScreenSpaceGIFeatureData          screenSpaceGISettings;
		WetnessEffectsFeatureData         wetnessEffectsSettings;
		TerrainShadowsFeatureData         terrainShadowsSettings;
		InverseSquareLightingFeatureData inverseSquareLightingSettings;
		WaterEffectsFeatureData           waterEffectsSettings;
	};
	static_assert(sizeof(FeatureDataCB) == 128);
	static_assert(sizeof(FeatureDataCB) % 16 == 0);
	static_assert(offsetof(FeatureDataCB, wetnessEffectsSettings) == 32);
	static_assert(offsetof(WetnessEffectsFeatureData, Wetness) == 0);
	static_assert(offsetof(WetnessEffectsFeatureData, MaxRainWetness) == 4);
	static_assert(offsetof(WetnessEffectsFeatureData, MinRainWetness) == 8);
	static_assert(offsetof(WetnessEffectsFeatureData, DebugVisualization) == 12);
	static_assert(offsetof(FeatureDataCB, terrainShadowsSettings) == 48);
	static_assert(offsetof(TerrainShadowsFeatureData, TerrainShadowMode) == 0);
	static_assert(offsetof(TerrainShadowsFeatureData, Scale) == 4);
	static_assert(offsetof(TerrainShadowsFeatureData, ZRange) == 16);
	static_assert(offsetof(TerrainShadowsFeatureData, Offset) == 24);
	static_assert(offsetof(TerrainShadowsFeatureData, HeightRange) == 32);
	static_assert(offsetof(TerrainShadowsFeatureData, DebugHeightRange) == 40);
	static_assert(offsetof(FeatureDataCB, inverseSquareLightingSettings) == 96);
	static_assert(offsetof(InverseSquareLightingFeatureData, Mode) == 0);
	static_assert(offsetof(InverseSquareLightingFeatureData, ExteriorStrength) == 4);
	static_assert(offsetof(InverseSquareLightingFeatureData, InteriorStrength) == 8);
	static_assert(offsetof(InverseSquareLightingFeatureData, NearFieldDistance) == 12);
	static_assert(offsetof(FeatureDataCB, waterEffectsSettings) == 112);
	static_assert(offsetof(WaterEffectsFeatureData, Mode) == 0);
	static_assert(offsetof(WaterEffectsFeatureData, HasWater) == 4);
	static_assert(offsetof(WaterEffectsFeatureData, WaterHeight) == 8);
	static_assert(offsetof(WaterEffectsFeatureData, pad0) == 12);

	// inactive contributors leave zeroed blocks
	FeatureDataCB GetFeatureBufferData();
}

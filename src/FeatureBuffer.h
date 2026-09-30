#pragma once

#include <cstddef>
#include <cstdint>

namespace cs
{
	// Feature contributors retain host contracts; the substrate packs shared and FO4-only fields.
	struct alignas(16) ScreenSpaceGIFeatureData
	{
		std::uint32_t EnableScreenSpaceGI = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(ScreenSpaceGIFeatureData) == 16);

	struct alignas(16) WetnessEffectsFeatureData
	{
		float Wetness = 0.0f;
		float MaxRainWetness = 0.0f;
		float MinRainWetness = 0.0f;
		std::uint32_t DebugVisualization = 0;
		float PuddleRadius = 1.0f;
		float PuddleMaxAngle = 0.95f;
		float MaxPuddleWetness = 1.5f;
		float PuddleWetness = 0.0f;
		float MaxShoreWetness = 0.0f;
		std::uint32_t ShoreRange = 32;
		std::uint32_t EnableWetnessEffects = 0;
		std::uint32_t Active = 0;
	};
	static_assert(sizeof(WetnessEffectsFeatureData) == 48);

	struct alignas(16) TerrainShadowsFeatureData
	{
		std::uint32_t TerrainShadowMode = 0;
		float Scale[3]{};
		float ZRange[2]{};
		float Offset[2]{};
		float HeightRange[2]{};
		float DebugHeightRange[2]{};
	};
	static_assert(sizeof(TerrainShadowsFeatureData) == 48);

	struct alignas(16) InverseSquareLightingFeatureData
	{
		std::uint32_t Mode = 0;
		float ExteriorStrength = 0.0f;
		float InteriorStrength = 0.0f;
		float NearFieldDistance = 0.0f;
	};
	static_assert(sizeof(InverseSquareLightingFeatureData) == 16);

	struct alignas(16) DynamicCubemapsFeatureData
	{
		std::uint32_t Enabled = 0;
		std::uint32_t DebugVisualization = 0;
		std::uint32_t EnabledSSR = 0;
		std::uint32_t pad0 = 0;
	};
	static_assert(sizeof(DynamicCubemapsFeatureData) == 16);

	struct alignas(16) ExponentialHeightFogFeatureData
	{
		std::uint32_t Mode = 0;
		float DensityMultiplier = 0.0f;
		float HeightFalloffMultiplier = 0.0f;
		float pad0 = 0.0f;
	};
	static_assert(sizeof(ExponentialHeightFogFeatureData) == 16);

	struct alignas(16) FeatureDataCB
	{
		ScreenSpaceGIFeatureData screenSpaceGISettings;
		WetnessEffectsFeatureData wetnessEffectsSettings;
		TerrainShadowsFeatureData terrainShadowsSettings;
		InverseSquareLightingFeatureData inverseSquareLightingSettings;
		DynamicCubemapsFeatureData dynamicCubemapsSettings;
		ExponentialHeightFogFeatureData exponentialHeightFogSettings;
	};
	static_assert(sizeof(FeatureDataCB) == 160);
	static_assert(sizeof(FeatureDataCB) % 16 == 0);
	static_assert(offsetof(FeatureDataCB, wetnessEffectsSettings) == 16);
	static_assert(offsetof(WetnessEffectsFeatureData, Wetness) == 0);
	static_assert(offsetof(WetnessEffectsFeatureData, MaxRainWetness) == 4);
	static_assert(offsetof(WetnessEffectsFeatureData, MinRainWetness) == 8);
	static_assert(offsetof(WetnessEffectsFeatureData, DebugVisualization) == 12);
	static_assert(offsetof(WetnessEffectsFeatureData, PuddleRadius) == 16);
	static_assert(offsetof(WetnessEffectsFeatureData, PuddleMaxAngle) == 20);
	static_assert(offsetof(WetnessEffectsFeatureData, MaxPuddleWetness) == 24);
	static_assert(offsetof(WetnessEffectsFeatureData, PuddleWetness) == 28);
	static_assert(offsetof(WetnessEffectsFeatureData, MaxShoreWetness) == 32);
	static_assert(offsetof(WetnessEffectsFeatureData, ShoreRange) == 36);
	static_assert(offsetof(WetnessEffectsFeatureData, EnableWetnessEffects) == 40);
	static_assert(offsetof(WetnessEffectsFeatureData, Active) == 44);
	static_assert(offsetof(FeatureDataCB, terrainShadowsSettings) == 64);
	static_assert(offsetof(TerrainShadowsFeatureData, TerrainShadowMode) == 0);
	static_assert(offsetof(TerrainShadowsFeatureData, Scale) == 4);
	static_assert(offsetof(TerrainShadowsFeatureData, ZRange) == 16);
	static_assert(offsetof(TerrainShadowsFeatureData, Offset) == 24);
	static_assert(offsetof(TerrainShadowsFeatureData, HeightRange) == 32);
	static_assert(offsetof(TerrainShadowsFeatureData, DebugHeightRange) == 40);
	static_assert(offsetof(FeatureDataCB, inverseSquareLightingSettings) == 112);
	static_assert(offsetof(InverseSquareLightingFeatureData, Mode) == 0);
	static_assert(offsetof(InverseSquareLightingFeatureData, ExteriorStrength) == 4);
	static_assert(offsetof(InverseSquareLightingFeatureData, InteriorStrength) == 8);
	static_assert(offsetof(InverseSquareLightingFeatureData, NearFieldDistance) == 12);
	static_assert(offsetof(FeatureDataCB, dynamicCubemapsSettings) == 128);
	static_assert(offsetof(DynamicCubemapsFeatureData, Enabled) == 0);
	static_assert(offsetof(DynamicCubemapsFeatureData, DebugVisualization) == 4);
	static_assert(offsetof(DynamicCubemapsFeatureData, EnabledSSR) == 8);
	static_assert(offsetof(FeatureDataCB, exponentialHeightFogSettings) == 144);
	static_assert(offsetof(ExponentialHeightFogFeatureData, Mode) == 0);
	static_assert(offsetof(ExponentialHeightFogFeatureData, DensityMultiplier) == 4);
	static_assert(
		offsetof(ExponentialHeightFogFeatureData, HeightFalloffMultiplier) == 8);

	// inactive contributors leave zeroed blocks
	FeatureDataCB GetFeatureBufferData();
}

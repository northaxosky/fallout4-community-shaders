#pragma once

#include "Render/SharedFeatureData.h"
#include <cstddef>
#include <cstdint>

namespace cs
{
	// Feature contributors retain host contracts; the substrate packs shared and FO4-only fields.
	using WetnessEffectsFeatureData = render::WetnessEffectsSettings;

	struct alignas(16) TerrainShadowsFeatureData
	{
		std::uint32_t EnableTerrainShadow = 0;
		float Scale[3]{};
		float ZRange[2]{};
		float Offset[2]{};
		float ZBlur = 0.0f;
		float pad0[3]{};
	};
	static_assert(sizeof(TerrainShadowsFeatureData) == 48);

	struct alignas(16) DynamicCubemapsFeatureData
	{
		std::uint32_t Enabled = 0;
		std::uint32_t DebugVisualization = 0;
		std::uint32_t EnabledSSR = 0;
		std::uint32_t pad0 = 0;
	};
	static_assert(sizeof(DynamicCubemapsFeatureData) == 16);

	struct alignas(16) FeatureDataCB
	{
		WetnessEffectsFeatureData wetnessEffectsSettings;
		TerrainShadowsFeatureData terrainShadowsSettings;
		DynamicCubemapsFeatureData dynamicCubemapsSettings;
	};
	static_assert(sizeof(FeatureDataCB) == 256);
	static_assert(sizeof(FeatureDataCB) % 16 == 0);
	static_assert(offsetof(FeatureDataCB, wetnessEffectsSettings) == 0);
	static_assert(offsetof(FeatureDataCB, terrainShadowsSettings) == 192);
	static_assert(offsetof(TerrainShadowsFeatureData, EnableTerrainShadow) == 0);
	static_assert(offsetof(TerrainShadowsFeatureData, Scale) == 4);
	static_assert(offsetof(TerrainShadowsFeatureData, ZRange) == 16);
	static_assert(offsetof(TerrainShadowsFeatureData, Offset) == 24);
	static_assert(offsetof(TerrainShadowsFeatureData, ZBlur) == 32);
	static_assert(offsetof(FeatureDataCB, dynamicCubemapsSettings) == 240);
	static_assert(offsetof(DynamicCubemapsFeatureData, Enabled) == 0);
	static_assert(offsetof(DynamicCubemapsFeatureData, DebugVisualization) == 4);
	static_assert(offsetof(DynamicCubemapsFeatureData, EnabledSSR) == 8);

	// inactive contributors leave zeroed blocks
	FeatureDataCB GetFeatureBufferData();
}

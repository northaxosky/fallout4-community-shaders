#pragma once

#include "Render/SharedFeatureData.h"
#include <cstddef>
#include <cstdint>

namespace cs
{
	// Feature contributors retain host contracts; the substrate packs shared and FO4-only fields.
	using LODBlendingFeatureData = render::LODBlendingSettings;
	using TerrainBlendingFeatureData = render::TerrainBlendingSettings;
	using RainRefractionFeatureData = render::RainRefractionSettings;
	using ExtendedMaterialsFeatureData = render::CPMSettings;

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
		std::uint32_t EnabledSSR = 0;
		float MaterialReflections = 0.0f;
		std::uint32_t pad0 = 0;
	};
	static_assert(sizeof(DynamicCubemapsFeatureData) == 16);

	struct alignas(16) WaterEffectsFeatureData
	{
		std::uint32_t EnabledParallax = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(WaterEffectsFeatureData) == 16);

	struct alignas(16) TerrainVariationFeatureData
	{
		render::TerrainVariationSettings shared;
		std::uint32_t Enabled = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(TerrainVariationFeatureData) == 32);

	struct alignas(16) SkylightingFeatureData
	{
		render::SkylightingSettings shared;
		std::uint32_t Enabled = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(SkylightingFeatureData) == 160);

	struct alignas(16) ScreenSpaceGIFeatureData
	{
		std::uint32_t Enabled = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(ScreenSpaceGIFeatureData) == 16);

	struct alignas(16) UnifiedWaterFeatureData
	{
		std::uint32_t Enabled = 0;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(UnifiedWaterFeatureData) == 16);

	struct alignas(16) FeatureDataCB
	{
		TerrainShadowsFeatureData terrainShadowsSettings;
		DynamicCubemapsFeatureData dynamicCubemapsSettings;
		LODBlendingFeatureData lodBlendingSettings;
		WaterEffectsFeatureData waterEffectsSettings;
		TerrainVariationFeatureData terrainVariationSettings;
		SkylightingFeatureData skylightingSettings;
		ScreenSpaceGIFeatureData screenSpaceGISettings;
		render::IBLSettings iblSettings;
		UnifiedWaterFeatureData unifiedWaterSettings;
		ExtendedMaterialsFeatureData extendedMaterialsSettings;
		TerrainBlendingFeatureData terrainBlendingSettings;
		RainRefractionFeatureData rainRefractionSettings;
	};
	static_assert(sizeof(FeatureDataCB) == 448);
	static_assert(sizeof(FeatureDataCB) % 16 == 0);
	static_assert(offsetof(FeatureDataCB, terrainShadowsSettings) == 0);
	static_assert(offsetof(TerrainShadowsFeatureData, EnableTerrainShadow) == 0);
	static_assert(offsetof(TerrainShadowsFeatureData, Scale) == 4);
	static_assert(offsetof(TerrainShadowsFeatureData, ZRange) == 16);
	static_assert(offsetof(TerrainShadowsFeatureData, Offset) == 24);
	static_assert(offsetof(TerrainShadowsFeatureData, ZBlur) == 32);
	static_assert(offsetof(FeatureDataCB, dynamicCubemapsSettings) == 48);
	static_assert(offsetof(FeatureDataCB, lodBlendingSettings) == 64);
	static_assert(offsetof(FeatureDataCB, waterEffectsSettings) == 96);
	static_assert(offsetof(FeatureDataCB, terrainVariationSettings) == 112);
	static_assert(offsetof(FeatureDataCB, skylightingSettings) == 144);
	static_assert(offsetof(FeatureDataCB, iblSettings) == 320);
	static_assert(offsetof(FeatureDataCB, unifiedWaterSettings) == 368);
	static_assert(offsetof(FeatureDataCB, extendedMaterialsSettings) == 384);
	static_assert(offsetof(FeatureDataCB, terrainBlendingSettings) == 416);
	static_assert(offsetof(FeatureDataCB, rainRefractionSettings) == 432);
	static_assert(offsetof(DynamicCubemapsFeatureData, Enabled) == 0);
	static_assert(offsetof(DynamicCubemapsFeatureData, EnabledSSR) == 4);

	// Inactive or disabled blocks are zeroed; LOD, Skylighting stay neutral.
	FeatureDataCB GetFeatureBufferData();
}

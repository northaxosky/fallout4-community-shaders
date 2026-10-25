#include "FeatureBuffer.h"

#include "DynamicCubemaps.h"
#include "ExtendedMaterials.h"
#include "ImageBasedLighting.h"
#include "LODBlending.h"
#include "RainRefraction.h"
#include "ScreenSpaceGI.h"
#include "Skylighting.h"
#include "TerrainBlending.h"
#include "TerrainShadows.h"
#include "TerrainVariation.h"
#include "UnifiedWater.h"
#include "WaterEffects.h"

namespace cs
{
	namespace
	{
		template <class Data, class Feature>
		Data CollectFeatureData(Feature* a_feature)
		{
			if (!a_feature || !a_feature->IsHealthy() || !a_feature->IsEnabled())
				return {};
			return a_feature->GetCommonBufferData();
		}
	}

	FeatureDataCB GetFeatureBufferData()
	{
		return {
			.terrainShadowsSettings =
				CollectFeatureData<TerrainShadowsFeatureData>(
					features::TerrainShadows::GetSingleton()),
			.dynamicCubemapsSettings =
				CollectFeatureData<DynamicCubemapsFeatureData>(
					features::DynamicCubemaps::GetSingleton()),
			.lodBlendingSettings =
				CollectFeatureData<LODBlendingFeatureData>(
					features::LODBlending::GetSingleton()),
			.waterEffectsSettings =
				CollectFeatureData<WaterEffectsFeatureData>(
					features::WaterEffects::GetSingleton()),
			.terrainVariationSettings =
				CollectFeatureData<TerrainVariationFeatureData>(
					features::TerrainVariation::GetSingleton()),
			.skylightingSettings =
				CollectFeatureData<SkylightingFeatureData>(
					features::Skylighting::GetSingleton()),
			.screenSpaceGISettings =
				CollectFeatureData<ScreenSpaceGIFeatureData>(
					features::ScreenSpaceGI::GetSingleton()),
			.iblSettings =
				CollectFeatureData<render::IBLSettings>(
					features::ImageBasedLighting::GetSingleton()),
			.unifiedWaterSettings =
				CollectFeatureData<UnifiedWaterFeatureData>(
					features::UnifiedWater::GetSingleton()),
			.extendedMaterialsSettings =
				CollectFeatureData<ExtendedMaterialsFeatureData>(
					features::ExtendedMaterials::GetSingleton()),
			.terrainBlendingSettings =
				CollectFeatureData<TerrainBlendingFeatureData>(
					features::TerrainBlending::GetSingleton()),
			.rainRefractionSettings =
				CollectFeatureData<RainRefractionFeatureData>(
					features::RainRefraction::GetSingleton())
		};
	}
}

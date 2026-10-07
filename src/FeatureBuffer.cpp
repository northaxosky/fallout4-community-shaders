#include "FeatureBuffer.h"

#include "DynamicCubemaps.h"
#include "ExponentialHeightFog.h"
#include "LODBlending.h"
#include "ScreenSpaceGI.h"
#include "Skylighting.h"
#include "TerrainShadows.h"
#include "TerrainVariation.h"
#include "WaterEffects.h"

namespace cs
{
	namespace
	{
		template <class Data, class Feature>
		Data CollectFeatureData(Feature* a_feature)
		{
			if (!a_feature || !a_feature->IsHealthy())
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
			.exponentialHeightFogSettings =
				CollectFeatureData<render::ExponentialHeightFogSettings>(
					features::ExponentialHeightFog::GetSingleton()),
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
					features::ScreenSpaceGI::GetSingleton())
		};
	}
}

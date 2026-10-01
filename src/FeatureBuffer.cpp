#include "FeatureBuffer.h"

#include "DynamicCubemaps.h"
#include "ExponentialHeightFog.h"
#include "ScreenSpaceGI.h"
#include "TerrainShadows.h"
#include "WetnessEffects.h"

namespace cs
{
	namespace
	{
		template <class Data, class Feature>
		Data CollectFeatureData(Feature* a_feature)
		{
			if (!a_feature || !a_feature->IsLoaded())
				return {};
			return a_feature->GetCommonBufferData();
		}
	}

	FeatureDataCB GetFeatureBufferData()
	{
		return {
			.screenSpaceGISettings =
				CollectFeatureData<ScreenSpaceGIFeatureData>(
					features::ScreenSpaceGI::GetSingleton()),
			.wetnessEffectsSettings =
				CollectFeatureData<WetnessEffectsFeatureData>(
					features::WetnessEffects::GetSingleton()),
			.terrainShadowsSettings =
				CollectFeatureData<TerrainShadowsFeatureData>(
					features::TerrainShadows::GetSingleton()),
			.dynamicCubemapsSettings =
				CollectFeatureData<DynamicCubemapsFeatureData>(
					features::DynamicCubemaps::GetSingleton()),
			.exponentialHeightFogSettings =
				CollectFeatureData<ExponentialHeightFogFeatureData>(
					features::ExponentialHeightFog::GetSingleton())
		};
	}
}

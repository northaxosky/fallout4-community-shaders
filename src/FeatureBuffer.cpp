#include "FeatureBuffer.h"

#include "DynamicCubemaps.h"
#include "ExponentialHeightFog.h"
#include "TerrainShadows.h"
#include "WetnessEffects.h"

namespace cs
{
	namespace
	{
		template <class Data, class Feature, class... Args>
		Data CollectFeatureData(Feature* a_feature, Args... a_args)
		{
			if (!a_feature || !a_feature->IsLoaded())
				return {};
			return a_feature->GetCommonBufferData(a_args...);
		}
	}

	FeatureDataCB GetFeatureBufferData()
	{
		const auto dynamicCubemaps = CollectFeatureData<DynamicCubemapsFeatureData>(
			features::DynamicCubemaps::GetSingleton());
		return {
			.wetnessEffectsSettings =
				CollectFeatureData<WetnessEffectsFeatureData>(
					features::WetnessEffects::GetSingleton(), dynamicCubemaps.Enabled != 0),
			.terrainShadowsSettings =
				CollectFeatureData<TerrainShadowsFeatureData>(
					features::TerrainShadows::GetSingleton()),
			.dynamicCubemapsSettings = dynamicCubemaps,
			.exponentialHeightFogSettings =
				CollectFeatureData<render::ExponentialHeightFogSettings>(
					features::ExponentialHeightFog::GetSingleton())
		};
	}
}

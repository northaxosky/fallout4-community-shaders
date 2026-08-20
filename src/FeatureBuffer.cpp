#include "FeatureBuffer.h"

#include "ScreenSpaceGI.h"
#include "ScreenSpaceShadows.h"
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
			.screenSpaceShadowsSettings =
				CollectFeatureData<ScreenSpaceShadowsFeatureData>(
					features::ScreenSpaceShadows::GetSingleton()),
			.screenSpaceGISettings =
				CollectFeatureData<ScreenSpaceGIFeatureData>(
					features::ScreenSpaceGI::GetSingleton()),
			.wetnessEffectsSettings =
				CollectFeatureData<WetnessEffectsFeatureData>(
					features::WetnessEffects::GetSingleton())
		};
	}
}

#include "Feature.h"

#include "Log.h"

namespace { auto* L = cs::log::Get("cs"); }

namespace cs
{
	FeatureManager& FeatureManager::Get()
	{
		static FeatureManager instance;
		return instance;
	}

	void FeatureManager::Register(Feature* a_feature)
	{
		_features.push_back(a_feature);
	}

	void FeatureManager::LoadAll()
	{
		for (auto* feature : _features) {
			L->info("Loading feature: {}", feature->GetName());
			feature->Load();
		}
	}

	void FeatureManager::OnDataLoadedAll()
	{
		for (auto* feature : _features) {
			feature->OnDataLoaded();
		}
	}

	void FeatureManager::OnPostPostLoadAll()
	{
		for (auto* feature : _features) {
			feature->OnPostPostLoad();
		}
	}
}

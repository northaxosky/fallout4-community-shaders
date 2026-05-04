#pragma once

#include <string_view>
#include <vector>

namespace cs
{
	class Feature
	{
	public:
		virtual ~Feature() = default;

		virtual std::string_view GetName() const = 0;

		virtual void Load() {}
		virtual void OnDataLoaded() {}
		virtual void OnPostPostLoad() {}
	};

	class FeatureManager
	{
	public:
		static FeatureManager& Get();

		void Register(Feature* a_feature);

		void LoadAll();
		void OnDataLoadedAll();
		void OnPostPostLoadAll();

	private:
		FeatureManager() = default;
		std::vector<Feature*> _features;
	};
}

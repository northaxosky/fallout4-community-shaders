#pragma once

#include "Feature.h"

namespace cs::features
{
	class MotionVectorFixes : public Feature
	{
	public:
		static MotionVectorFixes* GetSingleton();

		std::string_view GetName() const override { return "MotionVectorFixes"; }
		std::string GetFeatureSummary() const override { return "Restores correct motion vectors for upscalers and frame generation."; }
		std::string GetCategory() const override { return "Compatibility"; }

		void Load() override;
		void OnDataLoaded() override;
		void DrawSettings() override;
	};
}

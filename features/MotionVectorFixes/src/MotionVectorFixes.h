#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

namespace cs::features
{
	class MotionVectorFixes : public Feature
	{
	public:
		static MotionVectorFixes* GetSingleton();

		std::string_view GetName() const override { return "MotionVectorFixes"; }
		std::string GetFeatureSummary() const override { return "Restores correct motion vectors for upscalers and frame generation."; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }

		void Load() override;
		void OnDataLoaded() override;
		void DrawSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	private:
		bool _setSeqHooked = false;
	};
}

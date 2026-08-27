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
		std::string GetCategory() const override { return FeatureCategories::kCompatibility; }
		std::string GetFeatureSummary() const override
		{
			return "Corrects previous transforms for the player, animated objects, frozen scenes, and LOD geometry.";
		}

		void Load() override;
		void OnDataLoaded() override;
		void DrawSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	private:
		bool _playerUpdateHooked = false;
		bool _setSequencePositionHooked = false;
		bool _menuSinkRegistered = false;
	};
}

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
		std::string_view GetDisplayName() const override { return "Motion Vector Fixes"; }
		std::string GetCategory() const override { return FeatureCategories::kCompatibility; }

		void Load() override;
		void OnDataLoaded() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	private:
		bool _playerUpdateHooked = false;
		bool _setSequencePositionHooked = false;
		bool _menuSinkRegistered = false;
	};
}

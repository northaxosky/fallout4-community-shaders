#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "ShaderDefines.h"

namespace cs::features
{
	class MotionVectorFixes : public ShaderFeature<motion_vector_fixes_shader::kShaderDefines>
	{
	public:
		static MotionVectorFixes* GetSingleton();

		std::string_view GetName() const override { return "MotionVectorFixes"; }
		std::string_view GetDisplayName() const override { return "Motion Vector Fixes"; }
		std::string GetCategory() const override { return FeatureCategories::kDisplay; }
		// The CPU corrections keep running while shader ownership is off.
		bool FollowsShaderOwnership() const override { return false; }

		void Load() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		void WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4) override;

	private:
		bool _playerUpdateHooked = false;
		bool _setSequencePositionHooked = false;
	};
}

#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "FrameGenerationSettings.h"
#include "Render/TemporalPipeline.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace cs::features
{
	class FrameGeneration : public Feature
	{
	public:
		using Method = frame_generation::Method;
		using Settings = frame_generation::Settings;

		static FrameGeneration* GetSingleton();

		std::string_view GetName() const override { return "FrameGeneration"; }
		std::string_view GetDisplayName() const override { return "Frame Generation"; }
		bool SupportsEnableToggle() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kDisplay; }
		std::string GetFeatureSummary() const override
		{
			return "Frame generation for a smoother experience.";
		}
		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		void OnRuntimeQuarantined(std::string_view a_phase, std::string_view a_reason) noexcept override;

		Settings settings;

	private:
		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(frame_generation::kSchema, settings); }
		FeatureDebugTexture GetDebugTexture(render::FrameGenerationDebugResource a_view) const;
	};
}

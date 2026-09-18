#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/TemporalRenderSettings.h"

#include <optional>

namespace cs::features
{
	class Upscaling : public Feature
	{
	public:
		using UpscaleMethod = render::temporal::UpscaleMethod;
		using Settings = render::temporal::UpscalingSettings;

		static Upscaling* GetSingleton();

		std::string_view GetName() const override { return "Upscaling"; }
		std::string_view GetDisplayName() const override { return "Upscaling"; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }
		std::string GetFeatureSummary() const override
		{
			return "DLSS, FSR 3, and FSR 4 super resolution with independently selected frame generation.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		settings::RestartSettingsView GetRestartSettings() const noexcept override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		bool ParticipatesInPresets() const override { return true; }
		bool StageFromPreset(const toml::table& a_table,
			const PresetApplyContext& a_context,
			std::string& a_error) override;
		void CommitStagedSwap() noexcept override;
		void CommitStagedFinalize() override;
		void ExportToPreset(toml::table& a_out) override;

		Settings settings;

	private:
		Upscaling() = default;
		void SaveSettings();
		Settings _bootSettings;
		std::optional<Settings> _stagedSettings;
	};
}

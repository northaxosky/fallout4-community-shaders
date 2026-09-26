#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "FrameGenerationSettings.h"

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
		std::string GetPresetKey() const override { return "frame_generation"; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }
		std::string GetFeatureSummary() const override
		{
			return "Independent frame-generation request, admission, and runtime diagnostics.";
		}
		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
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
		enum class DebugView : std::uint8_t
		{
			kOff,
			kHudless,
			kFinal,
			kDepth,
			kMotion
		};

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(frame_generation::kSchema); }
		FeatureDebugTexture GetDebugTexture(DebugView a_view) const;

		std::optional<Settings> _stagedSettings;
	};
}

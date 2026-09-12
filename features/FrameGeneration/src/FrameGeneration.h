#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

#include <cstdint>
#include <string_view>

namespace cs::features
{
	class FrameGeneration : public Feature
	{
	public:
		enum class Method : std::uint32_t
		{
			kOff,
			kFSR3,
			kDLSSG
		};

		struct Settings
		{
			bool enabled = true;
			std::uint32_t frameGenerationMethod =
				static_cast<std::uint32_t>(Method::kFSR3);
			std::uint32_t frameGenerationForceEnable = 0;
			bool frameGenerationAllowInMenus = false;
			bool detailedDiagnostics = false;
		};

		static FrameGeneration* GetSingleton();

		std::string_view GetName() const override { return "FrameGeneration"; }
		std::string_view GetDisplayName() const override { return "Frame Generation"; }
		std::string GetCategory() const override { return FeatureCategories::kPerformance; }
		std::string GetFeatureSummary() const override
		{
			return "Independent frame-generation request, admission, and runtime diagnostics.";
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

		void SaveSettings();
		FeatureDebugTexture GetDebugTexture(DebugView a_view) const;

		Settings _bootSettings;
	};
}

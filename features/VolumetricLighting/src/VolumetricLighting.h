#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "VolumetricLightingSettings.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace cs::features
{
	class VolumetricLighting : public Feature
	{
	public:
		static VolumetricLighting* GetSingleton();

		std::string_view GetName() const override { return "VolumetricLighting"; }
		std::string_view GetDisplayName() const override { return "Volumetric Lighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Per-location enable and quality for the game's native godrays.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnRuntimeQuarantined(std::string_view, std::string_view) noexcept override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		using Settings = volumetric_lighting::Settings;

	private:
		VolumetricLighting() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(volumetric_lighting::kSchema, _settings); }

		void OnPreDeferredLights() noexcept;
		void Restore() noexcept;

		Settings _settings;

		// Render thread only; the override spans one DeferredLightsImpl call.
		std::atomic_bool _applied{ false };
		bool _savedRuntimeDisabled = false;
		std::int32_t _savedQuality = 0;

		std::atomic_bool _libraryPresent{ false };
		std::atomic_bool _interior{ false };
		std::atomic_bool _appliedEnabled{ false };
		std::atomic<std::int32_t> _appliedQuality{ 0 };
		std::atomic<std::uint64_t> _appliedFrames{ 0 };
	};
}

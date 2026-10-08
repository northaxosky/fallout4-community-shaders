#pragma once

#include "Feature.h"
#include "FeatureBuffer.h"
#include "FeatureCategories.h"
#include "Render/ShaderSubclassHooks.h"
#include "ShaderDefines.h"
#include "TerrainVariationSettings.h"

#include <atomic>
#include <string>

namespace cs::features
{
	class TerrainVariation : public ShaderFeature<terrain_variation_shader::kShaderDefines>
	{
	public:
		static TerrainVariation* GetSingleton();

		std::string_view GetName() const override { return "TerrainVariation"; }
		std::string_view GetDisplayName() const override { return "Terrain Variation"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Stochastic texture sampling that breaks up terrain tiling.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		cs::TerrainVariationFeatureData GetCommonBufferData() const;

		using Settings = terrain_variation::Settings;

	private:
		TerrainVariation() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(terrain_variation::kSchema, _settings); }

		Settings _settings;
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
	};
}

#pragma once

#include "Feature.h"
#include "LightLimitFixSettings.h"
#include "ShaderDefines.h"

namespace cs::features
{
	class LightLimitFix : public ShaderFeature<light_limit_fix::kShaderDefines>
	{
	public:
		static LightLimitFix* GetSingleton();
		std::string_view GetName() const override { return "LightLimitFix"; }
		std::string_view GetDisplayName() const override { return "Light Limit Fix"; }
		bool SupportsEnableToggle() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Every nearby light on effects and water; inverse-square falloff for authored lights.";
		}
		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnDataLoaded() override;
		void OnPostPostLoad() override;
		void OnRuntimeQuarantined(std::string_view, std::string_view) noexcept override;
		void Prepass() override;
		void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB& a_fo4) override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		FullscreenDebugData GetFullscreenDebugData() const noexcept override;
		void DrawSettings() override;
		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(light_limit_fix::kSchema, _settings); }
		std::vector<std::string_view> GetRestartSettings() const override;
		void RestoreDefaultSettings() override;
		// Applies at the next launch: the boot value gates light creation.
		bool DeriveUnauthoredLights() const noexcept { return _bootSettings.deriveUnauthoredLights; }
		void DrawFailLoadMessage() override { DrawSettings(); }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	private:
		LightLimitFix() = default;

		light_limit_fix::Settings _settings;
		light_limit_fix::Settings _bootSettings;
	};
}

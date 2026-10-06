#pragma once

#include "Feature.h"
#include "InverseSquareLightingSettings.h"
#include "ShaderDefines.h"

namespace cs::features
{
	class InverseSquareLighting : public ShaderFeature<isl::kShaderDefines>
	{
	public:
		static InverseSquareLighting* GetSingleton();
		std::string_view GetName() const override { return "InverseSquareLighting"; }
		std::string_view GetDisplayName() const override { return "Inverse Square Lighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Physically accurate inverse-square light falloff.";
		}
		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnDataLoaded() override;
		void OnPostPostLoad() override;
		void OnRuntimeQuarantined() noexcept override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override
		{
			return settings::MakeSchemaView(inverse_square_lighting::kSchema);
		}
		std::vector<std::string_view> GetRestartSettings() const override;
		bool HasResettableSettings() const override { return true; }
		void RestoreDefaultSettings() override;
		// Applies at the next launch: the boot value gates light creation.
		bool DeriveUnauthoredLights() const noexcept { return _bootSettings.deriveUnauthoredLights; }
		void DrawFailLoadMessage() override { DrawSettings(); }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

	private:
		InverseSquareLighting() = default;

		inverse_square_lighting::Settings _settings;
		inverse_square_lighting::Settings _bootSettings;
	};
}

#pragma once

#include "Feature.h"
#include "ShaderDefines.h"
#include "VanillaPBRSettings.h"

namespace cs::features
{
	class VanillaPBR : public ShaderFeature<vanilla_pbr::kShaderDefines>
	{
	public:
		static VanillaPBR* GetSingleton();
		std::string_view GetName() const override { return "VanillaPBR"; }
		std::string_view GetDisplayName() const override { return "Vanilla PBR"; }
		std::string GetCategory() const override { return FeatureCategories::kMaterials; }
		std::string GetFeatureSummary() const override { return "GGX direct specular for vanilla materials."; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB&) override;
		void DrawSettings() override;
		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(vanilla_pbr::kSchema, _settings); }
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink&) const override;

	private:
		bool ValidateInjectionPrerequisites(std::string&) override;
		vanilla_pbr::Settings _settings;
		std::atomic_bool _directEnabled{ false };
	};
}

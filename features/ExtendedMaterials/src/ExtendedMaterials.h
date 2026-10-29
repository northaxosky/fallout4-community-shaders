#pragma once

#include "ExtendedMaterialsSettings.h"
#include "Feature.h"
#include "FeatureCategories.h"
#include "ShaderDefines.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace cs::features
{
	class ExtendedMaterials : public ShaderFeature<extended_materials_shader::kShaderDefines>
	{
	public:
		static ExtendedMaterials* GetSingleton();

		std::string_view GetName() const override { return "ExtendedMaterials"; }
		std::string_view GetDisplayName() const override { return "Extended Materials"; }
		std::string GetCategory() const override { return FeatureCategories::kMaterials; }
		std::string GetFeatureSummary() const override { return "Parallax occlusion mapping for objects and terrain."; }

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		void WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB&) override;

		using Settings = extended_materials::Settings;

	private:
		ExtendedMaterials() = default;

		bool SaveSettings() override;
		[[nodiscard]] bool ShadowsActive() const noexcept;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(extended_materials::kSchema, _settings); }

		Settings _settings;
		std::atomic_bool _registrationsReady{ false };
		std::atomic_bool _injectionsOperational{ false };
	};
}

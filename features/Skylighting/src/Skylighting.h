#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "SkylightingSettings.h"

#include <string>
#include <string_view>

namespace cs::features
{
	class Skylighting : public Feature
	{
	public:
		using Settings = skylighting::Settings;

		static Skylighting* GetSingleton();

		std::string_view GetName() const override { return "Skylighting"; }
		std::string_view GetDisplayName() const override { return "Skylighting"; }
		std::string GetCategory() const override { return FeatureCategories::kLighting; }
		std::string GetFeatureSummary() const override
		{
			return "Simulates realistic ambient lighting by calculating sky occlusion and directional lighting, providing more accurate and natural illumination in outdoor environments.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;
		bool HasResettableSettings() const override { return true; }

	private:
		Skylighting() = default;

		bool SaveSettings() override;
		settings::SchemaView GetSettingsSchema() const override { return settings::MakeSchemaView(skylighting::kSchema); }

		Settings _settings;
	};
}

#pragma once

#include "Feature.h"
#include "FeatureCategories.h"

#include <atomic>
#include <string>
#include <string_view>

namespace cs::features
{
	class ShaderReplacement : public Feature
	{
	public:
		static ShaderReplacement* GetSingleton();

		std::string_view GetName() const override { return "ShaderReplacement"; }
		std::string GetFeatureSummary() const override { return "Lets developers swap in modified compiled shaders without restarting."; }
		std::string GetCategory() const override { return FeatureCategories::kDevTools; }
		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void DrawSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;

		struct PerShaderToggle
		{
			bool ambient_ibl_pass                  = false;
			bool bsdf_light_deferred_directional   = false;
			bool bsdf_light_deferred_directional_ibl = false;
			bool bsdf_light_deferred_point         = false;
			bool deferred_composite                = false;
			bool deferred_prepass                  = false;
			bool vls_slice_scatter                 = false;
		};

		struct Settings
		{
			bool             enabled = false;
			PerShaderToggle  shaders{};
			std::string      manifestPath = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\ShaderReplacement.json";
			std::string      shadersRoot  = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\Shaders";
		};

	private:
		ShaderReplacement() = default;

		void SaveSettings();
		void ApplyMarkerOverrides();
		bool IsShaderEnabled(const std::string& a_name) const noexcept;

		Settings           _settings;
		std::atomic<bool>  _started{ false };
		bool               _developerForceOffActive = false;
	};
}

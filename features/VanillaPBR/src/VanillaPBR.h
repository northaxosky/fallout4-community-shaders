#pragma once

#include "BlurMatch.h"
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
		std::string GetFeatureSummary() const override { return "GGX direct specular and live environment reflections for vanilla materials."; }
		bool Configure(const toml::table&, std::string&) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter*, ID3D11Device*) override;
		void Prepass() override;
		void OnEnabledChanged(bool a_enabled) noexcept override;
		void WriteSharedData(render::SharedFeatureDataCB&, render::FO4SharedDataCB&) override;
		void DrawSettings() override;
		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(vanilla_pbr::kSchema, _settings); }
		void RestoreDefaultSettings() override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view) noexcept override;
		FullscreenDebugData GetFullscreenDebugData() const noexcept override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(telemetry::Sink&) const override;

	private:
		bool ValidateInjectionPrerequisites(std::string&) override;
		vanilla_pbr::Settings _settings;
		std::atomic_uint32_t _debugMode{ 0 };
		std::atomic_bool _directEnabled{ false }, _liveEnvironmentEnabled{ false }, _liveEnvironmentReady{ false };
		vanilla_pbr::BlurMatch _blurMatch;
		std::atomic_bool _blurMatchCreated{ false };
		// Set off the render thread; Prepass releases the adopted cubes.
		std::atomic_bool _releaseBlurMatch{ false };
		// Render thread: composite draws capture the authored cubes only while live.
		bool _blurMatchActive = false;
	};
}

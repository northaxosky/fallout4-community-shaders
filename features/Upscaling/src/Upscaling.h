#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "Render/TemporalRenderSettings.h"
#include "ShaderDefines.h"

#include <d3d11.h>

#include <optional>

#include <winrt/base.h>

namespace cs::features
{
	class Upscaling : public Feature
	{
	public:
		using UpscaleMethod = render::temporal::UpscaleMethod;
		using Settings = render::temporal::UpscalingSettings;

		static Upscaling* GetSingleton();

		std::string_view GetName() const override { return "Upscaling"; }
		std::string_view GetDisplayName() const override { return "Upscaling"; }
		bool SupportsEnableToggle() const override { return false; }
		bool FollowsShaderOwnership() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kDisplay; }
		std::string GetFeatureSummary() const override
		{
			return "DLSS, FSR 3, and FSR 4 super resolution with independently selected frame generation.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		bool ValidateShaderInjections(std::string& a_error) override;
		void DrawSettings() override;
		bool HasDebugUI() const override { return true; }
		void DrawDebug() override;
		std::vector<std::string_view> GetRestartSettings() const override;
		void RestoreDefaultSettings() override;
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		void OnRuntimeQuarantined(std::string_view a_phase, std::string_view a_reason) noexcept override;

		Settings settings;

		// The Z-prepass alpha test while the main-view mip bias is active, else null.
		[[nodiscard]] ID3D11PixelShader* GetZPrepassAlphaPS() const noexcept;

	private:
		Upscaling() = default;
		engine::OwnedShaderDefineProvider _mipBiasDefines{ upscaling::kMipBiasShaderDefines, *this };
		engine::OwnedShaderDefineProvider _imageSpaceUvDefines{ upscaling::kImageSpaceUvShaderDefines, *this };
		winrt::com_ptr<ID3D11PixelShader> _zPrepassAlphaPS;
		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(render::temporal::kSchema, settings); }
		Settings _bootSettings;
	};
}

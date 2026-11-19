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
		settings::LiveSettingsAccess* GetLiveSettings() override { return &_live; }
		bool ProducesTelemetry() const override { return true; }
		void CollectTelemetry(cs::telemetry::Sink& a_sink) const override;
		std::span<const FeatureDebugView> GetDebugViews() const noexcept override;
		void SetDebugView(std::string_view a_view) noexcept override;
		void OnRuntimeQuarantined(std::string_view a_phase, std::string_view a_reason) noexcept override;

	private:
		// Swaps whole, so the derived enabled flag travels with the method.
		settings::LiveSettings<render::temporal::kSchema> _live{ { .normalize = [](Settings& a_settings) { a_settings.enabled = a_settings.upscaleMethod != static_cast<std::uint32_t>(UpscaleMethod::kNone); },
			.finalize = [this](const Settings&) { SubmitConfiguration(); } } };

	public:
		// Restart-only fields hold their boot values.
		const Settings& settings = _live.Get();

		// The Z-prepass alpha test while the main-view mip bias is active, else null.
		[[nodiscard]] ID3D11PixelShader* GetZPrepassAlphaPS() const noexcept;

	private:
		Upscaling() = default;
		void SubmitConfiguration();
		engine::OwnedShaderDefineProvider _mipBiasDefines{ upscaling::kMipBiasShaderDefines, *this };
		engine::OwnedShaderDefineProvider _imageSpaceUvDefines{ upscaling::kImageSpaceUvShaderDefines, *this };
		winrt::com_ptr<ID3D11PixelShader> _zPrepassAlphaPS;
	};
}

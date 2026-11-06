#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "HDRDisplaySettings.h"
#include "Render/FullscreenPass.h"
#include "Render/HdrCompose.h"
#include "ShaderDefines.h"

#include <Windows.h>
#include <d3d11.h>

#include <atomic>

#include <winrt/base.h>

namespace cs::features
{
	// Presents through an HDR10 container; the scene is HDR from the tonemap on.
	class HDRDisplay : public ShaderFeature<hdr_display_shader::kShaderDefines>
	{
	public:
		using Settings = hdr_display::Settings;

		static HDRDisplay* GetSingleton();

		std::string_view GetName() const override { return "HDRDisplay"; }
		std::string_view GetDisplayName() const override { return "HDR Display"; }
		bool SupportsEnableToggle() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kDisplay; }
		std::string GetFeatureSummary() const override
		{
			return "HDR10 output for HDR displays with configurable paper white and peak brightness.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void WriteSharedData(render::SharedFeatureDataCB& a_feature, render::FO4SharedDataCB& a_fo4) override;
		void WriteSharedFrameData(render::SharedDataCB& a_shared) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;

		// Presenter state for this present; the interface scale applies only after a seeded frame.
		[[nodiscard]] render::hdr::OutputSettings ConsumeOutputSettings() noexcept;

		// First run only: HDR starts on when Windows HDR is on for the game's monitor.
		void SeedFromDisplay(HWND a_window);

		// True while the frame renders HDR content: enabled, with a container to present it.
		[[nodiscard]] bool IsHdrActive() const noexcept;

	private:
		HDRDisplay() = default;

		bool SaveSettings() override;
		std::string_view GetRegistrationFailureMessage() const override { return "HDR Display could not register its imagespace shader define."; }
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(hdr_display::kSchema, _settings); }

		// After the imagespace chain, before the UI.
		void OnPostImagespace();
		// Scales the scene in linear space so the UI, drawn in place, sits at paper white.
		void SeedInterfaceBrightness(ID3D11DeviceContext& a_context);
		// ReShade runs on the HDR scene as scRGB, before the UI.
		void RenderReShade(ID3D11DeviceContext& a_context);

		Settings _settings;
		std::atomic_bool _displayKnown{ false };
		std::atomic_bool _displaySupportsHdr{ false };
		std::atomic_bool _windowsHdrOn{ false };

		render::FullscreenPass _seedPass;
		winrt::com_ptr<ID3D11PixelShader> _seedPS;
		winrt::com_ptr<ID3D11BlendState> _seedBlend;
		winrt::com_ptr<ID3D11Buffer> _seedConstants;
		std::atomic<float> _seededInterface{ 1.0f };
	};
}

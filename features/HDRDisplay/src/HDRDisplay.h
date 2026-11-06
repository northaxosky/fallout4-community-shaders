#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "HDRDisplaySettings.h"
#include "Render/HdrCompose.h"

#include <Windows.h>

#include <atomic>

namespace cs::features
{
	// Presents through an HDR10 container; the scene is still SDR content at paper white.
	class HDRDisplay : public Feature
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
		void OnD3D11Ready(IDXGIAdapter* a_adapter, ID3D11Device* a_device) override;
		void WriteSharedFrameData(render::SharedDataCB& a_shared) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;

		// The state the presenter encodes with; read on the render thread.
		[[nodiscard]] render::hdr::OutputSettings GetOutputSettings() const noexcept;

		// First run only: HDR starts on when Windows HDR is on for the game's monitor.
		void SeedFromDisplay(HWND a_window);

	private:
		HDRDisplay() = default;

		bool SaveSettings() override;
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(hdr_display::kSchema, _settings); }

		Settings _settings;
		std::atomic_bool _displayKnown{ false };
		std::atomic_bool _displaySupportsHdr{ false };
		std::atomic_bool _windowsHdrOn{ false };
	};
}

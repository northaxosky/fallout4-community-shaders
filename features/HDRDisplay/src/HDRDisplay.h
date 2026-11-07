#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "HDRDisplaySettings.h"
#include "Render/Engine.h"
#include "Render/HdrCompose.h"
#include "ShaderDefines.h"

#include <Windows.h>
#include <d3d11.h>

#include <atomic>

namespace cs::features
{
	// Presents through an HDR10 container; the scene is HDR from the tonemap on. The engine
	// renders the scene into a separate texture, so the interface lands alone in the proxy.
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
		void WriteSharedFrameData(render::SharedDataCB& a_shared) override;
		void DrawSettings() override;
		void RestoreDefaultSettings() override;

		// Presenter state for this present; the frame is separated once the scene was redirected.
		[[nodiscard]] render::hdr::OutputSettings ConsumeOutputSettings() noexcept;

		// First run only: HDR starts on when Windows HDR is on for the game's monitor.
		void SeedFromDisplay(HWND a_window);

		// True while the frame renders HDR content: enabled, with a container to present it.
		[[nodiscard]] bool IsHdrActive() const noexcept;

		[[nodiscard]] float GetPaperWhite() const noexcept { return static_cast<float>(_settings.paperWhite); }

	private:
		HDRDisplay() = default;

		bool SaveSettings() override;
		std::string_view GetRegistrationFailureMessage() const override { return "HDR Display could not register its imagespace shader define."; }
		settings::SettingsBinding GetSettingsBinding() const override { return settings::BindSettings(hdr_display::kSchema, _settings); }

		// Start of Render_UI: the interface layer redirects the frame target to the scene texture.
		void OnPreImagespace();
		// The scene is final: effects run on it, then the interface layer takes over the proxy.
		// Idempotent; pause frames reach it afterwards.
		void SeparateInterface();

		Settings _settings;
		std::atomic_bool _displayKnown{ false };
		std::atomic_bool _displaySupportsHdr{ false };
		std::atomic_bool _windowsHdrOn{ false };
	};
}

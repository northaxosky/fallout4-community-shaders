#pragma once

#include "Feature.h"
#include "FeatureCategories.h"
#include "HDRCapture.h"
#include "HDRDisplaySettings.h"
#include "Render/Engine.h"
#include "Render/HdrCompose.h"
#include "ShaderDefines.h"

#include <Windows.h>
#include <d3d11.h>

#include <atomic>

namespace cs::features
{
	// Presents through an HDR10 container; the interface lands alone in the proxy.
	class HDRDisplay : public ShaderFeature<hdr_display_shader::kShaderDefines>
	{
	public:
		using Settings = hdr_display::Settings;

		static HDRDisplay* GetSingleton();

		std::string_view GetName() const override { return "HDRDisplay"; }
		std::string_view GetDisplayName() const override { return "HDR Display"; }
		bool SupportsEnableToggle() const override { return false; }
		bool FollowsShaderOwnership() const override { return false; }
		std::string GetCategory() const override { return FeatureCategories::kDisplay; }
		std::string GetFeatureSummary() const override
		{
			return "HDR10 output for HDR displays with configurable paper white and peak brightness.";
		}

		bool Configure(const toml::table& a_config, std::string& a_error) override;
		void Load() override;
		void WriteSharedFrameData(render::SharedDataCB& a_shared) override;
		void DrawSettings() override;
		settings::LiveSettingsAccess* GetLiveSettings() override { return &_live; }

		[[nodiscard]] render::hdr::OutputSettings GetOutputSettings() const noexcept;

		// First run only: HDR starts on when Windows HDR is on for the game's monitor.
		void SeedFromDisplay(HWND a_window);

		// Windows HDR was on at launch; the HDR container needs it.
		[[nodiscard]] bool IsWindowsHdrOn() const noexcept { return _windowsHdrOn.load(std::memory_order_acquire); }

		// True while the frame renders HDR: enabled, with a container to present it.
		[[nodiscard]] bool IsHdrActive() const noexcept;

		// The split frame as PQ (a_hdr) or gamma 2.2; null when not split.
		[[nodiscard]] const buffer::Texture2D* ComposeCapture(bool a_hdr, bool a_interface);

	private:
		HDRDisplay() = default;

		std::string_view GetRegistrationFailureMessage() const override { return "HDR Display could not register its imagespace shader define."; }

		// Start of Render_UI: the interface layer redirects the target to the scene.
		void OnPreImagespace();
		// The scene is final; the interface takes the proxy. Idempotent for pauses.
		void SeparateInterface();

		settings::LiveSettings<hdr_display::kSchema> _live{ { .normalize = [](Settings& a_settings) {
																  // Paper white stays below peak, as upstream clamps it.
																  a_settings.paperWhite = std::min(a_settings.paperWhite, a_settings.peakNits - 1); },
			// Defaults re-seed from the display, like upstream.
			.defaults = [this] {
				Settings defaults;
				defaults.enableHDR = _windowsHdrOn.load(std::memory_order_acquire);
				defaults.autoDetected = _settings.autoDetected;
				return defaults; } } };
		const Settings& _settings = _live.Get();
		hdr_display::CaptureComposer _capture;
		std::atomic_bool _displayKnown{ false };
		std::atomic_bool _displaySupportsHdr{ false };
		std::atomic_bool _windowsHdrOn{ false };
	};
}

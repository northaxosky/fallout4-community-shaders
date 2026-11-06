#include "HDRDisplay.h"

#include <algorithm>

#include <DearModdingUI/Client.h>

#include "HDRDetection.h"
#include "Log.h"
#include "Menu/Section.h"
#include "Menu/SettingsEdit.h"
#include "Render/SharedDataLayout.h"
#include "Render/TemporalPipeline.h"
#include "Settings/SettingsPersistence.h"
#include "World/Sky.h"

namespace cs::features
{
	namespace
	{
		auto* L = cs::log::Get("cs.feature.hdrdisplay");

		// SharedData::HDRData.w: how the frame is framed for the tonemap.
		constexpr float kMenuSceneGameplay = 0.0f;
		constexpr float kMenuScenePaused = 0.58f;
		constexpr float kMenuSceneMainOrLoading = 1.0f;
	}

	HDRDisplay* HDRDisplay::GetSingleton()
	{
		static HDRDisplay instance;
		return &instance;
	}

	bool HDRDisplay::Configure(const toml::table& a_config, std::string& a_error)
	{
		auto candidate = _settings;
		if (!settings::Parse(hdr_display::kSchema, a_config, candidate, a_error))
			return false;
		// Paper white stays below peak, as upstream clamps on load.
		candidate.paperWhite = std::min(candidate.paperWhite, candidate.peakNits - 1);
		_settings = candidate;
		_liveSettings = settings::BindLiveSettings(hdr_display::kSchema, _settings, [this] {
			_settings.paperWhite = std::min(_settings.paperWhite, _settings.peakNits - 1);
		});
		return true;
	}

	bool HDRDisplay::SaveSettings()
	{
		return settings::SaveDelta(hdr_display::kSchema, GetConfigKey(), _settings, *L);
	}

	void HDRDisplay::RestoreDefaultSettings()
	{
		const bool autoDetected = _settings.autoDetected;
		_settings = Settings{};
		// Defaults re-seed from the display, like upstream.
		_settings.enableHDR = _windowsHdrOn.load(std::memory_order_acquire);
		_settings.autoDetected = autoDetected;
		SaveSettings();
	}

	void HDRDisplay::SeedFromDisplay(HWND a_window)
	{
		const auto capability = hdr_display::QueryDisplayCapability(a_window);
		if (!capability)
			return;
		L->info("HDR display detection: supported={}, enabled={}", capability->hdrSupported, capability->hdrEnabled);
		_displaySupportsHdr.store(capability->hdrSupported, std::memory_order_release);
		_windowsHdrOn.store(capability->hdrEnabled, std::memory_order_release);
		_displayKnown.store(true, std::memory_order_release);
		if (_settings.autoDetected)
			return;
		_settings.enableHDR = capability->hdrEnabled;
		_settings.autoDetected = true;
		SaveSettings();
	}

	render::hdr::OutputSettings HDRDisplay::GetOutputSettings() const noexcept
	{
		return {
			.enabled = _settings.enableHDR,
			.paperWhite = static_cast<float>(_settings.paperWhite),
			.peakNits = static_cast<float>(_settings.peakNits),
			.uiBrightness = _settings.uiBrightness
		};
	}

	void HDRDisplay::WriteSharedFrameData(render::SharedDataCB& a_shared)
	{
		const auto* ui = RE::UI::GetSingleton();
		float menuScene = kMenuSceneGameplay;
		if (engine::IsMainOrLoadingMenuOpen())
			menuScene = kMenuSceneMainOrLoading;
		else if (ui && ui->GameIsPaused())
			menuScene = kMenuScenePaused;
		const bool active = _settings.enableHDR && render::TemporalPipeline::Get().IsHdrContainerActive();
		a_shared.HDRData = {
			active ? 1.0f : 0.0f,
			static_cast<float>(_settings.paperWhite),
			static_cast<float>(_settings.peakNits),
			menuScene
		};
	}

	void HDRDisplay::DrawSettings()
	{
		settings::SettingsEdit edit{ *this };
		const auto field = edit.Bind(hdr_display::kSchema, _settings);
		constexpr auto clamped = dmui::ui::SliderFlags::kAlwaysClamp;

		if (const ui::Section section{ "hdr-display-output", "Output" }; section) {
			if (!render::TemporalPipeline::Get().IsHdrContainerActive())
				dmui::ui::TextDisabled("HDR output is inactive; it needs borderless windowed mode.");
			else if (_displayKnown.load(std::memory_order_acquire))
				dmui::ui::TextDisabled(_windowsHdrOn.load(std::memory_order_acquire)       ? "Windows HDR is on." :
									   _displaySupportsHdr.load(std::memory_order_acquire) ? "Windows HDR is off for this display." :
																							 "This display does not report HDR.");
			field(&Settings::enableHDR);
			field(&Settings::paperWhite, { .format = "%d nits", .flags = clamped });
			field(&Settings::peakNits, { .format = "%d nits", .flags = clamped });
			field(&Settings::uiBrightness, { .format = "%.2f", .flags = clamped });
		}
	}

	namespace
	{
		struct AutoRegister
		{
			AutoRegister() { FeatureManager::Get().Register(HDRDisplay::GetSingleton()); }
		};
		static AutoRegister _autoRegister;
	}
}

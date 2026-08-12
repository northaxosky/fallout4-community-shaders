#include "Imagespace.h"
#include "ImagespaceInternal.h"

#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <toml++/toml.hpp>
#include <type_traits>
#include <vector>

#include <DirectXMath.h>

#include "Render/ComputeScope.h"
#include "Render/RendererContext.h"
#include "Render/Engine.h"
#include "Utils/CSUtil.h"
#include "Env.h"
#include "ImagespaceConfigIO.h"
#include "Log.h"
#include "Menu/Menu.h"
#include "Settings/FeatureConfig.h"
#include "Settings/PresetManager.h"
#include "Render/RenderHooks.h"
#include "Settings/SettingsOverrideManager.h"
#include "World/Sky.h"
#include "World/Weather.h"
#include "WeatherProfiles.h"


namespace cs::features
{
	namespace { auto* L = cs::log::Get("cs.feature.imagespace"); }

	constexpr const char* kOpMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_operator";
	constexpr const char* kLutMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_lut";
	constexpr const char* kAdaptMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_adaptive_exposure";
	constexpr const char* kBloomMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_bloom";
	constexpr const char* kVignMarker    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_vignette";
	constexpr const char* kCAMarker      = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_ca";
	constexpr const char* kSharpenMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_sharpen";
	constexpr const char* kDofMarker     = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_dof";
	constexpr const char* kStyleMarker   = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_style";
	constexpr const char* kWeatherCatMarker    = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_weather_category";
	constexpr const char* kWeatherFormIDMarker = "Data\\F4SE\\Plugins\\FO4CommunityShaders\\.imagespace_force_weather_formid";

	bool Imagespace::Configure(const toml::table& a_config, std::string& a_error)
	{
		Settings settingsCandidate{};
		imagespace::WeatherProfiles weatherCandidate{};
		if (!imagespace::ParseSettingsStrict(a_config, settingsCandidate, a_error)
			|| !imagespace::ParseWeatherStrict(a_config, weatherCandidate, a_error)) {
			return false;
		}

		auto overrideResult = cs::settings_overrides::Load("Imagespace");
		switch (overrideResult.status) {
		case feature_config::FileLoadStatus::kMissing:
			break;
		case feature_config::FileLoadStatus::kParsed:
			if (!imagespace::ParseSettingsStrict(overrideResult.table, settingsCandidate, a_error)) {
				a_error = "override " + a_error;
				return false;
			}
			break;
		case feature_config::FileLoadStatus::kParseError:
		case feature_config::FileLoadStatus::kIoError:
			a_error = std::move(overrideResult.error);
			return false;
		}

		settings = std::move(settingsCandidate);
		weatherProfiles = std::move(weatherCandidate);
		return true;
	}

	void Imagespace::LoadSettings()
	{
		const auto reload = feature_config::Reload();
		if (!reload.defaultLoaded) {
			L->warn("Failed to reload unified configuration: {}; keeping current settings", reload.defaultError);
			return;
		}
		if (!reload.userWarning.empty()) {
			L->warn("Ignoring unified user configuration during reload: {}", reload.userWarning);
		}
		const auto config = feature_config::GetFeature(GetConfigKey());
		if (!config) {
			L->warn("Unified configuration has no {} feature; keeping current settings", GetConfigKey());
			return;
		}

		std::string error;
		if (!Configure(*config, error)) {
			L->warn("Failed to reload settings: {}; keeping current settings", error);
			return;
		}

		lutCache.Clear();
		if (cs::util::GetD3DDevice() != nullptr) {
			ApplyLUTState();
		}
		ApplySmokeMarkers();
	}

	void Imagespace::ApplySmokeMarkers()
	{
		char op_c = 0, lut_c = 0, adapt_c = 0, bloom_c = 0, vig_c = 0, ca_c = 0, sharp_c = 0, dof_c = 0, style_c = 0;
		const bool opP     = cs::util::ReadMarker(kOpMarker,      op_c);
		const bool lutP    = cs::util::ReadMarker(kLutMarker,     lut_c);
		const bool adaptP  = cs::util::ReadMarker(kAdaptMarker,   adapt_c);
		const bool bloomP  = cs::util::ReadMarker(kBloomMarker,   bloom_c);
		const bool vigP    = cs::util::ReadMarker(kVignMarker,    vig_c);
		const bool caP     = cs::util::ReadMarker(kCAMarker,      ca_c);
		const bool sharpP  = cs::util::ReadMarker(kSharpenMarker, sharp_c);
		const bool dofP    = cs::util::ReadMarker(kDofMarker,     dof_c);
		const bool styleP  = cs::util::ReadMarker(kStyleMarker,   style_c);

		// Weather smoke tests force one full-strength category.
		forcedWeatherCategory.reset();
		forcedWeatherFormID.reset();
		char wcat_c = 0;
		if (cs::util::ReadMarker(kWeatherCatMarker, wcat_c) && wcat_c >= '0' && wcat_c <= '7') {
			forcedWeatherCategory = static_cast<imagespace::WeatherCategory>(wcat_c - '0');
			weatherProfiles.enablePerWeatherProfiles = true;
			L->info("Forced weather category: {}", imagespace::CategoryName(*forcedWeatherCategory));
		}
		// Remove parsed FormID markers to prevent leakage.
		try {
			std::ifstream f(kWeatherFormIDMarker, std::ios::binary);
			if (f.is_open()) {
				std::string line; std::getline(f, line);
				std::string_view view(line);
				if (view.size() >= 3 &&
					static_cast<unsigned char>(view[0]) == 0xEF &&
					static_cast<unsigned char>(view[1]) == 0xBB &&
					static_cast<unsigned char>(view[2]) == 0xBF)
				{
					view.remove_prefix(3);
				}
				const auto* begin = view.data();
				const auto* end   = begin + view.size();
				std::uint32_t formID = 0;
				int parseBase = 10;
				if (view.size() > 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) {
					begin += 2; parseBase = 16;
				}
				auto [ptr, ec] = std::from_chars(begin, end, formID, parseBase);
				if (ec == std::errc{} && ptr == end) {
					forcedWeatherFormID = formID;
					L->info("Forced weather formID: 0x{:08X}", formID);
					f.close();
					std::error_code rmEc;
					std::filesystem::remove(kWeatherFormIDMarker, rmEc);
				}
			}
		} catch (...) {}

		testModeActive = opP || lutP || adaptP || bloomP || vigP || caP || sharpP || dofP || styleP;

		if (testModeActive) {
			settings.enabled            = true;
			settings.tonemapOperator    = opP && (op_c >= '0' && op_c <= '3') ? (op_c - '0') : 0;
			settings.exposure           = 1.0f;
			settings.lutEnable          = lutP && (lut_c == '1');
			settings.lutStrength        = 1.0f;
			settings.adaptiveExposure   = adaptP && (adapt_c == '1');
			settings.adaptationSpeedUp  = 0.5f;
			settings.adaptationSpeedDown = 2.0f;
			settings.exposureKey        = 0.18f;
			settings.bloomEnable        = bloomP && (bloom_c == '1');
			settings.bloomIntensity     = settings.bloomEnable ? 0.15f : 0.05f;
			settings.vignetteEnable     = vigP && (vig_c == '1');
			settings.vignetteIntensity  = settings.vignetteEnable ? 0.6f : 0.3f;
			settings.caEnable           = caP && (ca_c == '1');
			settings.caIntensity        = settings.caEnable ? 1.5f : 0.5f;
			settings.sharpenEnable      = sharpP && (sharp_c == '1');
			settings.sharpness          = settings.sharpenEnable ? 0.8f : 0.4f;
			settings.dofEnable          = dofP && (dof_c == '1' || dof_c == '2');
			if (dof_c == '1') {
				settings.aperture      = 0.05f;
				settings.focusDistance = 1500.0f;
				settings.focalLength   = 50.0f;
				settings.dofQuality    = 1;
			} else if (dof_c == '2') {
				settings.aperture      = 0.30f;
				settings.focusDistance = 500.0f;
				settings.focalLength   = 50.0f;
				settings.dofQuality    = 2;
			}
			// Smoke levels 1-4 force visible effects.
			if (styleP && style_c >= '1' && style_c <= '4') {
				ApplyStyle(static_cast<Style>(style_c - '0'));
				settings.bloomEnable     = true;
				settings.vignetteEnable  = true;
				settings.caEnable        = true;
				settings.sharpenEnable   = true;
				settings.lensFlareEnable = true;
			} else if (styleP && style_c == '0') {
				settings.style = static_cast<int>(Style::kCustom);
			}
			L->info("Test mode: op={} lut={} adapt={} bloom={} vig={} ca={} sharp={} dof={} style={}",
				settings.tonemapOperator, settings.lutEnable, settings.adaptiveExposure,
				settings.bloomEnable, settings.vignetteEnable, settings.caEnable, settings.sharpenEnable,
				settings.dofEnable, settings.style);
		}
	}

	void Imagespace::SaveSettings()
	{
		if (testModeActive)
			return;

		toml::table feature;
		imagespace::EmitSettings(feature, settings);
		imagespace::EmitWeather(feature, weatherProfiles, true);

		if (const auto result = feature_config::UpdateFeature(GetConfigKey(), feature); !result) {
			L->error("Failed to save settings: {}", result.error);
		}
	}

	bool Imagespace::StageFromPreset(const toml::table& a_subtable, const cs::PresetApplyContext& a_ctx, std::string& a_err)
	{
		stagedSettings        = Settings{};
		stagedWeatherProfiles = imagespace::WeatherProfiles{};
		imagespace::ParseSettings(a_subtable, stagedSettings);
		imagespace::ParseWeather(a_subtable, stagedWeatherProfiles, a_ctx.isBuiltin);

		// Built-ins must preserve user weather overrides.
		if (a_ctx.isBuiltin) {
			stagedWeatherProfiles.userOverrides = weatherProfiles.userOverrides;
		}
		stagedValid = true;
		a_err.clear();
		return true;
	}

	void Imagespace::CommitStagedSwap() noexcept
	{
		static_assert(std::is_nothrow_move_assignable_v<Settings>);
		static_assert(std::is_nothrow_move_assignable_v<imagespace::WeatherProfiles>);

		detail::AssertRenderThread("CommitStagedSwap");
		if (!stagedValid) return;
		settings        = std::move(stagedSettings);
		weatherProfiles = std::move(stagedWeatherProfiles);
		stagedValid     = false;
	}

	void Imagespace::CommitStagedFinalize()
	{
		SaveSettings();
		ApplyLUTState();
	}

	void Imagespace::ExportToPreset(toml::table& a_subtable)
	{
		imagespace::EmitSettings(a_subtable, settings);
		imagespace::EmitWeather(a_subtable, weatherProfiles, true);
	}

}

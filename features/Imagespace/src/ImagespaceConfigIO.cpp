#include "ImagespaceConfigIO.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "TomlUtil.h"

namespace cs::features::imagespace
{
	namespace
	{
		constexpr std::array<std::pair<std::string_view, WeatherCategory>,
			static_cast<std::size_t>(WeatherCategory::kCount)> kCatTables = { {
				{ "clear",    WeatherCategory::kClear    },
				{ "overcast", WeatherCategory::kOvercast },
				{ "fog",      WeatherCategory::kFog      },
				{ "rain",     WeatherCategory::kRain     },
				{ "radstorm", WeatherCategory::kRadstorm },
				{ "snow",     WeatherCategory::kSnow     },
				{ "interior", WeatherCategory::kInterior },
				{ "unknown",  WeatherCategory::kUnknown  },
			} };

		constexpr std::string_view kSettingsCtx = "imagespace.settings";
	}

	void ParseSettings(const toml::table& a_root, Imagespace::Settings& s)
	{
		const auto* sub = a_root["settings"].as_table();
		if (!sub) return;
		const toml::table& settings = *sub;

		auto readInt = [&settings](const char* a_key, int a_current, int a_min, int a_max) {
			return cs::toml_util::ReadInt(settings, a_key, a_current, a_min, a_max, kSettingsCtx);
		};
		auto readFloat = [&settings](const char* a_key, float a_current, float a_min, float a_max) {
			return cs::toml_util::ReadFloat(settings, a_key, a_current, a_min, a_max, kSettingsCtx);
		};
		auto readBool = [&settings](const char* a_key, bool a_current) {
			return cs::toml_util::ReadBool(settings, a_key, a_current, kSettingsCtx);
		};
		auto readString = [&settings](const char* a_key, const std::string& a_current) {
			return cs::toml_util::ReadString(settings, a_key, a_current, kSettingsCtx);
		};

		s.enabled           = readBool("enabled", s.enabled);
		s.style             = readInt("style", s.style, 0, 4);
		s.forceWithENB      = readBool("force_with_enb", s.forceWithENB);
		s.tonemapOperator   = readInt("tonemap_operator", s.tonemapOperator, 0, 3);
		s.exposure          = readFloat("exposure", s.exposure, 0.25f, 4.0f);
		s.lutEnable         = readBool("lut_enable", s.lutEnable);
		s.lutPath           = readString("lut_path", s.lutPath);
		s.lutStrength       = readFloat("lut_strength", s.lutStrength, 0.0f, 1.0f);

		s.adaptiveExposure    = readBool("adaptive_exposure", s.adaptiveExposure);
		s.adaptationSpeedUp   = readFloat("adaptation_speed_up", s.adaptationSpeedUp, 0.05f, 10.0f);
		s.adaptationSpeedDown = readFloat("adaptation_speed_down", s.adaptationSpeedDown, 0.05f, 30.0f);
		s.exposureKey         = readFloat("exposure_key", s.exposureKey, 0.05f, 0.5f);
		s.exposureMin         = readFloat("exposure_min", s.exposureMin, 0.005f, 0.5f);
		s.exposureMax         = readFloat("exposure_max", s.exposureMax, 1.0f, 16.0f);

		s.bloomEnable    = readBool("bloom_enable", s.bloomEnable);
		s.bloomThreshold = readFloat("bloom_threshold", s.bloomThreshold, 0.0f, 2.0f);
		s.bloomIntensity = readFloat("bloom_intensity", s.bloomIntensity, 0.0f, 0.3f);
		s.bloomMips      = readInt("bloom_mips", s.bloomMips, 3, 6);
		if (const auto* weights = settings["bloom_mip_weights"].as_array();
			weights && weights->size() == std::size(s.bloomMipWeights)) {
			bool valid = true;
			for (std::size_t i = 0; i < std::size(s.bloomMipWeights); ++i) {
				if (!(*weights)[i].value<double>()) { valid = false; break; }
			}
			if (valid) {
				for (std::size_t i = 0; i < std::size(s.bloomMipWeights); ++i) {
					const auto weight = (*weights)[i].value<double>();
					s.bloomMipWeights[i] = std::clamp(static_cast<float>(*weight), 0.0f, 4.0f);
				}
			}
		}

		s.vignetteEnable    = readBool("vignette_enable", s.vignetteEnable);
		s.vignetteIntensity = readFloat("vignette_intensity", s.vignetteIntensity, 0.0f, 1.0f);
		s.caEnable          = readBool("ca_enable", s.caEnable);
		s.caIntensity       = readFloat("ca_intensity", s.caIntensity, 0.0f, 2.0f);
		s.sharpenEnable     = readBool("sharpen_enable", s.sharpenEnable);
		s.sharpness         = readFloat("sharpness", s.sharpness, 0.0f, 1.0f);

		s.sunspriteEnable    = readBool("sunsprite_enable", s.sunspriteEnable);
		s.sunspriteIntensity = readFloat("sunsprite_intensity", s.sunspriteIntensity, 0.0f, 2.0f);
		s.sunspriteSize      = readFloat("sunsprite_size", s.sunspriteSize, 0.01f, 0.2f);
		s.lensFlareEnable    = readBool("lens_flare_enable", s.lensFlareEnable);
		s.lensFlareIntensity = readFloat("lens_flare_intensity", s.lensFlareIntensity, 0.0f, 2.0f);
		s.lensFlareGhosts    = readInt("lens_flare_ghosts", s.lensFlareGhosts, 3, 7);
		s.dirtEnable         = readBool("dirt_enable", s.dirtEnable);
		s.dirtIntensity      = readFloat("dirt_intensity", s.dirtIntensity, 0.0f, 2.0f);

		s.dofEnable      = readBool("dof_enable", s.dofEnable);
		s.aperture       = readFloat("aperture", s.aperture, 0.0f, 0.5f);
		s.focusDistance  = readFloat("focus_distance", s.focusDistance, 10.0f, 100000.0f);
		s.focalLength    = readFloat("focal_length", s.focalLength, 1.0f, 200.0f);
		s.focusRange     = readFloat("focus_range", s.focusRange, 10.0f, 10000.0f);
		s.dofQuality     = readInt("dof_quality", s.dofQuality, 0, 2);
		s.cocLimitFactor = readFloat("coc_limit_factor", s.cocLimitFactor, 0.005f, 0.10f);
		s.bokehIntensity = readFloat("bokeh_intensity", s.bokehIntensity, 0.0f, 1.0f);
		s.anamorphRatio  = readFloat("anamorph_ratio", s.anamorphRatio, 0.25f, 4.0f);
	}

	void ParseWeather(const toml::table& a_root, WeatherProfiles& wp, bool a_dropOverrides)
	{
		const auto* weatherNode = a_root["weather"].as_table();
		if (!weatherNode) return;

		wp.enablePerWeatherProfiles = (*weatherNode)["enable_per_weather_profiles"].value_or(false);

		for (const auto& [name, cat] : kCatTables) {
			const auto* sub = (*weatherNode)[name].as_table();
			if (!sub) continue;
			auto& ov = wp.overlays[static_cast<std::size_t>(cat)];
			auto readOptF = [&sub](const char* k, float lo, float hi) -> std::optional<float> {
				if (auto v = (*sub)[k].value<double>()) return std::clamp(static_cast<float>(*v), lo, hi);
				return std::nullopt;
			};
			auto readOptI = [&sub](const char* k, int lo, int hi) -> std::optional<int> {
				if (auto v = (*sub)[k].value<std::int64_t>()) return static_cast<int>(std::clamp(*v, static_cast<std::int64_t>(lo), static_cast<std::int64_t>(hi)));
				return std::nullopt;
			};
			auto readOptB = [&sub](const char* k) -> std::optional<bool> {
				if (auto v = (*sub)[k].value<bool>()) return *v;
				return std::nullopt;
			};
			auto readOptS = [&sub](const char* k) -> std::optional<std::string> {
				if (auto v = (*sub)[k].value<std::string>()) return *v;
				return std::nullopt;
			};
			ov.exposure           = readOptF("exposure",           0.25f, 4.0f);
			ov.lutEnable          = readOptB("lut_enable");
			ov.lutPath            = readOptS("lut_path");
			ov.lutStrength        = readOptF("lut_strength",       0.0f, 1.0f);
			ov.bloomEnable        = readOptB("bloom_enable");
			ov.bloomThreshold     = readOptF("bloom_threshold",    0.0f, 2.0f);
			ov.bloomIntensity     = readOptF("bloom_intensity",    0.0f, 0.3f);
			if (const auto* w = (*sub)["bloom_mip_weights"].as_array(); w && w->size() == 6) {
				std::array<float, 6> arr{};
				bool valid = true;
				for (std::size_t i = 0; i < 6 && valid; ++i) {
					if (auto v = (*w)[i].value<double>()) arr[i] = std::clamp(static_cast<float>(*v), 0.0f, 4.0f);
					else valid = false;
				}
				if (valid) ov.bloomMipWeights = arr;
			}
			ov.vignetteEnable     = readOptB("vignette_enable");
			ov.vignetteIntensity  = readOptF("vignette_intensity", 0.0f, 1.0f);
			ov.caEnable           = readOptB("ca_enable");
			ov.caIntensity        = readOptF("ca_intensity",       0.0f, 2.0f);
			ov.sunspriteIntensity = readOptF("sunsprite_intensity", 0.0f, 2.0f);
			ov.sunspriteSize      = readOptF("sunsprite_size",     0.01f, 0.2f);
			ov.lensFlareEnable    = readOptB("lens_flare_enable");
			ov.lensFlareIntensity = readOptF("lens_flare_intensity", 0.0f, 2.0f);
			ov.lensFlareGhosts    = readOptI("lens_flare_ghosts",  3, 7);
			ov.dirtEnable         = readOptB("dirt_enable");
			ov.dirtIntensity      = readOptF("dirt_intensity",     0.0f, 2.0f);
		}

		if (!a_dropOverrides) {
			if (const auto* ovTbl = (*weatherNode)["overrides"].as_table()) {
				for (const auto& [key, val] : *ovTbl) {
					const std::string keyStr(key.str());
					std::uint32_t formID = 0;
					const auto* begin = keyStr.c_str();
					const auto* end   = begin + keyStr.size();
					int base = 10;
					if (keyStr.size() > 2 && keyStr[0] == '0' && (keyStr[1] == 'x' || keyStr[1] == 'X')) {
						begin += 2;
						base = 16;
					}
					auto [ptr, ec] = std::from_chars(begin, end, formID, base);
					if (ec != std::errc{} || ptr != end) continue;
					if (auto v = val.value<std::string>()) {
						if (auto cat = ParseCategory(*v)) {
							wp.userOverrides.emplace(formID, *cat);
						}
					}
				}
			}
		}
	}

	void EmitSettings(toml::table& a_root, const Imagespace::Settings& settings)
	{
		auto& s = a_root.insert_or_assign("settings", toml::table{}).first->second.as_table()->ref<toml::table>();
		s.insert_or_assign("enabled", settings.enabled);
		s.insert_or_assign("style", static_cast<std::int64_t>(settings.style));
		s.insert_or_assign("force_with_enb", settings.forceWithENB);
		s.insert_or_assign("tonemap_operator", static_cast<std::int64_t>(settings.tonemapOperator));
		s.insert_or_assign("exposure", static_cast<double>(settings.exposure));
		s.insert_or_assign("lut_enable", settings.lutEnable);
		s.insert_or_assign("lut_path", settings.lutPath);
		s.insert_or_assign("lut_strength", static_cast<double>(settings.lutStrength));
		s.insert_or_assign("adaptive_exposure", settings.adaptiveExposure);
		s.insert_or_assign("adaptation_speed_up", static_cast<double>(settings.adaptationSpeedUp));
		s.insert_or_assign("adaptation_speed_down", static_cast<double>(settings.adaptationSpeedDown));
		s.insert_or_assign("exposure_key", static_cast<double>(settings.exposureKey));
		s.insert_or_assign("exposure_min", static_cast<double>(settings.exposureMin));
		s.insert_or_assign("exposure_max", static_cast<double>(settings.exposureMax));
		s.insert_or_assign("bloom_enable", settings.bloomEnable);
		s.insert_or_assign("bloom_threshold", static_cast<double>(settings.bloomThreshold));
		s.insert_or_assign("bloom_intensity", static_cast<double>(settings.bloomIntensity));
		s.insert_or_assign("bloom_mips", static_cast<std::int64_t>(settings.bloomMips));
		toml::array bloomMipWeights;
		for (const auto weight : settings.bloomMipWeights) {
			bloomMipWeights.push_back(static_cast<double>(weight));
		}
		s.insert_or_assign("bloom_mip_weights", std::move(bloomMipWeights));
		s.insert_or_assign("vignette_enable", settings.vignetteEnable);
		s.insert_or_assign("vignette_intensity", static_cast<double>(settings.vignetteIntensity));
		s.insert_or_assign("ca_enable", settings.caEnable);
		s.insert_or_assign("ca_intensity", static_cast<double>(settings.caIntensity));
		s.insert_or_assign("sharpen_enable", settings.sharpenEnable);
		s.insert_or_assign("sharpness", static_cast<double>(settings.sharpness));
		s.insert_or_assign("sunsprite_enable", settings.sunspriteEnable);
		s.insert_or_assign("sunsprite_intensity", static_cast<double>(settings.sunspriteIntensity));
		s.insert_or_assign("sunsprite_size", static_cast<double>(settings.sunspriteSize));
		s.insert_or_assign("lens_flare_enable", settings.lensFlareEnable);
		s.insert_or_assign("lens_flare_intensity", static_cast<double>(settings.lensFlareIntensity));
		s.insert_or_assign("lens_flare_ghosts", static_cast<std::int64_t>(settings.lensFlareGhosts));
		s.insert_or_assign("dirt_enable", settings.dirtEnable);
		s.insert_or_assign("dirt_intensity", static_cast<double>(settings.dirtIntensity));
		s.insert_or_assign("dof_enable", settings.dofEnable);
		s.insert_or_assign("aperture", static_cast<double>(settings.aperture));
		s.insert_or_assign("focus_distance", static_cast<double>(settings.focusDistance));
		s.insert_or_assign("focal_length", static_cast<double>(settings.focalLength));
		s.insert_or_assign("focus_range", static_cast<double>(settings.focusRange));
		s.insert_or_assign("dof_quality", static_cast<std::int64_t>(settings.dofQuality));
		s.insert_or_assign("coc_limit_factor", static_cast<double>(settings.cocLimitFactor));
		s.insert_or_assign("bokeh_intensity", static_cast<double>(settings.bokehIntensity));
		s.insert_or_assign("anamorph_ratio", static_cast<double>(settings.anamorphRatio));
	}

	void EmitWeather(toml::table& a_root, const WeatherProfiles& wp, bool a_includeOverrides)
	{
		auto& w = a_root.insert_or_assign("weather", toml::table{}).first->second.as_table()->ref<toml::table>();
		w.insert_or_assign("enable_per_weather_profiles", wp.enablePerWeatherProfiles);

		for (const auto& [name, cat] : kCatTables) {
			const auto& ov = wp.overlays[static_cast<std::size_t>(cat)];
			if (ov.SetKeyCount() == 0) continue;
			toml::table cat_tbl;
			if (ov.exposure)           cat_tbl.insert_or_assign("exposure",           static_cast<double>(*ov.exposure));
			if (ov.lutEnable)          cat_tbl.insert_or_assign("lut_enable",         *ov.lutEnable);
			if (ov.lutPath)            cat_tbl.insert_or_assign("lut_path",           *ov.lutPath);
			if (ov.lutStrength)        cat_tbl.insert_or_assign("lut_strength",       static_cast<double>(*ov.lutStrength));
			if (ov.bloomEnable)        cat_tbl.insert_or_assign("bloom_enable",       *ov.bloomEnable);
			if (ov.bloomThreshold)     cat_tbl.insert_or_assign("bloom_threshold",    static_cast<double>(*ov.bloomThreshold));
			if (ov.bloomIntensity)     cat_tbl.insert_or_assign("bloom_intensity",    static_cast<double>(*ov.bloomIntensity));
			if (ov.bloomMipWeights) {
				toml::array arr;
				for (const auto v : *ov.bloomMipWeights) arr.push_back(static_cast<double>(v));
				cat_tbl.insert_or_assign("bloom_mip_weights", std::move(arr));
			}
			if (ov.vignetteEnable)     cat_tbl.insert_or_assign("vignette_enable",    *ov.vignetteEnable);
			if (ov.vignetteIntensity)  cat_tbl.insert_or_assign("vignette_intensity", static_cast<double>(*ov.vignetteIntensity));
			if (ov.caEnable)           cat_tbl.insert_or_assign("ca_enable",          *ov.caEnable);
			if (ov.caIntensity)        cat_tbl.insert_or_assign("ca_intensity",       static_cast<double>(*ov.caIntensity));
			if (ov.sunspriteIntensity) cat_tbl.insert_or_assign("sunsprite_intensity", static_cast<double>(*ov.sunspriteIntensity));
			if (ov.sunspriteSize)      cat_tbl.insert_or_assign("sunsprite_size",     static_cast<double>(*ov.sunspriteSize));
			if (ov.lensFlareEnable)    cat_tbl.insert_or_assign("lens_flare_enable",  *ov.lensFlareEnable);
			if (ov.lensFlareIntensity) cat_tbl.insert_or_assign("lens_flare_intensity", static_cast<double>(*ov.lensFlareIntensity));
			if (ov.lensFlareGhosts)    cat_tbl.insert_or_assign("lens_flare_ghosts",  static_cast<std::int64_t>(*ov.lensFlareGhosts));
			if (ov.dirtEnable)         cat_tbl.insert_or_assign("dirt_enable",        *ov.dirtEnable);
			if (ov.dirtIntensity)      cat_tbl.insert_or_assign("dirt_intensity",     static_cast<double>(*ov.dirtIntensity));
			w.insert_or_assign(name, std::move(cat_tbl));
		}

		if (a_includeOverrides && !wp.userOverrides.empty()) {
			toml::table ov_tbl;
			for (const auto& [formID, cat] : wp.userOverrides) {
				char hex[12];
				std::snprintf(hex, sizeof(hex), "0x%08X", formID);
				ov_tbl.insert_or_assign(hex, std::string(CategoryName(cat)));
			}
			w.insert_or_assign("overrides", std::move(ov_tbl));
		}
	}
}

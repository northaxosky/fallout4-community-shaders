#pragma once

#include "Imagespace.h"
#include "WeatherProfiles.h"

#include <toml++/toml.hpp>

namespace cs::features::imagespace
{
	// Missing or unknown settings preserve current values.
	void ParseSettings(const toml::table& a_root, Imagespace::Settings& a_outSettings);

	// Built-ins ignore user weather overrides.
	void ParseWeather(const toml::table& a_root, WeatherProfiles& a_outProfiles, bool a_dropOverrides = false);

	bool ParseSettingsStrict(const toml::table& a_root, Imagespace::Settings& a_outSettings, std::string& a_error);
	bool ParseWeatherStrict(const toml::table& a_root, WeatherProfiles& a_outProfiles, std::string& a_error);

	void EmitSettings(toml::table& a_root, const Imagespace::Settings& a_settings);

	void EmitWeather(toml::table& a_root, const WeatherProfiles& a_profiles, bool a_includeOverrides);
}

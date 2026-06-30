#pragma once

#include "Imagespace.h"
#include "WeatherProfiles.h"

#include <toml++/toml.hpp>

namespace cs::features::imagespace
{
	// Parses [settings]; missing/unknown keys leave out_settings unchanged.
	void ParseSettings(const toml::table& a_root, Imagespace::Settings& a_outSettings);

	// Parses [weather]; a_dropOverrides skips user formID mappings for builtin presets.
	void ParseWeather(const toml::table& a_root, WeatherProfiles& a_outProfiles, bool a_dropOverrides = false);

	// Emits [settings] into a_root.
	void EmitSettings(toml::table& a_root, const Imagespace::Settings& a_settings);

	// Emits [weather]; overrides are omitted for builtin presets.
	void EmitWeather(toml::table& a_root, const WeatherProfiles& a_profiles, bool a_includeOverrides);
}

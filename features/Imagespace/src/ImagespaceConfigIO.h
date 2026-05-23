#pragma once

#include "Imagespace.h"
#include "WeatherProfiles.h"

#include <toml++/toml.hpp>

namespace cs::features::imagespace
{
	// Parses the [settings] subtable into out_settings. Missing keys leave out_settings unchanged
	// (caller is expected to reset to Imagespace::Settings{} when full snapshot semantics are
	// wanted). Unknown keys and unknown tables are silently ignored.
	void ParseSettings(const toml::table& a_root, Imagespace::Settings& a_outSettings);

	// Parses the [weather] subtable into out_profiles. Unknown sub-tables and keys are silently
	// ignored. If a_dropOverrides is true, the [weather.overrides] formID map is not consumed
	// (used by PresetManager::Load for builtin presets, which must not stamp formID mappings).
	void ParseWeather(const toml::table& a_root, WeatherProfiles& a_outProfiles, bool a_dropOverrides = false);

	// Emits the [settings] subtable into a_root (insert-or-assign).
	void EmitSettings(toml::table& a_root, const Imagespace::Settings& a_settings);

	// Emits the [weather] subtable into a_root. When a_includeOverrides is true, also emits the
	// [weather.overrides] formID map. Set false for builtin presets (which must not ship user formIDs).
	void EmitWeather(toml::table& a_root, const WeatherProfiles& a_profiles, bool a_includeOverrides);
}

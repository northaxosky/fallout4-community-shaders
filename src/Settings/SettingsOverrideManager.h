#pragma once

#include <optional>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::settings_overrides
{
	// Looks up Data\F4SE\Plugins\FO4CommunityShaders\overrides\<FeatureName>.toml; returns the
	// parsed table on success, std::nullopt if the file is missing or the parse failed. Failures
	// are logged but never throw.
	//
	// Caller is expected to feed the returned table back through the same feature's ParseSettings
	// helper as a second pass so absent keys keep the base TOML value and present keys overlay.
	// See Imagespace's settings load (Imagespace.cpp) for the canonical call pattern.
	std::optional<toml::table> TryLoad(std::string_view a_featureName);
}

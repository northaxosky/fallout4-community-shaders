#pragma once

#include <optional>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::settings_overrides
{
	// Loads overrides\<FeatureName>.toml if present; parse failures log and return std::nullopt.
	// Feed the table through the feature parser as an overlay so absent keys keep base TOML values.
	std::optional<toml::table> TryLoad(std::string_view a_featureName);
}

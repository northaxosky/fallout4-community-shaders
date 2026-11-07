#pragma once

#include <optional>
#include <string_view>

namespace cs::env
{
	// One-shot detection. Call from F4SEPlugin_Load after the logger is attached.
	void DetectENB();

	// Cached result of DetectENB. False before DetectENB has run.
	bool IsENBLoaded() noexcept;

	// One-shot scan of Data/F4SE/Plugins; same call contract as DetectENB.
	void DetectPluginConflicts();

	// Why an installed third-party mod deactivates a feature; empty if none.
	std::optional<std::string_view> FeatureConflict(std::string_view a_featureKey) noexcept;

	// True when DetectPluginConflicts found anything; CS must then stay inert.
	bool IsBlocked() noexcept;

	// Shows one message box per conflict. Call once game data is ready.
	void ShowBlockingErrors();
}

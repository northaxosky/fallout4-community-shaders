#pragma once

#include <span>
#include <string>
#include <string_view>

namespace cs::compat
{
	enum class Source
	{
		kF4SEPlugin,  // id: plugin name from F4SEPlugin_Version
		kGameFile,    // id: path relative to the game directory
		kENB          // id unused
	};

	struct Identity
	{
		Source source;
		std::string_view id = {};
	};

	struct Rule
	{
		std::string_view mod;
		Identity identity;
		// Features kept inactive; empty means every feature.
		std::span<const std::string_view> features;
		// Shown to users as is.
		std::string_view reason;
	};

	struct Finding
	{
		const Rule& rule;
		// What was detected, e.g. "FO4Isotope 1.4.0 (FO4Isotope.dll)".
		std::string source;

		bool Affects(std::string_view a_featureKey) const noexcept;
	};

	// One-shot detection. Call from F4SEPlugin_Load after the logger is attached.
	void Detect();

	std::span<const Finding> Findings() noexcept;

	// First finding that deactivates the feature; null if none.
	const Finding* Find(std::string_view a_featureKey) noexcept;
}

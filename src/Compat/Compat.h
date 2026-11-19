#pragma once

#include <span>
#include <string>
#include <string_view>

namespace cs
{
	class Feature;
}

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

	// A feature singleton accessor, so a renamed or removed feature breaks the build.
	using FeatureRef = Feature* (*)() noexcept;

	struct Rule
	{
		std::string_view mod;
		Identity identity;
		// Features kept inactive; empty means every feature.
		std::span<const FeatureRef> features;
		// Shown to users as is.
		std::string_view reason;
	};

	struct Finding
	{
		const Rule& rule;
		// What was detected, e.g. "FO4Isotope 1.4.0 (FO4Isotope.dll)".
		std::string source;

		bool Affects(const Feature& a_feature) const noexcept;
	};

	// One-shot detection. Call from F4SEPlugin_Load after the logger is attached.
	void Detect();

	std::span<const Finding> Findings() noexcept;

	// First finding that deactivates the feature; null if none.
	const Finding* Find(const Feature& a_feature) noexcept;
}

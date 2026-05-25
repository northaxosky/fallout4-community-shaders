#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::toml_util
{
	// Reads scalar TOML keys with default fallback, clamping where a range is supplied, and emits a
	// standardized warning when a present key has the wrong type so user-edited TOMLs surface typos
	// instead of silently falling back. a_logCtx is a short label that identifies the feature, e.g.
	// "ssgi.settings" or "imagespace.tonemap"; it shows up verbatim in the warning line.
	//
	// Missing keys are silent. Wrong-type keys log once per call and return the default. Numeric
	// reads clamp to [a_min, a_max] when the source value is in range of the storage type. Bool and
	// string reads have no clamping.
	bool ReadBool(const toml::table& a_table, std::string_view a_key, bool a_default, std::string_view a_logCtx);

	int ReadInt(const toml::table& a_table, std::string_view a_key, int a_default, int a_min, int a_max, std::string_view a_logCtx);

	std::uint32_t ReadUInt(const toml::table& a_table, std::string_view a_key, std::uint32_t a_default, std::uint32_t a_min, std::uint32_t a_max, std::string_view a_logCtx);

	float ReadFloat(const toml::table& a_table, std::string_view a_key, float a_default, float a_min, float a_max, std::string_view a_logCtx);

	std::string ReadString(const toml::table& a_table, std::string_view a_key, std::string_view a_default, std::string_view a_logCtx);
}

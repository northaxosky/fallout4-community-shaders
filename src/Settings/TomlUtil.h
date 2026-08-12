#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <toml++/toml.hpp>

namespace cs::toml_util
{
	// Invalid TOML uses defaults, warnings, and numeric clamping.
	bool ReadBool(const toml::table& a_table, std::string_view a_key, bool a_default, std::string_view a_logCtx);

	int ReadInt(const toml::table& a_table, std::string_view a_key, int a_default, int a_min, int a_max, std::string_view a_logCtx);

	std::uint32_t ReadUInt(const toml::table& a_table, std::string_view a_key, std::uint32_t a_default, std::uint32_t a_min, std::uint32_t a_max, std::string_view a_logCtx);

	float ReadFloat(const toml::table& a_table, std::string_view a_key, float a_default, float a_min, float a_max, std::string_view a_logCtx);

	std::string ReadString(const toml::table& a_table, std::string_view a_key, std::string_view a_default, std::string_view a_logCtx);
}

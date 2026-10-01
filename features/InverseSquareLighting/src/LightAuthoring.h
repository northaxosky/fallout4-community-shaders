#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

namespace cs::features::inverse_square_lighting
{
	struct FormIdentity
	{
		std::string plugin;
		std::uint32_t formID = 0;
	};

	struct AuthoredLight
	{
		std::optional<bool> inverseSquare;
		std::optional<bool> linear;
		std::optional<float> cutoff;
		std::optional<float> size;
		void Apply(const AuthoredLight& a_override);
	};

	struct LightDefinition
	{
		FormIdentity identity;
		bool reference = false;
		AuthoredLight data;
	};

	bool ParseLightDefinitions(const toml::table& a_table,
		std::vector<LightDefinition>& a_definitions, std::string& a_error);
	bool LoadLightDefinitions(const std::filesystem::path& a_directory,
		std::vector<LightDefinition>& a_definitions, std::string& a_error);
}

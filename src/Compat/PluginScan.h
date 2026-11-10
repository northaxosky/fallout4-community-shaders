#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "REL/Version.h"

namespace cs::compat
{
	struct ScannedPlugin
	{
		std::string name;
		REL::Version version;
		std::filesystem::path file;
	};

	// Reads each DLL's F4SEPlugin_Version without running its code.
	std::vector<ScannedPlugin> ScanF4SEPlugins(const std::filesystem::path& a_directory);
}

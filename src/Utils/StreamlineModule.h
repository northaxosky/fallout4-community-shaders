#pragma once

#include <filesystem>

#include <source/core/sl.security/projectTrust.h>

namespace cs::files
{
	// Authenticate and load in one operation so verified handles survive until DllMain.
	[[nodiscard]] sl::security::ProjectLoadResult LoadStreamlineInterposer(
		const std::filesystem::path& a_directory,
		const sl::security::ProjectTrustConfig& a_trust =
			sl::security::getCompiledProjectTrust());
}

#pragma once

#include <expected>
#include <filesystem>
#include <system_error>

namespace cs::files
{
	// Resolves the backing file rather than the virtual name reported by USVFS.
	std::expected<std::filesystem::path, std::error_code> PhysicalFilePath(
		const std::filesystem::path& a_path);
}

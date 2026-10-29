#pragma once

#include <filesystem>
#include <string>

namespace cs::util
{
	[[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& a_path)
	{
		const auto encoded = a_path.u8string();
		return { encoded.begin(), encoded.end() };
	}
}

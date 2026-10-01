#pragma once

#include <filesystem>
#include <system_error>

namespace cs::util
{
	inline constexpr auto kDefaultShaderRoot = L"Data\\Shaders";

	inline std::filesystem::path ResolveShaderInclude(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_name)
	{
		std::error_code error;
		auto candidate = std::filesystem::weakly_canonical(a_root / a_name, error);
		if (error || candidate.empty())
			candidate = (a_root / a_name).lexically_normal();
		return candidate;
	}
}

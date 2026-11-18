#pragma once

#include <cctype>
#include <cstddef>
#include <string_view>

namespace cs::util
{
	// ASCII case-insensitive equality, as the engine compares editor IDs and names.
	[[nodiscard]] inline bool EqualsIgnoreCase(std::string_view a_lhs, std::string_view a_rhs) noexcept
	{
		if (a_lhs.size() != a_rhs.size())
			return false;
		for (std::size_t i = 0; i < a_lhs.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a_lhs[i])) != std::tolower(static_cast<unsigned char>(a_rhs[i])))
				return false;
		}
		return true;
	}
}

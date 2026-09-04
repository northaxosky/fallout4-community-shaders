#pragma once

#include <cstdint>

namespace cs::features::skylighting
{
	[[nodiscard]] constexpr std::uint32_t NormalizeProbeArrayOrigin(
		std::int64_t a_origin,
		std::uint32_t a_dimension) noexcept
	{
		const auto dimension = static_cast<std::int64_t>(a_dimension);
		const auto remainder = a_origin % dimension;
		return static_cast<std::uint32_t>(
			remainder < 0 ? remainder + dimension : remainder);
	}
}

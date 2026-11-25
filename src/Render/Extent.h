#pragma once

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>

namespace cs::render
{
	struct Extent
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return width != 0 && height != 0; }
		auto operator<=>(const Extent&) const = default;
	};

	// The one rounding rule: float product, truncated.
	[[nodiscard]] inline std::uint32_t ScaledExtent(std::uint32_t a_extent, float a_ratio) noexcept
	{
		if (!std::isfinite(a_ratio) || a_ratio <= 0.0f)
			return 0;
		return (std::min)(a_extent, static_cast<std::uint32_t>(static_cast<float>(a_extent) * a_ratio));
	}
}

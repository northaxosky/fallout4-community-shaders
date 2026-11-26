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

	// Half-open pixel rectangle; empty when either side has no area.
	struct PixelRect
	{
		std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;

		[[nodiscard]] constexpr std::uint32_t Width() const noexcept { return x1 > x0 ? x1 - x0 : 0; }
		[[nodiscard]] constexpr std::uint32_t Height() const noexcept { return y1 > y0 ? y1 - y0 : 0; }
		[[nodiscard]] constexpr bool IsEmpty() const noexcept { return Width() == 0 || Height() == 0; }
		[[nodiscard]] static constexpr PixelRect Full(Extent a_extent) noexcept { return { 0, 0, a_extent.width, a_extent.height }; }

		constexpr void Union(const PixelRect& a_other) noexcept
		{
			if (a_other.IsEmpty())
				return;
			if (IsEmpty()) {
				*this = a_other;
				return;
			}
			x0 = (std::min)(x0, a_other.x0);
			y0 = (std::min)(y0, a_other.y0);
			x1 = (std::max)(x1, a_other.x1);
			y1 = (std::max)(y1, a_other.y1);
		}
	};

	// The one rounding rule: float product, truncated.
	[[nodiscard]] inline std::uint32_t ScaledExtent(std::uint32_t a_extent, float a_ratio) noexcept
	{
		if (!std::isfinite(a_ratio) || a_ratio <= 0.0f)
			return 0;
		return (std::min)(a_extent, static_cast<std::uint32_t>(static_cast<float>(a_extent) * a_ratio));
	}
}

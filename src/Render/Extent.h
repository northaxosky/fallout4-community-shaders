#pragma once

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
}

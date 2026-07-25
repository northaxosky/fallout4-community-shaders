#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::features::catalog::subclass_attribution
{
	bool Register(std::size_t a_pixelShadersOffset) noexcept;

	struct RuntimeStats
	{
		std::uint64_t setupTechniqueCalls = 0;
		std::uint64_t mapAttributions = 0;
	};

	RuntimeStats GetRuntimeStats() noexcept;
}

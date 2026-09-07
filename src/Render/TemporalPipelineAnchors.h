#pragma once

#include <cstddef>
#include <cstdint>

namespace cs::render::temporal_anchors
{
	inline constexpr std::uint64_t kMainLoopAnchor[] = {
		1125396, 2718225, 4484191
	};
	inline constexpr std::ptrdiff_t kMainLoopMessageLoopCall[] = {
		0x1E, 0x4E, 0x2E
	};
	inline constexpr std::uint64_t kWindowsMessageLoop[] = {
		847266, 2228915, 2228915
	};

	inline constexpr std::uint64_t kOnIdle[] = {
		633524, 2228917, 2228917
	};
	inline constexpr std::ptrdiff_t kOnIdleSwapCall[] = {
		0x6EC, 0xCDC, 0xCDC
	};
	inline constexpr std::uint64_t kSwap[] = {
		1075087, 2228913, 2228913
	};
}

#pragma once

#include <cstdint>

namespace cs::engine
{
	// Captured once by the plugin so host code never queries REX.
	enum class GameRuntime : std::uint8_t
	{
		kOG,
		kNG,
		kAE
	};
}

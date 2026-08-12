#pragma once

namespace cs::engine
{
	// Returns false without a usable exterior sun.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;
}

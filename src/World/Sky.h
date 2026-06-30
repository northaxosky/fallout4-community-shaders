#pragma once

namespace cs::engine
{
	// Sun direction from RE::Sky light row 0; false at night, in interiors, or when unbound.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;
}

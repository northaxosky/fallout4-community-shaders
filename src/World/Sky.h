#pragma once

namespace cs::engine
{
	// Sun world-space direction sourced from RE::Sky->sun->light->world.rotate row 0.
	// Returns false at night, in interiors, or any state where the sun light is not bound.
	bool TryGetSunDirectionWS(float& outX, float& outY, float& outZ) noexcept;
}

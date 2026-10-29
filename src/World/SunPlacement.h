#pragma once

#include "RE/N/NiPoint3.h"
#include "RE/S/Sky.h"
#include "RE/S/Sun.h"

namespace cs::engine
{
	// Places the disc and glare along a sky-local unit direction.
	void SetSunPosition(const RE::Sky& a_sky, const RE::NiPoint3& a_direction, float a_distance);

	// Sets disc and glare alpha, and the composite's disc alpha.
	void SetSunAlpha(const RE::Sun& a_sun, float a_disc, float a_glare);

	// Points the sun and cloud lights away from a sky-local body direction.
	void SetSunLightDirection(const RE::Sky& a_sky, const RE::NiPoint3& a_towardsBody);
}

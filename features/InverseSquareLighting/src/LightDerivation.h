#pragma once

#include "LightAuthoring.h"

#include <optional>

namespace cs::features::inverse_square_lighting
{
	// Native falloff of one light instance: radiance = fade * pow(1 - saturate(a + b * x^c), 2.2).
	struct NativeLight
	{
		float radius = 0;
		float fade = 0;
		float a = 0, b = 0, c = 0;
		// ISL's pow(abs) color path would lose the negated sign.
		bool negatedColor = false;
	};

	struct DerivedLight
	{
		AuthoredLight authored;
		float intensityScale = 1;
	};

	// Fills only the upstream-authored inputs; nullopt keeps the light native.
	std::optional<DerivedLight> DeriveLight(const NativeLight& a_light);
}

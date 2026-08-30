// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky
#pragma once

#include "Common/SharedData.hlsli"

namespace InverseSquareLighting
{
	static const uint MODE_ENABLED = 1U << 0;
	static const uint MODE_COMPARISON_DEBUG = 1U << 1;
	static const float INVERSE_SQUARE_SCALE = 3920.0;
	static const float FADE_BASE = 252.0;
	static const float MAX_SAFE_SQUARE_INPUT = 1.0e19;

	float GetAttenuation(
		float vanilla,
		float distance,
		float radius,
		float pixelPositionX)
	{
		uint mode = SharedData::inverseSquareLightingSettings.Mode;
		if ((mode & MODE_ENABLED) == 0)
			return vanilla;

		float strength = SharedData::InInterior ?
			SharedData::inverseSquareLightingSettings.InteriorStrength :
			SharedData::inverseSquareLightingSettings.ExteriorStrength;
		if (!isfinite(strength) || strength <= 0.0)
			return vanilla;
		strength = saturate(strength);

		if ((mode & MODE_COMPARISON_DEBUG) != 0
			&& pixelPositionX
				< SharedData::BufferDim.x
					* SharedData::DynamicResolution.x * 0.5) {
			return vanilla;
		}

		float nearFieldDistance =
			SharedData::inverseSquareLightingSettings.NearFieldDistance;
		if (!isfinite(vanilla)
			|| !isfinite(distance)
			|| distance < 0.0
			|| distance > MAX_SAFE_SQUARE_INPUT
			|| !isfinite(radius)
			|| radius <= 0.0
			|| !isfinite(nearFieldDistance)
			|| nearFieldDistance <= 0.0
			|| nearFieldDistance > MAX_SAFE_SQUARE_INPUT) {
			return vanilla;
		}

		float denominator =
			distance * distance + nearFieldDistance * nearFieldDistance;
		if (!isfinite(denominator) || denominator <= 0.0)
			return vanilla;

		float fadeWidth = min(radius, FADE_BASE);
		float fade = saturate((radius - distance) / fadeWidth);
		float cutoff = fade * fade * (3.0 - 2.0 * fade);
		float physical = INVERSE_SQUARE_SCALE / denominator * cutoff;
		if (!isfinite(physical))
			return vanilla;
		return lerp(vanilla, physical, strength);
	}
}

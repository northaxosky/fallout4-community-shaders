#pragma once

#include <algorithm>
#include <cmath>

namespace cs::features::inverse_square_lighting
{
	inline constexpr float kScaledUnitsSq = 0.8f * 70.0f * 70.0f;
	inline constexpr float kFadeZoneBase = 4.5f * 0.8f * 70.0f;

	inline float CalculateRadius(float a_intensity, bool a_shadowCaster,
		float a_cutoffOverride, float a_size) noexcept
	{
		float cutoff = a_shadowCaster ? 0.022f : 0.05f;
		cutoff = a_cutoffOverride == 1.0f ? cutoff : a_cutoffOverride;
		const float radius = std::sqrt(kScaledUnitsSq *
									   ((2 * a_intensity - cutoff * a_size * a_size) / (2 * cutoff)));
		return std::isnan(radius) ? 1.0f : radius;
	}

	inline float GetAttenuation(float a_distance, float a_radius, float a_size) noexcept
	{
		const float attenuation = kScaledUnitsSq /
		                          (a_distance * a_distance + kScaledUnitsSq * a_size * a_size / 2);
		const float fadeZone = std::clamp(kFadeZoneBase / a_radius, 0.0f, 1.0f);
		const float t = std::clamp((a_radius - a_distance) / (a_radius * fadeZone), 0.0f, 1.0f);
		const float fade = t * t * (3.0f - 2.0f * t);
		return attenuation * fade;
	}
}

#pragma once

#include <algorithm>
#include <cmath>

namespace cs::features::wetness_math
{
	struct Settings
	{
		bool enabled = true;
		float maxRainWetness = 1.0f;
		float minRainWetness = 0.65f;
	};

	inline constexpr float kMaxRainWetnessMin = 0.0f;
	inline constexpr float kMaxRainWetnessMax = 2.5f;
	inline constexpr float kMinRainWetnessMin = 0.0f;
	inline constexpr float kMinRainWetnessMax = 0.9f;

	inline Settings Clamp(Settings a_settings) noexcept
	{
		a_settings.maxRainWetness = std::clamp(
			a_settings.maxRainWetness, kMaxRainWetnessMin, kMaxRainWetnessMax);
		a_settings.minRainWetness = std::clamp(
			a_settings.minRainWetness, kMinRainWetnessMin, kMinRainWetnessMax);
		return a_settings;
	}

	// interiors and non-rain weather stay exactly zero, which is shader identity
	inline float ComputeWeatherWetness(
		bool a_isExterior,
		bool a_previousIsRain,
		bool a_currentIsRain,
		float a_transitionPct) noexcept
	{
		if (!a_isExterior)
			return 0.0f;
		if (!std::isfinite(a_transitionPct))
			return a_currentIsRain ? 1.0f : 0.0f;

		const float transition = std::clamp(a_transitionPct, 0.0f, 1.0f);
		const float previous = a_previousIsRain ? 1.0f : 0.0f;
		const float current = a_currentIsRain ? 1.0f : 0.0f;
		return std::lerp(previous, current, transition);
	}

	// disabled publishes exact zero; enabled hands the weather value through untouched
	inline constexpr float PublishedWetness(bool a_enabled, float a_weatherWetness) noexcept
	{
		return a_enabled ? a_weatherWetness : 0.0f;
	}
}

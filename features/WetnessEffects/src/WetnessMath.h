#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <cmath>

namespace cs::features::wetness_math
{
	struct Settings
	{
		bool enabled = true;
		float maxRainWetness = 1.0f;
		float minRainWetness = 0.65f;
		float puddleRadius = 1.0f;
		float puddleMaxAngle = 0.95f;
		float maxPuddleWetness = 1.5f;
	};

	inline constexpr float kMaxRainWetnessMin = 0.0f;
	inline constexpr float kMaxRainWetnessMax = 2.5f;
	inline constexpr float kMinRainWetnessMin = 0.0f;
	inline constexpr float kMinRainWetnessMax = 0.9f;
	inline constexpr float kPuddleRadiusMin = 0.3f;
	inline constexpr float kPuddleRadiusMax = 3.0f;
	inline constexpr float kPuddleMaxAngleMin = 0.6f;
	inline constexpr float kPuddleMaxAngleMax = 1.0f;
	inline constexpr float kMaxPuddleWetnessMin = 0.0f;
	inline constexpr float kMaxPuddleWetnessMax = 6.0f;

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable rain wetness.", &Settings::enabled },
			settings::Field{ "max_rain_wetness", "Maximum rain wetness strength.", &Settings::maxRainWetness, settings::Range{ kMaxRainWetnessMin, kMaxRainWetnessMax } },
			settings::Field{ "min_rain_wetness", "Minimum rain wetness strength.", &Settings::minRainWetness, settings::Range{ kMinRainWetnessMin, kMinRainWetnessMax } },
			settings::Field{ "puddle_radius", "The radius used to determine puddle size and location", &Settings::puddleRadius, settings::Range{ kPuddleRadiusMin, kPuddleRadiusMax } },
			settings::Field{ "puddle_max_angle", "How flat a surface needs to be for puddles to form on it.", &Settings::puddleMaxAngle, settings::Range{ kPuddleMaxAngleMin, kPuddleMaxAngleMax } },
			settings::Field{ "max_puddle_wetness", "Puddle Wetness", &Settings::maxPuddleWetness, settings::Range{ kMaxPuddleWetnessMin, kMaxPuddleWetnessMax } }
		}
	};

	inline Settings Clamp(Settings a_settings) noexcept
	{
		a_settings.maxRainWetness = std::clamp(
			a_settings.maxRainWetness, kMaxRainWetnessMin, kMaxRainWetnessMax);
		a_settings.minRainWetness = std::clamp(
			a_settings.minRainWetness, kMinRainWetnessMin, kMinRainWetnessMax);
		a_settings.puddleRadius = std::clamp(
			a_settings.puddleRadius, kPuddleRadiusMin, kPuddleRadiusMax);
		a_settings.puddleMaxAngle = std::clamp(
			a_settings.puddleMaxAngle, kPuddleMaxAngleMin, kPuddleMaxAngleMax);
		a_settings.maxPuddleWetness = std::clamp(
			a_settings.maxPuddleWetness, kMaxPuddleWetnessMin, kMaxPuddleWetnessMax);
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

	// FO4CS weather wetness has no precipitation fade thresholds, so each weather's wetness is its transition weight.
	inline float ComputeWeatherPuddleWetness(
		bool a_isExterior,
		bool a_previousIsRain,
		bool a_currentIsRain,
		float a_transitionPct) noexcept
	{
		if (!a_isExterior)
			return 0.0f;
		const float transition = std::isfinite(a_transitionPct) ?
			std::clamp(a_transitionPct, 0.0f, 1.0f) :
			1.0f;
		const float currentWetness = a_currentIsRain ? transition : 0.0f;
		const float currentPuddleWetness = std::pow(currentWetness, 2.0f);
		const float lastPuddleWetness = a_previousIsRain ? std::pow(1.0f - transition, 0.25f) : 0.0f;
		return std::min(1.0f, currentPuddleWetness + lastPuddleWetness);
	}

	// disabled publishes exact zero; enabled hands the weather value through untouched
	inline constexpr float PublishedWetness(bool a_enabled, float a_weatherWetness) noexcept
	{
		return a_enabled ? a_weatherWetness : 0.0f;
	}
}

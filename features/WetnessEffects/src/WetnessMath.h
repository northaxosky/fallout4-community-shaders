#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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
		float maxShoreWetness = 1.0f;
		std::uint32_t shoreRange = 32;
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
	inline constexpr float kMaxShoreWetnessMin = 0.0f;
	inline constexpr float kMaxShoreWetnessMax = 1.0f;
	inline constexpr std::uint32_t kShoreRangeMin = 1;
	inline constexpr std::uint32_t kShoreRangeMax = 64;

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable rain wetness.", &Settings::enabled },
			settings::Field{ "max_rain_wetness", "Maximum rain wetness strength.", &Settings::maxRainWetness, settings::Range{ kMaxRainWetnessMin, kMaxRainWetnessMax } },
			settings::Field{ "min_rain_wetness", "Minimum rain wetness strength.", &Settings::minRainWetness, settings::Range{ kMinRainWetnessMin, kMinRainWetnessMax } },
			settings::Field{ "puddle_radius", "The radius used to determine puddle size and location", &Settings::puddleRadius, settings::Range{ kPuddleRadiusMin, kPuddleRadiusMax } },
			settings::Field{ "puddle_max_angle", "How flat a surface needs to be for puddles to form on it.", &Settings::puddleMaxAngle, settings::Range{ kPuddleMaxAngleMin, kPuddleMaxAngleMax } },
			settings::Field{ "max_puddle_wetness", "Puddle Wetness", &Settings::maxPuddleWetness, settings::Range{ kMaxPuddleWetnessMin, kMaxPuddleWetnessMax } },
			settings::Field{ "max_shore_wetness", "Shore Wetness", &Settings::maxShoreWetness, settings::Range{ kMaxShoreWetnessMin, kMaxShoreWetnessMax } },
			settings::Field{ "shore_range", "The maximum distance from a body of water that Shore Wetness affects", &Settings::shoreRange, settings::Range{ kShoreRangeMin, kShoreRangeMax } } }
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
		a_settings.maxShoreWetness = std::clamp(
			a_settings.maxShoreWetness, kMaxShoreWetnessMin, kMaxShoreWetnessMax);
		a_settings.shoreRange = std::clamp(
			a_settings.shoreRange, kShoreRangeMin, kShoreRangeMax);
		return a_settings;
	}

	struct WeatherWetnessResult
	{
		float wetness = 0.0f;
		float puddleWetness = 0.0f;
	};

	inline float LinearStep(float a_edge0, float a_edge1, float a_x) noexcept
	{
		if (a_edge0 >= a_edge1)
			return a_x >= a_edge1 ? 1.0f : 0.0f;
		return std::clamp((a_x - a_edge0) / (a_edge1 - a_edge0), 0.0f, 1.0f);
	}

	inline WeatherWetnessResult CalculateWeatherWetness(
		bool a_isRain,
		std::uint8_t a_fadeValue,
		float a_weatherPct,
		bool a_isCurrentWeather) noexcept
	{
		WeatherWetnessResult result{};
		if (!a_isRain)
			return result;

		if (a_isCurrentWeather) {
			// Current weather uses fade-in logic
			const float fadeValue = a_fadeValue;
			const float fadeNormalized = fadeValue / 255.0f;
			const float fadeThreshold = 255.0f * (1.0f - fadeNormalized);
			const float weatherProgress = a_weatherPct * 255.0f;

			if (fadeNormalized == 0.0f) {
				// No fade-in period, use immediate wetness
				result.wetness = (a_weatherPct > 0.1f) ? 1.0f : 0.0f;
			} else {
				result.wetness = LinearStep(fadeThreshold, 255.0f, weatherProgress);
			}
			result.puddleWetness = std::pow(result.wetness, 2.0f);
		} else {
			// Last weather uses fade-out logic
			const float fadeValue = a_fadeValue;
			const float fadeNormalized = fadeValue / 255.0f;
			const float fadeThreshold = 255.0f * fadeNormalized;
			const float weatherProgress = a_weatherPct * 255.0f;

			result.wetness = 1.0f - LinearStep(fadeThreshold, 255.0f, weatherProgress);
			result.puddleWetness = std::pow(std::max(result.wetness, 1.0f - a_weatherPct), 0.25f);
		}
		return result;
	}

	inline WeatherWetnessResult ComputeWeatherWetness(
		bool a_isExterior,
		bool a_previousIsRain,
		std::uint8_t a_previousEndPrecip,
		bool a_currentIsRain,
		std::uint8_t a_currentBeginPrecip,
		float a_transitionPct) noexcept
	{
		// FO4 uses the exterior cell check instead of Skyrim's full-sky mode.
		if (!a_isExterior)
			return {};
		const float transition = std::isfinite(a_transitionPct) ?
		                             std::clamp(a_transitionPct, 0.0f, 1.0f) :
		                             1.0f;
		const auto current = CalculateWeatherWetness(a_currentIsRain, a_currentBeginPrecip, transition, true);
		const auto last = CalculateWeatherWetness(a_previousIsRain, a_previousEndPrecip, transition, false);
		return {
			std::min(1.0f, current.wetness + last.wetness),
			std::min(1.0f, current.puddleWetness + last.puddleWetness)
		};
	}

	// disabled publishes exact zero; enabled hands the weather value through untouched
	inline constexpr float PublishedWetness(bool a_enabled, float a_weatherWetness) noexcept
	{
		return a_enabled ? a_weatherWetness : 0.0f;
	}
}

#pragma once

#include "Settings/SettingsSchema.h"

#include <algorithm>
#include <array>
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
		float puddleMinWetness = 0.85f;
		float skinWetness = 0.95f;
		float weatherTransitionSpeed = 3.0f;
		bool enableRaindropFx = true;
		bool enableSplashes = true;
		bool enableRipples = true;
		bool enableVanillaRipples = false;
		float raindropFxRange = 1000.0f;
		float raindropGridSize = 4.0f;
		float raindropInterval = 1.0f;
		float raindropChance = 1.0f;
		float splashesLifetime = 10.0f;
		float splashesStrength = 1.05f;
		float splashesMinRadius = 0.3f;
		float splashesMaxRadius = 0.5f;
		float rippleStrength = 1.0f;
		float rippleRadius = 1.0f;
		float rippleBreadth = 0.5f;
		float rippleLifetime = 0.5f;
		bool enableWetnessOverride = false, enablePuddleOverride = false, enableRainOverride = false, enableIntExOverride = false;
		float wetnessOverrideInterior = 0.0f, wetnessOverrideExterior = 0.0f;
		float puddleOverrideInterior = 0.0f, puddleOverrideExterior = 0.0f;
		float rainOverrideInterior = 0.0f, rainOverrideExterior = 0.0f;
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
			settings::Field{ "shore_range", "The maximum distance from a body of water that Shore Wetness affects", &Settings::shoreRange, settings::Range{ kShoreRangeMin, kShoreRangeMax } },
			settings::Field{ "puddle_min_wetness", "Puddle minimum wetness", &Settings::puddleMinWetness, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "skin_wetness", "Skin wetness", &Settings::skinWetness, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "weather_transition_speed", "Weather transition speed", &Settings::weatherTransitionSpeed, settings::Range{ 0.2f, 8.0f } },
			settings::Field{ "enable_raindrop_fx", "Enable raindrop effects", &Settings::enableRaindropFx },
			settings::Field{ "enable_splashes", "Enable splashes", &Settings::enableSplashes },
			settings::Field{ "enable_ripples", "Enable ripples", &Settings::enableRipples },
			settings::Field{ "enable_vanilla_ripples", "Enable vanilla ripples", &Settings::enableVanillaRipples },
			settings::Field{ "raindrop_fx_range", "Raindrop effect range", &Settings::raindropFxRange, settings::Range{ 100.0f, 2000.0f } },
			settings::Field{ "raindrop_grid_size", "Raindrop grid size", &Settings::raindropGridSize, settings::Range{ 1.0f, 10.0f } },
			settings::Field{ "raindrop_interval", "Raindrop interval", &Settings::raindropInterval, settings::Range{ 0.1f, 2.0f } },
			settings::Field{ "raindrop_chance", "Raindrop chance", &Settings::raindropChance, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "splashes_lifetime", "Splash lifetime", &Settings::splashesLifetime, settings::Range{ 0.1f, 20.0f } },
			settings::Field{ "splashes_strength", "Splash strength", &Settings::splashesStrength, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "splashes_min_radius", "Splash minimum radius", &Settings::splashesMinRadius, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "splashes_max_radius", "Splash maximum radius", &Settings::splashesMaxRadius, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "ripple_strength", "Ripple strength", &Settings::rippleStrength, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "ripple_radius", "Ripple radius", &Settings::rippleRadius, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "ripple_breadth", "Ripple breadth", &Settings::rippleBreadth, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "ripple_lifetime", "Ripple lifetime", &Settings::rippleLifetime, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "enable_wetness_override", "Override wetness", &Settings::enableWetnessOverride },
			settings::Field{ "enable_puddle_override", "Override puddles", &Settings::enablePuddleOverride },
			settings::Field{ "enable_rain_override", "Override rain", &Settings::enableRainOverride },
			settings::Field{ "enable_interior_exterior_override", "Separate interior overrides", &Settings::enableIntExOverride },
			settings::Field{ "wetness_override_interior", "Interior wetness", &Settings::wetnessOverrideInterior, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "wetness_override_exterior", "Exterior wetness", &Settings::wetnessOverrideExterior, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "puddle_override_interior", "Interior puddles", &Settings::puddleOverrideInterior, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "puddle_override_exterior", "Exterior puddles", &Settings::puddleOverrideExterior, settings::Range{ 0.0f, 2.0f } },
			settings::Field{ "rain_override_interior", "Interior rain", &Settings::rainOverrideInterior, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "rain_override_exterior", "Exterior rain", &Settings::rainOverrideExterior, settings::Range{ 0.0f, 1.0f } } }
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

	struct Climate
	{
		const char* name;
		float wetness, puddles, transition, chance, grid, interval;
	};
	inline constexpr std::array kClimates{
		Climate{ "Custom", 0, 0, 0, 0, 0, 0 },
		Climate{ "Legacy", 1, 1, 1, 0.3f, 4, 0.5f },
		Climate{ "Nordic (Default)", 1, 1, 1, 1, 3, 1 },
		Climate{ "Arctic Tundra", 0.5f, 0.3f, 0.5f, 0.3f, 3.5f, 0.4f },
		Climate{ "Temperate Coastal", 1.5f, 1.7f, 1.7f, 0.8f, 2.5f, 0.25f },
		Climate{ "Monsoon/Extreme", 2, 2.5f, 2, 1, 2, 0.2f }
	};
	inline void ApplyClimate(Settings& a_settings, std::size_t a_index)
	{
		const auto& climate = kClimates[a_index];
		const Settings defaults{};
		a_settings.maxRainWetness = defaults.maxRainWetness * climate.wetness;
		a_settings.maxPuddleWetness = defaults.maxPuddleWetness * climate.puddles;
		a_settings.weatherTransitionSpeed = defaults.weatherTransitionSpeed * climate.transition;
		a_settings.raindropChance = climate.chance;
		a_settings.raindropGridSize = climate.grid;
		a_settings.raindropInterval = climate.interval;
	}
	inline std::size_t DetectClimate(const Settings& a_settings)
	{
		for (std::size_t index = 1; index < kClimates.size(); ++index) {
			Settings expected{};
			ApplyClimate(expected, index);
			if (std::abs(a_settings.maxRainWetness - expected.maxRainWetness) < 0.001f &&
				std::abs(a_settings.maxPuddleWetness - expected.maxPuddleWetness) < 0.001f &&
				std::abs(a_settings.weatherTransitionSpeed - expected.weatherTransitionSpeed) < 0.001f &&
				std::abs(a_settings.raindropChance - expected.raindropChance) < 0.001f)
				return index;
		}
		return 0;
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
		// The caller supplies the host's full-sky state.
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

	inline float ComputeRaining(float a_currentDensity, std::uint8_t a_begin,
		float a_previousDensity, std::uint8_t a_end, float a_progress) noexcept
	{
		return a_currentDensity * LinearStep(a_begin / 255.0f, 1.0f, a_progress) +
		       a_previousDensity * (1.0f - LinearStep(0.0f, a_end / 255.0f, a_progress));
	}
}

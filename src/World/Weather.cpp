#include "World/Weather.h"

#include "RE/S/Sky.h"
#include "RE/T/TESWeather.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace cs::engine
{
	namespace
	{
		bool IsPrecipitation(const RE::TESWeather* a_weather) noexcept
		{
			if (!a_weather) {
				return false;
			}

			const auto flags = static_cast<std::uint8_t>(
				a_weather->weatherData[static_cast<std::size_t>(RE::TESWeather::WeatherData::kFlags)]);
			using F = RE::TESWeather::WeatherDataFlags;
			const auto precipitationFlags =
				static_cast<std::uint8_t>(F::kRainy) |
				static_cast<std::uint8_t>(F::kSnow);
			return (flags & precipitationFlags) != 0;
		}
	}

	WeatherSnapshot SnapshotWeather() noexcept
	{
		WeatherSnapshot s;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) return s;
		s.current        = sky->currentWeather;
		s.previous       = sky->lastWeather;
		s.transitionPct  = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		s.currentIsRain  = IsPrecipitation(s.current);
		s.previousIsRain = IsPrecipitation(s.previous);
		return s;
	}
}

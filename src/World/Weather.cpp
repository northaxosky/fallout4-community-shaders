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
		// FO4 stores precipitation fades and flags in signed weatherData bytes; decode them as unsigned.
		std::uint8_t WeatherByte(const RE::TESWeather* a_weather, RE::TESWeather::WeatherData a_field) noexcept
		{
			return a_weather ? static_cast<std::uint8_t>(
								   a_weather->weatherData[static_cast<std::size_t>(a_field)]) :
			                   0;
		}

		bool IsRain(const RE::TESWeather* a_weather) noexcept
		{
			if (!a_weather || !a_weather->precipitationData) {
				return false;
			}

			const auto flags = WeatherByte(a_weather, RE::TESWeather::WeatherData::kFlags);
			return (flags & static_cast<std::uint8_t>(RE::TESWeather::WeatherDataFlags::kRainy)) != 0;
		}
	}

	WeatherSnapshot SnapshotWeather() noexcept
	{
		WeatherSnapshot s;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky)
			return s;
		s.current = sky->currentWeather;
		s.previous = sky->lastWeather;
		s.transitionPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		s.currentIsRain = IsRain(s.current);
		s.previousIsRain = IsRain(s.previous);
		s.currentBeginPrecip = WeatherByte(s.current, RE::TESWeather::WeatherData::kBeginPrecip);
		s.previousEndPrecip = WeatherByte(s.previous, RE::TESWeather::WeatherData::kEndPrecip);
		return s;
	}
}

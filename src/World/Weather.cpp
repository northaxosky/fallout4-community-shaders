#include "World/Weather.h"

#include "RE/S/Sky.h"

#include <algorithm>

namespace cs::engine
{
	WeatherSnapshot SnapshotWeather() noexcept
	{
		WeatherSnapshot s;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky)
			return s;
		s.current = sky->currentWeather;
		s.previous = sky->lastWeather;
		s.transitionPct = std::clamp(sky->currentWeatherPct, 0.0f, 1.0f);
		return s;
	}
}

#include "Weather.h"

#include "RE/S/Sky.h"

namespace cs::engine
{
	WeatherSnapshot SnapshotWeather() noexcept
	{
		WeatherSnapshot s;
		auto* sky = RE::Sky::GetSingleton();
		if (!sky) return s;
		s.current       = sky->currentWeather;
		s.previous      = sky->lastWeather;
		s.transitionPct = sky->currentWeatherPct;
		return s;
	}
}

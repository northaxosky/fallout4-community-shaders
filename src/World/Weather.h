#pragma once

#include <cstdint>

namespace RE { class TESWeather; }

namespace cs::engine
{
	struct WeatherSnapshot
	{
		const RE::TESWeather* current       = nullptr;
		const RE::TESWeather* previous      = nullptr;
		float                 transitionPct = 1.0f;
		bool                  currentIsRain = false;
		bool                  previousIsRain = false;
		std::uint8_t          currentBeginPrecip = 0;
		std::uint8_t          previousEndPrecip = 0;
	};

	WeatherSnapshot SnapshotWeather() noexcept;
}

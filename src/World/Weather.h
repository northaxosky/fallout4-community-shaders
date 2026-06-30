#pragma once

namespace RE { class TESWeather; }

namespace cs::engine
{
	struct WeatherSnapshot
	{
		const RE::TESWeather* current       = nullptr;
		const RE::TESWeather* previous      = nullptr;
		float                 transitionPct = 1.0f;
	};

	WeatherSnapshot SnapshotWeather() noexcept;
}

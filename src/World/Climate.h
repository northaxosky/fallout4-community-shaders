#pragma once

#include "RE/T/TESClimate.h"

#include <cstdint>

namespace cs::engine::climate
{
	// Hours per timing unit: the record stores times in 10-minute steps.
	inline constexpr float kHoursPerTimingUnit = 1.0f / 6.0f;

	struct Interval
	{
		std::uint8_t begin;
		std::uint8_t end;
	};

	struct Timing
	{
		Interval sunrise;
		Interval sunset;
	};

	// FO4: TESClimate stores the four timing bytes flat.
	inline Timing GetTiming(const RE::TESClimate& a_climate)
	{
		return { { a_climate.sunriseBegin, a_climate.sunriseEnd }, { a_climate.sunsetBegin, a_climate.sunsetEnd } };
	}

	// Interval middle in hours, with Sun::Update's float ops.
	inline float GetMiddleHour(const Interval& a_interval)
	{
		return (a_interval.end * kHoursPerTimingUnit + a_interval.begin * kHoursPerTimingUnit) * 0.5f;
	}
}

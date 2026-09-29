#include "WetnessMath.h"

#include <iostream>
#include <limits>

int main()
{
	using cs::features::wetness_math::CalculateWeatherWetness;
	using cs::features::wetness_math::ComputeWeatherWetness;
	int failures = 0;
	const auto check = [&](bool a_condition, const char* a_message) {
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	};

	const auto interior = ComputeWeatherWetness(false, true, 128, true, 128, 0.75f);
	check(interior.wetness == 0.0f && interior.puddleWetness == 0.0f,
		"interiors must suppress rain and puddles");
	const auto beforeRain = CalculateWeatherWetness(true, 128, 0.49f, true);
	const auto duringRain = CalculateWeatherWetness(true, 128, 191.0f / 255.0f, true);
	check(beforeRain.wetness == 0.0f && beforeRain.puddleWetness == 0.0f && std::abs(duringRain.wetness - 0.5f) < 1e-6f && std::abs(duringRain.puddleWetness - 0.25f) < 1e-6f,
		"rain must start at the unsigned fade-in threshold and square puddle wetness");
	const auto beforeDrying = CalculateWeatherWetness(true, 127, 0.49f, false);
	const auto duringDrying = CalculateWeatherWetness(true, 127, 191.0f / 255.0f, false);
	check(beforeDrying.wetness == 1.0f && std::abs(duringDrying.wetness - 0.5f) < 1e-6f && std::abs(duringDrying.puddleWetness - std::pow(0.5f, 0.25f)) < 1e-6f && CalculateWeatherWetness(true, 255, 0.99f, false).wetness == 1.0f && CalculateWeatherWetness(true, 255, 1.0f, false).wetness == 0.0f,
		"last rain must use fade-out timing including the zero-width endpoint");
	check(CalculateWeatherWetness(true, 0, 0.1f, true).wetness == 0.0f && CalculateWeatherWetness(true, 0, 0.1001f, true).wetness == 1.0f,
		"zero fade-in must switch immediately only after ten percent");
	const auto overlap = ComputeWeatherWetness(true, true, 200, true, 200, 0.75f);
	const auto dry = ComputeWeatherWetness(true, false, 200, false, 200, 0.75f);
	check(overlap.wetness == 1.0f && overlap.puddleWetness == 1.0f && dry.wetness == 0.0f && dry.puddleWetness == 0.0f,
		"eligible weather contributions must sum and clamp without wetting dry weather");
	check(
		ComputeWeatherWetness(true, false, 0, true, 128, -4.0f).wetness == 0.0f && ComputeWeatherWetness(true, false, 0, true, 128, 4.0f).wetness == 1.0f,
		"transition progress must clamp");
	check(
		ComputeWeatherWetness(
			true,
			false,
			0,
			true,
			128,
			std::numeric_limits<float>::quiet_NaN())
				.wetness == 1.0f,
		"invalid transition progress must fail wet");

	return failures == 0 ? 0 : 1;
}

#include "WetnessMath.h"

#include <iostream>
#include <limits>

int main()
{
	using cs::features::wetness_math::ComputeWeatherWetness;
	int failures = 0;
	const auto check = [&](bool a_condition, const char* a_message) {
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	};

	check(
		ComputeWeatherWetness(false, true, true, 1.0f) == 0.0f,
		"interiors must remain dry");
	check(
		ComputeWeatherWetness(true, false, true, 0.25f) == 0.25f,
		"rain transition must publish its current wetness");
	check(
		ComputeWeatherWetness(true, true, false, 0.25f) == 0.75f,
		"drying transition must invert its progress");
	check(
		ComputeWeatherWetness(true, false, true, -4.0f) == 0.0f
			&& ComputeWeatherWetness(true, false, true, 4.0f) == 1.0f,
		"transition progress must clamp");
	check(
		ComputeWeatherWetness(
			true,
			false,
			true,
			std::numeric_limits<float>::quiet_NaN())
			== 1.0f,
		"invalid transition progress must fail wet");

	return failures == 0 ? 0 : 1;
}

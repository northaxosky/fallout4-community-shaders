#include "WetnessMath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
	int failures = 0;

	void Check(bool a_condition, std::string_view a_expression, int a_line)
	{
		if (!a_condition) {
			std::cerr << "CHECK failed at line " << a_line << ": " << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	void TestWeatherWetness()
	{
		using cs::features::wetness_math::ComputeWeatherWetness;

		CHECK(ComputeWeatherWetness(false, true, true, 1.0f) == 0.0f);
		CHECK(ComputeWeatherWetness(true, false, false, 1.0f) == 0.0f);
		CHECK(ComputeWeatherWetness(true, false, true, 1.0f) == 1.0f);
		CHECK(ComputeWeatherWetness(true, true, false, 0.0f) == 1.0f);
		CHECK(ComputeWeatherWetness(true, false, true, 0.25f) == 0.25f);
		CHECK(ComputeWeatherWetness(true, true, false, 0.25f) == 0.75f);
		// out-of-range transitions clamp instead of extrapolating
		CHECK(ComputeWeatherWetness(true, false, true, 4.0f) == 1.0f);
		CHECK(ComputeWeatherWetness(true, false, true, -4.0f) == 0.0f);
		CHECK(ComputeWeatherWetness(
				  true,
				  false,
				  true,
				  std::numeric_limits<float>::quiet_NaN()) == 1.0f);
	}

	void TestSettingsClamp()
	{
		using cs::features::wetness_math::Clamp;
		using cs::features::wetness_math::Settings;

		const Settings defaults;
		CHECK(defaults.enabled);
		CHECK(defaults.maxRainWetness == 1.0f);
		CHECK(defaults.minRainWetness == 0.65f);

		const auto clamped = Clamp({ .maxRainWetness = 9.0f, .minRainWetness = 9.0f });
		CHECK(clamped.maxRainWetness == 2.5f);
		CHECK(clamped.minRainWetness == 0.9f);

		const auto floored = Clamp({ .maxRainWetness = -1.0f, .minRainWetness = -1.0f });
		CHECK(floored.maxRainWetness == 0.0f);
		CHECK(floored.minRainWetness == 0.0f);

		const auto disabled = Clamp({
			.enabled = false,
			.maxRainWetness = 1.0f,
			.minRainWetness = 0.65f
		});
		CHECK(!disabled.enabled);
	}

	void TestPublishedWetness()
	{
		using cs::features::wetness_math::PublishedWetness;

		CHECK(PublishedWetness(false, 0.75f) == 0.0f);
		CHECK(PublishedWetness(true, 0.75f) == 0.75f);

		constexpr std::uint32_t payloadBits = 0x7FC01234u;
		const float payload = std::bit_cast<float>(payloadBits);
		CHECK(std::bit_cast<std::uint32_t>(PublishedWetness(true, payload))
			== payloadBits);
	}

}

int main()
{
	TestWeatherWetness();
	TestSettingsClamp();
	TestPublishedWetness();
	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "WetnessEffects math tests passed\n";
	return 0;
}

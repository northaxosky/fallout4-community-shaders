#include "InverseSquareLightingMath.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace
{
	int failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	void TestIdentityGuards()
	{
		using namespace cs::features::inverse_square_lighting;
		constexpr std::uint32_t vanillaBits = 0x3EAAAAABU;
		const float vanilla = std::bit_cast<float>(vanillaBits);

		Settings disabled;
		disabled.enabled = false;
		Check(
			std::bit_cast<std::uint32_t>(ApplyAttenuation(
				vanilla,
				std::numeric_limits<float>::quiet_NaN(),
				-1.0f,
				disabled,
				false))
				== vanillaBits,
			"disabled mode must preserve the exact native value");

		const Settings enabled;
		for (const auto [distance, radius] : {
				 std::pair{ -1.0f, 100.0f },
				 std::pair{
					 std::numeric_limits<float>::quiet_NaN(),
					 100.0f },
				 std::pair{
					 std::numeric_limits<float>::infinity(),
					 100.0f },
				 std::pair{ 1.0f, 0.0f },
				 std::pair{
					 1.0f,
					 std::numeric_limits<float>::quiet_NaN() } }) {
			Check(
				std::bit_cast<std::uint32_t>(ApplyAttenuation(
					vanilla, distance, radius, enabled, false))
					== vanillaBits,
				"malformed light inputs must preserve native attenuation");
		}

		auto malformed = enabled;
		malformed.exteriorStrength =
			std::numeric_limits<float>::quiet_NaN();
		Check(
			std::bit_cast<std::uint32_t>(ApplyAttenuation(
				vanilla, 1.0f, 100.0f, malformed, false))
				== vanillaBits,
			"malformed settings must preserve native attenuation");
	}

	void TestFalloffAndCutoff()
	{
		using namespace cs::features::inverse_square_lighting;
		const float nearField = kDefaultNearFieldDistance;
		const float radius = 1000.0f;
		const float source =
			PhysicalAttenuation(0.0f, radius, nearField);
		const float middle =
			PhysicalAttenuation(100.0f, radius, nearField);
		const float far =
			PhysicalAttenuation(500.0f, radius, nearField);
		Check(
			std::isfinite(source) && source > middle && middle > far,
			"physical attenuation must remain finite and monotonic");
		Check(
			PhysicalAttenuation(radius, radius, nearField) == 0.0f
				&& PhysicalAttenuation(
					radius + 1.0f, radius, nearField)
					== 0.0f,
			"radius cutoff must reach exact zero");
		Check(
			PhysicalAttenuation(
				radius - 0.001f, radius, nearField)
				< 1.0e-10f,
			"cutoff must remain continuous at the light radius");
	}
}

int main()
{
	TestIdentityGuards();
	TestFalloffAndCutoff();
	return failures == 0 ? 0 : 1;
}

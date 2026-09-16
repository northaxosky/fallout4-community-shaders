#include "InverseSquareLightingMath.h"

#include <algorithm>
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
			std::cerr << "CHECK failed at line " << a_line << ": "
					  << a_expression << '\n';
			++failures;
		}
	}

#define CHECK(a_expression) \
	Check(static_cast<bool>(a_expression), #a_expression, __LINE__)

	bool Near(float a_left, float a_right, float a_epsilon = 1.0e-5f)
	{
		return std::abs(a_left - a_right) <= a_epsilon;
	}

	void TestConstantsAndDefaults()
	{
		using namespace cs::features::inverse_square_lighting;

		const Settings defaults;
		CHECK(
			kInverseSquareScale
			== kScale * kUnitsPerMeter * kUnitsPerMeter);
		CHECK(kInverseSquareScale == 3920.0f);
		CHECK(kDefaultSourceSizeSquared == 2.0f);
		CHECK(kFadeBase == 4.5f * (kScale * kUnitsPerMeter));
		CHECK(Near(kFadeBase, 252.0f));
		CHECK(defaults.enabled);
		CHECK(defaults.exteriorStrength == 1.0f);
		CHECK(defaults.interiorStrength == 1.0f);
		CHECK(Near(
			defaults.nearFieldDistance * defaults.nearFieldDistance,
			kInverseSquareScale * kDefaultSourceSizeSquared * 0.5f,
			1.0e-3f));
		CHECK(Near(
			defaults.nearFieldDistance * defaults.nearFieldDistance,
			kInverseSquareScale,
			1.0e-3f));
		CHECK(Near(
			PhysicalAttenuation(
				0.0f, 1000.0f, defaults.nearFieldDistance),
			1.0f));
		CHECK(CutoffFadeWidth(100.0f) == 100.0f);
		CHECK(CutoffFadeWidth(1000.0f) == 252.0f);
	}

	void TestSettingsClamp()
	{
		using namespace cs::features::inverse_square_lighting;

		auto settings = Clamp({
			.enabled = false,
			.exteriorStrength = 3.0f,
			.interiorStrength = -2.0f,
			.nearFieldDistance = 9999.0f
		});
		CHECK(!settings.enabled);
		CHECK(settings.exteriorStrength == kStrengthMax);
		CHECK(settings.interiorStrength == kStrengthMin);
		CHECK(settings.nearFieldDistance == kNearFieldDistanceMax);

		settings = Clamp({ .nearFieldDistance = 0.0f });
		CHECK(settings.nearFieldDistance == kNearFieldDistanceMin);
		CHECK(settings.nearFieldDistance > 0.0f);

		settings = Clamp({
			.exteriorStrength = std::numeric_limits<float>::quiet_NaN(),
			.interiorStrength = std::numeric_limits<float>::infinity(),
			.nearFieldDistance = -std::numeric_limits<float>::infinity()
		});
		const Settings defaults;
		CHECK(settings.exteriorStrength == defaults.exteriorStrength);
		CHECK(settings.interiorStrength == defaults.interiorStrength);
		CHECK(settings.nearFieldDistance == defaults.nearFieldDistance);
		CHECK(std::isfinite(settings.nearFieldDistance));
	}

	void TestIdentityGuards()
	{
		using namespace cs::features::inverse_square_lighting;

		constexpr std::uint32_t vanillaBits = 0x3EAAAAABU;
		const float vanilla = std::bit_cast<float>(vanillaBits);
		Settings disabled;
		disabled.enabled = false;
		CHECK(std::bit_cast<std::uint32_t>(ApplyAttenuation(
				  vanilla,
				  std::numeric_limits<float>::quiet_NaN(),
				  -1.0f,
				  disabled,
				  false))
			== vanillaBits);

		Settings zeroStrength;
		zeroStrength.exteriorStrength = 0.0f;
		CHECK(std::bit_cast<std::uint32_t>(ApplyAttenuation(
				  vanilla,
				  std::numeric_limits<float>::infinity(),
				  0.0f,
				  zeroStrength,
				  false))
			== vanillaBits);

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
				 std::pair{ 1.0f, -1.0f },
				 std::pair{
					 1.0f,
					 std::numeric_limits<float>::quiet_NaN() },
				 std::pair{
					 1.0f,
					 std::numeric_limits<float>::infinity() } }) {
			CHECK(std::bit_cast<std::uint32_t>(ApplyAttenuation(
					  vanilla, distance, radius, enabled, false))
				== vanillaBits);
		}

		auto malformedSettings = enabled;
		malformedSettings.exteriorStrength =
			std::numeric_limits<float>::quiet_NaN();
		CHECK(std::bit_cast<std::uint32_t>(ApplyAttenuation(
				  vanilla, 1.0f, 100.0f, malformedSettings, false))
			== vanillaBits);
		malformedSettings = enabled;
		malformedSettings.nearFieldDistance = 0.0f;
		CHECK(std::bit_cast<std::uint32_t>(ApplyAttenuation(
				  vanilla, 1.0f, 100.0f, malformedSettings, false))
			== vanillaBits);
		malformedSettings.nearFieldDistance =
			std::numeric_limits<float>::quiet_NaN();
		CHECK(std::bit_cast<std::uint32_t>(ApplyAttenuation(
				  vanilla, 1.0f, 100.0f, malformedSettings, false))
			== vanillaBits);
	}

	void TestFalloffAndCutoff()
	{
		using namespace cs::features::inverse_square_lighting;

		const float nearField = kDefaultNearFieldDistance;
		const float radius = 1000.0f;
		const float atSource =
			PhysicalAttenuation(0.0f, radius, nearField);
		const float at100 =
			PhysicalAttenuation(100.0f, radius, nearField);
		const float at500 =
			PhysicalAttenuation(500.0f, radius, nearField);
		CHECK(std::isfinite(atSource));
		CHECK(atSource > at100);
		CHECK(at100 > at500);

		CHECK(PhysicalAttenuation(radius, radius, nearField) == 0.0f);
		CHECK(PhysicalAttenuation(radius + 1.0f, radius, nearField) == 0.0f);

		const float justInside =
			PhysicalAttenuation(radius - 0.001f, radius, nearField);
		CHECK(justInside >= 0.0f);
		CHECK(justInside < 1.0e-10f);
		CHECK(std::abs(
				  justInside
				  - PhysicalAttenuation(radius, radius, nearField))
			< 1.0e-10f);
	}

	void TestLocationSelection()
	{
		using namespace cs::features::inverse_square_lighting;

		const Settings settings{
			.enabled = true,
			.exteriorStrength = 0.8f,
			.interiorStrength = 0.25f,
			.nearFieldDistance = kDefaultNearFieldDistance
		};
		CHECK(SelectStrength(settings, false) == 0.8f);
		CHECK(SelectStrength(settings, true) == 0.25f);

		const float vanilla = 0.2f;
		const float physical =
			PhysicalAttenuation(100.0f, 1000.0f, settings.nearFieldDistance);
		CHECK(Near(
			ApplyAttenuation(vanilla, 100.0f, 1000.0f, settings, false),
			std::lerp(vanilla, physical, settings.exteriorStrength)));
		CHECK(Near(
			ApplyAttenuation(vanilla, 100.0f, 1000.0f, settings, true),
			std::lerp(vanilla, physical, settings.interiorStrength)));

		auto disabled = settings;
		disabled.enabled = false;
		CHECK(SelectStrength(disabled, false) == 0.0f);
		CHECK(SelectStrength(disabled, true) == 0.0f);
	}

}

int main()
{
	TestConstantsAndDefaults();
	TestSettingsClamp();
	TestIdentityGuards();
	TestFalloffAndCutoff();
	TestLocationSelection();
	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "InverseSquareLighting math tests passed\n";
	return 0;
}

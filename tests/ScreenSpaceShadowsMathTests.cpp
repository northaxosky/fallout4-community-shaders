#include "ScreenSpaceShadowsMath.h"

#include <iostream>
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

	void TestResolutionScaling()
	{
		using cs::features::sss_math::ScaleSampleCount;

		CHECK(ScaleSampleCount(1, 1920.0f, 1080.0f) == 64);
		CHECK(ScaleSampleCount(1, 2560.0f, 1440.0f) == 80);
		CHECK(ScaleSampleCount(1, 3840.0f, 2160.0f) == 120);
		CHECK(ScaleSampleCount(1, 3440.0f, 1440.0f) == 96);
		CHECK(ScaleSampleCount(4, 1920.0f, 1080.0f) == 240);
		CHECK(ScaleSampleCount(1, 0.0f, 0.0f) == 8);
	}

	void TestLegacyMigration()
	{
		using cs::features::sss_math::MigrateLegacyShadowLengthPercent;

		CHECK(MigrateLegacyShadowLengthPercent(0.5f) == 1);
		CHECK(MigrateLegacyShadowLengthPercent(2.0f) == 1);
		CHECK(MigrateLegacyShadowLengthPercent(10.0f) == 2);
		CHECK(MigrateLegacyShadowLengthPercent(15.0f) == 3);
	}
}

int main()
{
	TestResolutionScaling();
	TestLegacyMigration();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "ScreenSpaceShadows math tests passed\n";
	return 0;
}

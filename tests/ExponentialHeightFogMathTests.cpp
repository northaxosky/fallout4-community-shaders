#include "ExponentialHeightFogMath.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	int failures = 0;

#define CHECK(expr)                                                           \
	do {                                                                      \
		if (!(expr)) {                                                        \
			std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "      \
					  << #expr << '\n';                                       \
			++failures;                                                       \
		}                                                                     \
	} while (false)

	bool Near(float a_left, float a_right, float a_tolerance = 1.0e-5f)
	{
		return std::abs(a_left - a_right) <= a_tolerance;
	}

	void TestDistanceFit()
	{
		using namespace cs::features::exponential_height_fog;
		const auto fit = DeriveParameters(
			0.001f,
			1.0f,
			0.01f,
			-0.02f,
			10.0f,
			-40.0f,
			1.0f,
			1.0f);
		CHECK(fit.IsValid());
		CHECK(Near(fit.distanceNear, 1000.0f, 1.0e-3f));
		CHECK(Near(fit.distanceFar, 2000.0f, 1.0e-3f));
		CHECK(Near(EvaluateDistanceExtinction(fit, 999.0f), 0.0f));
		CHECK(Near(
			EvaluateDistanceExtinction(fit, fit.distanceFar),
			kReferenceExtinction,
			1.0e-6f));
		CHECK(EvaluateDistanceExtinction(fit, 3000.0f)
			> kReferenceExtinction);

		const auto stronger = DeriveParameters(
			0.001f,
			1.0f,
			0.01f,
			-0.02f,
			10.0f,
			-40.0f,
			2.0f,
			1.0f);
		CHECK(Near(stronger.density, fit.density * 2.0f));
	}

	void TestHeightFit()
	{
		using namespace cs::features::exponential_height_fog;
		const auto fit = DeriveParameters(
			0.001f,
			0.0f,
			0.01f,
			-0.02f,
			10.0f,
			-40.0f,
			1.0f,
			1.0f);
		CHECK(fit.IsValid());
		CHECK(Near(fit.heightZeroX, 1000.0f));
		CHECK(Near(fit.heightZeroY, 2000.0f));
		CHECK(fit.heightDirectionX == 1.0f);
		CHECK(fit.heightDirectionY == -1.0f);
		CHECK(Near(
			EvaluateHeightFactor(
				1100.0f,
				fit.heightZeroX,
				fit.heightDirectionX,
				fit.heightFalloffX),
			kReferenceExtinction,
			1.0e-6f));
		CHECK(Near(
			EvaluateHeightFactor(
				1950.0f,
				fit.heightZeroY,
				fit.heightDirectionY,
				fit.heightFalloffY),
			kReferenceExtinction,
			1.0e-6f));
	}

	void TestDegenerateFallbacks()
	{
		using namespace cs::features::exponential_height_fog;
		const auto make = [](float a_distanceScale,
							  float a_heightScaleX,
							  float a_heightScaleY) {
			return DeriveParameters(
				a_distanceScale,
				0.0f,
				a_heightScaleX,
				a_heightScaleY,
				0.0f,
				0.0f,
				1.0f,
				1.0f);
		};
		CHECK(make(0.0f, 1.0f, 1.0f).status
			== FitStatus::kDistanceSlopeNearZero);
		CHECK(make(-1.0f, 1.0f, 1.0f).status
			== FitStatus::kDistancePlaneOrder);
		CHECK(make(1.0f, 0.0f, 1.0f).status
			== FitStatus::kHeightSlopeXNearZero);
		CHECK(make(1.0f, 1.0f, 0.0f).status
			== FitStatus::kHeightSlopeYNearZero);
		CHECK(DeriveParameters(
			std::numeric_limits<float>::quiet_NaN(),
			0.0f,
			1.0f,
			1.0f,
			0.0f,
			0.0f,
			1.0f,
			1.0f).status == FitStatus::kNonFiniteDistanceRamp);
	}

}

int main()
{
	TestDistanceFit();
	TestHeightFit();
	TestDegenerateFallbacks();
	if (failures != 0)
		return 1;
	std::cout << "PASS: exponential height fog fit tests\n";
	return 0;
}

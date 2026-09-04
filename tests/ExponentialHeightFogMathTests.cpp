#include "ExponentialHeightFogMath.h"
#include "FeatureBuffer.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

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

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path, std::ios::binary);
		if (!stream) {
			std::cerr << "FAIL: cannot open " << a_path.string() << '\n';
			++failures;
			return {};
		}
		return {
			std::istreambuf_iterator<char>(stream),
			std::istreambuf_iterator<char>()
		};
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

	void TestFeatureBlockLayout()
	{
		using cs::ExponentialHeightFogFeatureData;
		using cs::FeatureDataCB;
		CHECK(sizeof(FeatureDataCB) == 160);
		CHECK(
			offsetof(FeatureDataCB, exponentialHeightFogSettings) == 144);
		CHECK(sizeof(ExponentialHeightFogFeatureData) == 16);
		CHECK(offsetof(ExponentialHeightFogFeatureData, Mode) == 0);
		CHECK(
			offsetof(ExponentialHeightFogFeatureData, DensityMultiplier) == 4);
		CHECK(
			offsetof(
				ExponentialHeightFogFeatureData,
				HeightFalloffMultiplier)
			== 8);
	}

	void TestShaderIdentityContract(
		const std::filesystem::path& a_compositePath,
		const std::filesystem::path& a_featurePath)
	{
		const auto composite = ReadFile(a_compositePath);
		const auto feature = ReadFile(a_featurePath);
		const auto cb47Family = composite.find(
			"#ifdef BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY");
		const auto fog2dFamily =
			composite.find("#ifdef BSDFCOMPOSITE_PS_2D_FOG");
		const auto cb47Include = composite.find(
			"#include \"ExponentialHeightFog/ExponentialHeightFog.hlsli\"",
			cb47Family);
		const auto fog2dInclude = composite.find(
			"#include \"ExponentialHeightFog/ExponentialHeightFog.hlsli\"",
			fog2dFamily);
		const auto cb47VanillaHeight =
			composite.find("float2 fogRemapPair =", cb47Family);
		const auto fog2dVanillaHeight = composite.find(
			"float2 fogRemapPair = saturate(fogPlaneDistance.xx",
			fog2dFamily);
		const auto cb47FeatureCall = composite.find(
			"ExponentialHeightFog::TryEvaluate(", cb47Family);
		const auto fog2dFeatureCall = composite.find(
			"ExponentialHeightFog::TryEvaluate(", fog2dFamily);
		const auto cb47VanillaDistance = composite.find(
			"float distancePow = pow(distanceFactor, FogNearLowColorAndPower.w);",
			cb47Family);
		const auto fog2dVanillaDistance = composite.find(
			"float distancePow   = pow(distanceFactor, FogNearLowColor_and_power.w);",
			fog2dFamily);
		CHECK(cb47Family < cb47Include);
		CHECK(cb47Include < cb47VanillaHeight);
		CHECK(cb47VanillaHeight < cb47FeatureCall);
		CHECK(cb47FeatureCall < cb47VanillaDistance);
		CHECK(fog2dFamily < fog2dInclude);
		CHECK(fog2dInclude < fog2dVanillaHeight);
		CHECK(fog2dVanillaHeight < fog2dFeatureCall);
		CHECK(fog2dFeatureCall < fog2dVanillaDistance);
		CHECK(feature.contains("if (!IsActive())"));
		CHECK(feature.contains("return false;"));
		CHECK(feature.contains("&& !SharedData::InInterior"));
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 3) {
		std::cerr
			<< "usage: ExponentialHeightFogMathTests <composite> <feature>\n";
		return 2;
	}
	TestDistanceFit();
	TestHeightFit();
	TestDegenerateFallbacks();
	TestFeatureBlockLayout();
	TestShaderIdentityContract(a_argv[1], a_argv[2]);
	if (failures != 0)
		return 1;
	std::cout << "PASS: exponential height fog fit and identity contracts\n";
	return 0;
}

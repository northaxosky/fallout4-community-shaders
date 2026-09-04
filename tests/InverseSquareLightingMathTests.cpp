#include "FeatureBuffer.h"
#include "InverseSquareLightingMath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
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

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path);
		if (!stream) {
			std::cerr << "FAIL: cannot open " << a_path.string() << '\n';
			++failures;
			return {};
		}
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		return buffer.str();
	}

	std::size_t Count(
		const std::string& a_text,
		std::string_view a_needle)
	{
		std::size_t count = 0;
		std::size_t position = 0;
		while ((position = a_text.find(a_needle, position))
			!= std::string::npos) {
			++count;
			position += a_needle.size();
		}
		return count;
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

	void TestFeatureBlockLayout()
	{
		using cs::FeatureDataCB;
		using cs::InverseSquareLightingFeatureData;

		CHECK(sizeof(FeatureDataCB) == 160);
		CHECK(
			offsetof(FeatureDataCB, inverseSquareLightingSettings) == 96);
		CHECK(sizeof(InverseSquareLightingFeatureData) == 16);
		CHECK(offsetof(InverseSquareLightingFeatureData, Mode) == 0);
		CHECK(
			offsetof(
				InverseSquareLightingFeatureData,
				ExteriorStrength)
			== 4);
		CHECK(
			offsetof(
				InverseSquareLightingFeatureData,
				InteriorStrength)
			== 8);
		CHECK(
			offsetof(
				InverseSquareLightingFeatureData,
				NearFieldDistance)
			== 12);
	}

	void TestShaderContract(
		const std::filesystem::path& a_featureShaderRoot,
		const std::filesystem::path& a_bsdfLightPath,
		const std::filesystem::path& a_sharedDataPath)
	{
		const auto helper = ReadFile(
			a_featureShaderRoot / "InverseSquareLighting"
				/ "InverseSquareLighting.hlsli");
		const auto bsdf = ReadFile(a_bsdfLightPath);
		const auto shared = ReadFile(a_sharedDataPath);
		if (helper.empty() || bsdf.empty() || shared.empty())
			return;

		CHECK(helper.find("static const float INVERSE_SQUARE_SCALE = 3920.0;")
			!= std::string::npos);
		CHECK(helper.find("static const float FADE_BASE = 252.0;")
			!= std::string::npos);
		CHECK(helper.find("if ((mode & MODE_ENABLED) == 0)")
			< helper.find("float denominator ="));
		CHECK(helper.find("if (!isfinite(strength) || strength <= 0.0)")
			< helper.find("float denominator ="));
		CHECK(helper.find("SharedData::InInterior ?")
			!= std::string::npos);
		CHECK(helper.find("SharedData::BufferDim.x")
			!= std::string::npos);
		CHECK(helper.find("* SharedData::DynamicResolution.x * 0.5")
			!= std::string::npos);
		CHECK(helper.find("distance * distance + nearFieldDistance * nearFieldDistance")
			!= std::string::npos);
		CHECK(helper.find("float fadeWidth = min(radius, FADE_BASE);")
			!= std::string::npos);

		constexpr std::string_view call =
			"InverseSquareLighting::GetAttenuation(";
		CHECK(Count(bsdf, call) == 5);
		CHECK(bsdf.find("#if LIGHT_TYPE == LIGHT_TYPE_POINT")
			< bsdf.find(call));
		CHECK(bsdf.find(
				  "attenuation, d, LightPos_and_Radius.w, input.position.x")
			!= std::string::npos);
		CHECK(bsdf.find(
				  "attenuation, distance, LightPos_and_Radius.w, input.position.x")
			!= std::string::npos);
		CHECK(bsdf.find(
				  "attenuation, sqrt(distSq), LightVector.w, input.position.x")
			!= std::string::npos);
		CHECK(bsdf.find(call)
			> bsdf.find("#if LIGHT_TYPE == LIGHT_TYPE_POINT"));
		CHECK(bsdf.rfind(call)
			> bsdf.find("#ifdef POINTOMNI", bsdf.find("#ifdef BSDFLIGHT_PS_UNSHADOWED")));

		CHECK(shared.find("struct InverseSquareLightingSettings")
			!= std::string::npos);
		CHECK(shared.find(
				  "InverseSquareLightingSettings inverseSquareLightingSettings;")
			!= std::string::npos);
	}

	void TestLiveSettingsContract(const std::filesystem::path& a_sourcePath)
	{
		const auto source = ReadFile(a_sourcePath);
		if (source.empty())
			return;

		CHECK(source.find("ImGui::Checkbox(\"Enabled\", &_settings.enabled)")
			!= std::string::npos);
		CHECK(source.find("\"Exterior strength\"")
			!= std::string::npos);
		CHECK(source.find(
				  "1.0 matches upstream's full effect; lower values blend ")
			!= std::string::npos);
		CHECK(source.find("\"Interior strength\"")
			!= std::string::npos);
		CHECK(source.find("\"Near-field distance (game units)\"")
			!= std::string::npos);
		CHECK(source.find(
				  "1.0 matches upstream's full effect; it remains a starting ")
			!= std::string::npos);
		CHECK(source.find(
				  "Lower values damp interior punctual lights if authored ")
			!= std::string::npos);
		CHECK(source.find(
				  "Matches upstream's default size sqrt(2); peak attenuation is 1.0.")
			!= std::string::npos);
		const auto changed = source.find("if (changed) {");
		CHECK(changed != std::string::npos);
		CHECK(source.find("PublishSettings();", changed)
			!= std::string::npos);
		CHECK(source.find("SaveSettings();", changed)
			!= std::string::npos);
		CHECK(source.find("RefreshDeliveryState") == std::string::npos);
		CHECK(source.find("forcing vanilla") == std::string::npos);
		CHECK(source.find(
				  "only, rendering remains unchanged.")
			!= std::string::npos);
	}
}

int main(int a_argc, char* a_argv[])
{
	TestConstantsAndDefaults();
	TestSettingsClamp();
	TestIdentityGuards();
	TestFalloffAndCutoff();
	TestLocationSelection();
	TestFeatureBlockLayout();
	if (a_argc == 5) {
		TestShaderContract(a_argv[1], a_argv[2], a_argv[3]);
		TestLiveSettingsContract(a_argv[4]);
	} else {
		std::cerr << "FAIL: expected feature shader, BSDF, shared data, and source paths\n";
		++failures;
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "InverseSquareLighting math and source-contract tests passed\n";
	return 0;
}

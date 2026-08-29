#include "FeatureBuffer.h"
#include "WetnessMath.h"

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

	void TestFeatureBlockLayout()
	{
		using cs::FeatureDataCB;
		using cs::WetnessEffectsFeatureData;

		CHECK(sizeof(FeatureDataCB) == 96);
		CHECK(offsetof(FeatureDataCB, wetnessEffectsSettings) == 32);
		CHECK(sizeof(WetnessEffectsFeatureData) == 16);
		CHECK(offsetof(WetnessEffectsFeatureData, Wetness) == 0);
		CHECK(offsetof(WetnessEffectsFeatureData, MaxRainWetness) == 4);
		CHECK(offsetof(WetnessEffectsFeatureData, MinRainWetness) == 8);
		CHECK(offsetof(WetnessEffectsFeatureData, pad0) == 12);

		const WetnessEffectsFeatureData zeroed;
		CHECK(zeroed.Wetness == 0.0f);
		CHECK(zeroed.MaxRainWetness == 0.0f);
		CHECK(zeroed.MinRainWetness == 0.0f);
	}

	constexpr float kFilmF0 = 0.02f;
	constexpr float kMaxFilmSpecularMagnitude = 15.0f;
	constexpr float kPi = 3.1415927f;

	float DirectFilmDistribution(float a_roughness, float a_ndotH)
	{
		const float roughnessSquared = a_roughness * a_roughness;
		const float alphaSquared = roughnessSquared * roughnessSquared;
		const float denominator =
			a_ndotH * a_ndotH * (alphaSquared - 1.0f) + 1.0f;
		return alphaSquared / (kPi * denominator * denominator);
	}

	float DirectFilmVisibility(
		float a_roughness,
		float a_ndotV,
		float a_ndotL)
	{
		const float roughnessSquared = a_roughness * a_roughness;
		const float visibilityV = a_ndotL *
			(a_ndotV * (1.0f + roughnessSquared) + roughnessSquared);
		const float visibilityL = a_ndotV *
			(a_ndotL * (1.0f + roughnessSquared) + roughnessSquared);
		return 0.5f / std::max(visibilityV + visibilityL, 1e-6f);
	}

	float DirectFilmFresnel(float a_vdotH)
	{
		const float fresnel = std::pow(1.0f - a_vdotH, 5.0f);
		return fresnel + (1.0f - fresnel) * kFilmF0;
	}

	float UncappedDirectFilmBrdf(
		float a_roughness,
		float a_ndotH,
		float a_ndotV,
		float a_ndotL,
		float a_vdotH)
	{
		return DirectFilmDistribution(a_roughness, a_ndotH) *
			DirectFilmVisibility(a_roughness, a_ndotV, a_ndotL) *
			DirectFilmFresnel(a_vdotH);
	}

	float CappedDirectFilmBrdf(
		float a_roughness,
		float a_ndotH,
		float a_ndotV,
		float a_ndotL,
		float a_vdotH)
	{
		return std::min(
			UncappedDirectFilmBrdf(
				a_roughness,
				a_ndotH,
				a_ndotV,
				a_ndotL,
				a_vdotH),
			kMaxFilmSpecularMagnitude);
	}

	void TestDirectFilmCap()
	{
		const float narrowPeak =
			UncappedDirectFilmBrdf(0.05f, 1.0f, 1.0f, 1.0f, 1.0f);
		CHECK(narrowPeak > kMaxFilmSpecularMagnitude);
		CHECK(CappedDirectFilmBrdf(0.05f, 1.0f, 1.0f, 1.0f, 1.0f)
			== kMaxFilmSpecularMagnitude);

		const float midRange =
			UncappedDirectFilmBrdf(0.5f, 0.5f, 1.0f, 1.0f, 0.5f);
		CHECK(midRange < kMaxFilmSpecularMagnitude);
		CHECK(CappedDirectFilmBrdf(0.5f, 0.5f, 1.0f, 1.0f, 0.5f)
			== midRange);
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

	bool Contains(const std::string& a_text, std::string_view a_needle)
	{
		return a_text.find(a_needle) != std::string::npos;
	}

	// the helper's identity guards are the shader-side contract this feature rests on
	void TestShaderContract(const std::filesystem::path& a_shaderRoot)
	{
		const auto source = ReadFile(
			a_shaderRoot / "WetnessEffects" / "WetnessEffects.hlsli");
		if (source.empty())
			return;

		// t25 belongs to composite consumers only
		CHECK(Contains(source, "#ifdef WETNESS_COMPOSITE_CONSUMER"));
		CHECK(Contains(source, "Texture2D<float4> GbufferNormal : register(t25);"));
		CHECK(!Contains(source, "Texture2D<float>"));
		CHECK(!Contains(source, "EnableWetness"));

		// encode-domain guard, written so NaN fails it too
		CHECK(Contains(source, "[branch] if (encodedLengthSquared <= 4.0) {"));

		// wetness zero is exact identity on every helper
		CHECK(Contains(source, "static const float MinFilmRoughness = 0.05;"));
		CHECK(Contains(source, "static const float FilmF0 = 0.02;"));
		CHECK(Contains(source, "static const float FilmSpecularScale = 3.1415927;"));
		CHECK(Contains(source, "float3 wetColor = baseColor;"));
		CHECK(Contains(source, "float weight = 0.0;"));
		CHECK(Contains(source, "float wetness = 0.0;"));

		// native FO4 caps the uncoupled D*G*F lobe before strength and pi
		CHECK(Contains(
			source,
			"static const float MaxFilmSpecularMagnitude = 15.0;"));
		const auto fresnel = source.find(
			"float fresnel = F_Schlick(FilmF0, VdotH);");
		const auto filmFresnel = source.find(
			"float filmFresnel = fresnel * strength;");
		const auto filmBrdf = source.find(
			"float filmBrdf = min(D * G * fresnel, MaxFilmSpecularMagnitude);");
		const auto film = source.find(
			"filmBrdf * strength * NdotL * lightColor * FilmSpecularScale;");
		CHECK(fresnel != std::string::npos);
		CHECK(filmFresnel != std::string::npos);
		CHECK(filmBrdf != std::string::npos);
		CHECK(film != std::string::npos);
		CHECK(fresnel < filmFresnel);
		CHECK(filmFresnel < filmBrdf);
		CHECK(filmBrdf < film);
		CHECK(Contains(source, "diffuse *= 1.0 - filmFresnel;"));
		CHECK(Contains(source, "specular *= 1.0 - filmFresnel;"));
		CHECK(Contains(source, "specular += film;"));

		// partial wetness must not blur a more polished native reflection
		CHECK(Contains(source, "return min(FilmRoughness(wetness), nativeRoughness);"));
	}

	void TestRuntimeToggleContract(const std::filesystem::path& a_sourcePath)
	{
		const auto source = ReadFile(a_sourcePath);
		if (source.empty())
			return;

		CHECK(Contains(source, "ReadBool("));
		CHECK(Contains(source, "\"enabled\", a_candidate.enabled"));
		CHECK(Contains(source, "settings.insert_or_assign(\"enabled\", _settings.enabled);"));
		CHECK(Contains(source, "ImGui::Checkbox(\"Enabled\", &_settings.enabled)"));
		CHECK(Contains(source, "wetness_math::PublishedWetness("));
		CHECK(Contains(source, ".Field(\"enabled\", _settings.enabled)"));
		CHECK(Contains(source, "\"weather_wetness\""));
	}
}

int main(int a_argc, char* a_argv[])
{
	TestWeatherWetness();
	TestSettingsClamp();
	TestPublishedWetness();
	TestFeatureBlockLayout();
	TestDirectFilmCap();
	if (a_argc == 3) {
		TestShaderContract(std::filesystem::path(a_argv[1]));
		TestRuntimeToggleContract(std::filesystem::path(a_argv[2]));
	} else {
		std::cerr << "FAIL: expected shader root and feature source arguments\n";
		++failures;
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "WetnessEffects math and shader-contract tests passed\n";
	return 0;
}

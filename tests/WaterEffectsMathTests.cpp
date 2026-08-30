#include "FeatureBuffer.h"
#include "WaterEffectsMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

	std::size_t Count(const std::string& a_text, std::string_view a_needle)
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

	using namespace cs::features::water_effects;

	void TestUpstreamConstants()
	{
		CHECK(Near(kShoreRange, 64.0f));
		CHECK(Near(kFadeRange, 1024.0f));
		CHECK(Near(kDispersionRange, 256.0f));
		CHECK(Near(kUvScale, 0.005f));
		CHECK(Near(kDispersionScale, 0.025f));
		CHECK(Near(kDispersionDirection[0], 0.6f));
		CHECK(Near(kDispersionDirection[1], 0.8f));
		CHECK(Near(kCausticsGain, 4.0f));
		CHECK(kNoWaterHeight < -1.0e8f);

		Settings settings;
		CHECK(settings.enabled);
		CHECK(Clamp(settings).enabled);
	}

	void TestShoreRamp()
	{
		CHECK(Near(ShoreFactor(-10.0f), 0.0f));
		CHECK(Near(ShoreFactor(0.0f), 0.0f));
		CHECK(Near(ShoreFactor(32.0f), 0.5f));
		CHECK(Near(ShoreFactor(64.0f), 1.0f));
		CHECK(Near(ShoreFactor(4096.0f), 1.0f));
	}

	void TestSquaredFade()
	{
		CHECK(Near(CausticsFade(0.0f), 1.0f));
		// Squared, not linear: half the range keeps a quarter of the weight.
		CHECK(Near(CausticsFade(512.0f), 0.25f));
		CHECK(Near(CausticsFade(1024.0f), 0.0f));
		CHECK(Near(CausticsFade(8192.0f), 0.0f));
	}

	void TestDispersion()
	{
		const auto zero = DispersionOffset(0.0f, 0.0f);
		CHECK(Near(zero[0], 0.0f) && Near(zero[1], 0.0f));

		const auto full = DispersionOffset(kDispersionRange, 1.0f);
		CHECK(Near(full[0], 0.6f * 0.025f));
		CHECK(Near(full[1], 0.8f * 0.025f));

		// The centre channel is the untouched tap; that is what the scalar
		// light path consumes, so debug and production cannot disagree.
		const auto spread = Dispersion(0.2f, 0.7f, 0.9f);
		CHECK(Near(spread[1], 0.7f));
		CHECK(Near(spread[0], 0.45f));
		CHECK(Near(spread[2], 0.8f));
	}

	void TestPan()
	{
		const auto panned = PanCausticsUV({ 0.25f, 0.75f }, 0.1f, 1.0f, 3.0f);
		CHECK(Near(panned[0], 0.55f));
		// Only x carries the timer, matching upstream's float2(1, 0) mask.
		CHECK(Near(panned[1], 0.75f));

		const auto wrapped = PanCausticsUV({ 4.25f, -0.25f }, 0.0f, 1.0f, 0.0f);
		CHECK(wrapped[0] >= 0.0f && wrapped[0] < 1.0f);
		CHECK(wrapped[1] >= 0.0f && wrapped[1] < 1.0f);
		CHECK(Near(wrapped[1], 0.75f));
	}

	void TestBilinearWrap()
	{
		constexpr std::int32_t width = 4;
		constexpr std::int32_t height = 4;
		const auto load = [](std::int32_t a_x, std::int32_t a_y) {
			return static_cast<float>(a_x + a_y);
		};

		// Texel centres reproduce the stored value exactly.
		CHECK(Near(BilinearWrap(load, { 1.5f / width, 2.5f / height }, width, height), 3.0f));
		// Midway between two texels is their average.
		CHECK(Near(BilinearWrap(load, { 2.0f / width, 2.5f / height }, width, height), 3.5f));
		// Out-of-range UVs wrap instead of clamping.
		CHECK(Near(
			BilinearWrap(load, { 1.5f / width, 2.5f / height }, width, height),
			BilinearWrap(load, { 1.5f / width + 1.0f, 2.5f / height }, width, height)));
		CHECK(Near(BilinearWrap(load, { 0.5f, 0.5f }, 0, 0), 1.0f));
	}

	void TestCausticsMultiplier()
	{
		const auto flat = [](std::array<float, 2>) { return 0.25f; };

		// Above the plane the effect is identity.
		CHECK(Near(ComputeCausticsMult(0.0f, { 0.0f, 0.0f, 10.0f }, 0.0f, flat), 1.0f));
		CHECK(Near(ComputeCausticsMult(0.0f, { 0.0f, 0.0f, 0.0f }, 0.0f, flat), 1.0f));

		// Fully submerged past the shore ramp: min(0.25, 0.25) * 4 == 1.
		CHECK(Near(ComputeCausticsMult(0.0f, { 0.0f, 0.0f, -200.0f }, 0.0f, flat), 1.0f));

		const auto bright = [](std::array<float, 2>) { return 0.5f; };
		// Half the shore ramp blends halfway toward the caustics value.
		const float half =
			ComputeCausticsMult(0.0f, { 0.0f, 0.0f, -32.0f }, 0.0f, bright);
		CHECK(Near(half, Lerp(1.0f, 2.0f, 0.5f), 1.0e-4f));

		// Deeper than the fade range the low layer alone survives.
		const float deep =
			ComputeCausticsMult(0.0f, { 0.0f, 0.0f, -4096.0f }, 0.0f, bright);
		CHECK(Near(deep, 2.0f, 1.0e-4f));
	}

	void TestWorldLock()
	{
		// The same world position must produce the same UV regardless of where
		// the camera is: the port folds the origin in once, upstream twice.
		std::array<std::array<float, 2>, 2> seen{};
		std::size_t index = 0;
		const auto record = [&](std::array<float, 2> a_uv) {
			if (index < seen.size())
				seen[index++] = a_uv;
			return 0.25f;
		};
		ComputeCausticsMult(0.0f, { 1000.0f, 2000.0f, -500.0f }, 0.0f, record);
		const auto first = seen[0];

		index = 0;
		seen = {};
		ComputeCausticsMult(0.0f, { 1000.0f, 2000.0f, -500.0f }, 0.0f, record);
		CHECK(Near(seen[0][0], first[0]));
		CHECK(Near(seen[0][1], first[1]));
	}

	void TestFeatureBlockLayout()
	{
		using cs::FeatureDataCB;
		using cs::WaterEffectsFeatureData;

		static_assert(sizeof(WaterEffectsFeatureData) == 16);
		static_assert(offsetof(WaterEffectsFeatureData, Mode) == 0);
		static_assert(offsetof(WaterEffectsFeatureData, HasWater) == 4);
		static_assert(offsetof(WaterEffectsFeatureData, WaterHeight) == 8);
		static_assert(offsetof(FeatureDataCB, waterEffectsSettings) == 112);
		static_assert(sizeof(FeatureDataCB) == 128);

		const WaterEffectsFeatureData data{};
		CHECK(data.Mode == 0);
		CHECK(data.HasWater == 0);
		CHECK(Near(data.WaterHeight, 0.0f));
	}

	void TestShaderContract(
		const std::filesystem::path& a_hlsliPath,
		const std::filesystem::path& a_bsdfLightPath,
		const std::filesystem::path& a_bsdfCompositePath,
		const std::filesystem::path& a_sharedDataPath)
	{
		const auto hlsli = ReadFile(a_hlsliPath);
		if (hlsli.empty())
			return;

		CHECK(hlsli.find("Texture2D<float4> WaterCaustics : register(t32)")
			!= std::string::npos);
		CHECK(hlsli.find("Texture2D<float> SceneDepthTexture : register(t33)")
			!= std::string::npos);
		// s14 is taken in the composite, so the sampler must not be declared in
		// the shared header at all.
		CHECK(hlsli.find("register(s14)") == std::string::npos);

		// Upstream re-adds CameraPosAdjust because its world position is
		// camera-relative. Ours is absolute, so a second add would double the
		// pan rate and make caustics swim with the camera.
		const auto uv = hlsli.find("float2 causticsUV = ");
		CHECK(uv != std::string::npos);
		const auto uvEnd = hlsli.find(';', uv);
		CHECK(uvEnd != std::string::npos);
		CHECK(hlsli.substr(uv, uvEnd - uv).find("CameraPosAdjust")
			== std::string::npos);
		CHECK(hlsli.find("worldPosition.xy * UV_SCALE") != std::string::npos);

		CHECK(hlsli.find("static const float SHORE_RANGE = 64.0;")
			!= std::string::npos);
		CHECK(hlsli.find("static const float FADE_RANGE = 1024.0;")
			!= std::string::npos);
		CHECK(hlsli.find("static const float UV_SCALE = 0.005;")
			!= std::string::npos);
		CHECK(hlsli.find("static const float CAUSTICS_GAIN = 4.0;")
			!= std::string::npos);
		// Upstream reconstruction contract: three rows plus the origin.
		CHECK(hlsli.find("dot(viewToWorldRow2, positionView) + cameraPosAdjust.z")
			!= std::string::npos);

		const auto bsdfLight = ReadFile(a_bsdfLightPath);
		if (bsdfLight.empty())
			return;

		CHECK(Count(
				  bsdfLight,
				  "WaterEffects::GetCausticsMultFromViewPosition")
			== 7);
		CHECK(bsdfLight.find("#if defined(DIRECTIONAL) && defined(WATER_EFFECTS)")
			!= std::string::npos);
		// The sampler is light-path only; the composite's s14 is g_sLitScene.
		CHECK(bsdfLight.find(
				  "#include \"WaterEffects/WaterCausticsSampler.hlsli\"")
			!= std::string::npos);
		// Caustics modulate the sun term before directional shadowing, which is
		// upstream's ordering.
		const auto firstCaustics =
			bsdfLight.find("WaterEffects::GetCausticsMultFromViewPosition");
		const auto firstShadow = bsdfLight.find("GetWorldShadow");
		CHECK(firstShadow == std::string::npos || firstCaustics < firstShadow);
		CHECK(bsdfLight.find("WaterEffects::TryGetDebugColor")
			== std::string::npos);

		const auto bsdfComposite = ReadFile(a_bsdfCompositePath);
		if (bsdfComposite.empty())
			return;

		CHECK(Count(
				  bsdfComposite,
				  "WaterEffects::TryGetDebugColorFromViewPosition")
			== 9);
		CHECK(Count(
				  bsdfComposite,
				  "WaterEffects::TryGetDebugColorFromScreenPosition")
			== 7);
		// The mandated shape: a local float4 and an immediate return, so an
		// inactive helper cannot erase an earlier active one.
		CHECK(Count(bsdfComposite, "float4 waterDebugColor;") == 16);
		CHECK(bsdfComposite.find("output.color = waterDebugColor;")
			!= std::string::npos);
		CHECK(bsdfComposite.find("return waterDebugColor;")
			!= std::string::npos);
		CHECK(bsdfComposite.find("#include \"WaterEffects/WaterCaustics.hlsli\"")
			!= std::string::npos);
		CHECK(bsdfComposite.find("WaterCausticsSampler") == std::string::npos);

		const auto sharedData = ReadFile(a_sharedDataPath);
		if (sharedData.empty())
			return;

		CHECK(sharedData.find("struct WaterEffectsSettings") != std::string::npos);
		const auto cbuffer = sharedData.find("cbuffer FeatureData");
		CHECK(cbuffer != std::string::npos);
		const auto member =
			sharedData.find("waterEffectsSettings;", cbuffer);
		CHECK(member != std::string::npos);
		// The block must stay last so the b6 offsets above remain pinned.
		CHECK(sharedData.find("Settings ", member) == std::string::npos);
	}

	void TestLiveSettingsContract(const std::filesystem::path& a_sourcePath)
	{
		const auto source = ReadFile(a_sourcePath);
		if (source.empty())
			return;

		CHECK(source.find("ImGui::Checkbox(\"Enabled\", &_settings.enabled)")
			!= std::string::npos);
		// Upstream ships no settings at all; inventing knobs here would repeat
		// the invented interior-strength default.
		CHECK(source.find("\"strength\"") == std::string::npos);
		CHECK(source.find("\"shore_range\"") == std::string::npos);
		CHECK(source.find("Menu::Get().DrawDebugViewSelector(*this)")
			!= std::string::npos);
		// The composite debug fetch has no mip selection, so it cannot show the
		// light path's mip seams; the caveat has to be visible in the UI.
		CHECK(source.find("mip") != std::string::npos);
	}
}

int main(int a_argc, char* a_argv[])
{
	TestUpstreamConstants();
	TestShoreRamp();
	TestSquaredFade();
	TestDispersion();
	TestPan();
	TestBilinearWrap();
	TestCausticsMultiplier();
	TestWorldLock();
	TestFeatureBlockLayout();
	if (a_argc == 6) {
		TestShaderContract(a_argv[1], a_argv[2], a_argv[3], a_argv[4]);
		TestLiveSettingsContract(a_argv[5]);
	} else {
		std::cerr << "FAIL: expected caustics hlsli, BSDF light, BSDF "
					 "composite, shared data, and source paths\n";
		++failures;
	}

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "WaterEffects math and source-contract tests passed\n";
	return 0;
}

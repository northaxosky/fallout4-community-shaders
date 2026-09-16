#include "WaterEffectsMath.h"

#include <array>
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

		// The center channel is the untouched tap; that is what the scalar
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

		// Texel centers reproduce the stored value exactly.
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
		// Two cameras observing the same world point must produce the same UV.
		// Upstream re-adds the origin here; if that line were ported verbatim
		// the UVs would diverge by the camera delta and caustics would swim.
		const std::array<float, 3> world{ 1000.0f, 2000.0f, -500.0f };
		const std::array<float, 4> row0{ 1.0f, 0.0f, 0.0f, 0.0f };
		const std::array<float, 4> row1{ 0.0f, 1.0f, 0.0f, 0.0f };
		const std::array<float, 4> row2{ 0.0f, 0.0f, 1.0f, 0.0f };

		const auto uvSeenFrom = [&](std::array<float, 3> a_camera) {
			const std::array<float, 3> view{
				world[0] - a_camera[0],
				world[1] - a_camera[1],
				world[2] - a_camera[2]
			};
			const auto reconstructed =
				ViewToWorldPosition(view, row0, row1, row2, a_camera);
			CHECK(Near(reconstructed[0], world[0], 1.0e-2f));
			CHECK(Near(reconstructed[1], world[1], 1.0e-2f));

			std::array<float, 2> uv{};
			bool captured = false;
			const auto record = [&](std::array<float, 2> a_uv) {
				if (!captured) {
					uv = a_uv;
					captured = true;
				}
				return 0.25f;
			};
			ComputeCausticsMult(0.0f, reconstructed, 0.0f, record);
			CHECK(captured);
			return uv;
		};

		const auto near = uvSeenFrom({ 900.0f, 1900.0f, 100.0f });
		const auto far = uvSeenFrom({ -40000.0f, 65000.0f, 3000.0f });
		CHECK(Near(near[0], far[0], 1.0e-3f));
		CHECK(Near(near[1], far[1], 1.0e-3f));
	}

	// Worldspace-inherited cells store a sentinel, not a usable plane.
	void TestWaterHeightSanitization()
	{
		CHECK(IsUsableWaterHeight(0.0f));
		CHECK(IsUsableWaterHeight(-4096.0f));
		CHECK(!IsUsableWaterHeight(kNoWaterHeight));
		CHECK(!IsUsableWaterHeight(-3.4e38f));
		CHECK(!IsUsableWaterHeight(
			std::numeric_limits<float>::quiet_NaN()));
		CHECK(!IsUsableWaterHeight(
			std::numeric_limits<float>::infinity()));
	}

}

int main()
{
	TestUpstreamConstants();
	TestShoreRamp();
	TestSquaredFade();
	TestDispersion();
	TestPan();
	TestBilinearWrap();
	TestCausticsMultiplier();
	TestWorldLock();
	TestWaterHeightSanitization();
	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "WaterEffects math tests passed\n";
	return 0;
}

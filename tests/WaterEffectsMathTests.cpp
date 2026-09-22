#include "WaterEffectsMath.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
	using namespace cs::features::water_effects;
	int failures = 0;

	void Check(bool a_condition, const char* a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	bool Near(float a_left, float a_right, float a_epsilon = 1.0e-4f)
	{
		return std::abs(a_left - a_right) <= a_epsilon;
	}

	void TestCausticsMultiplier()
	{
		const auto flat = [](std::array<float, 2>) { return 0.25f; };
		Check(
			Near(ComputeCausticsMult(
				0.0f, { 0.0f, 0.0f, 10.0f }, 0.0f, flat), 1.0f),
			"geometry above water must remain unchanged");
		Check(
			Near(ComputeCausticsMult(
				0.0f, { 0.0f, 0.0f, -200.0f }, 0.0f, flat), 1.0f),
			"fully submerged flat samples must remain stable");

		const auto bright = [](std::array<float, 2>) { return 0.5f; };
		Check(
			Near(ComputeCausticsMult(
				0.0f, { 0.0f, 0.0f, -32.0f }, 0.0f, bright), 1.5f),
			"shore transition must blend toward caustics");
		Check(
			Near(ComputeCausticsMult(
				0.0f, { 0.0f, 0.0f, -4096.0f }, 0.0f, bright), 2.0f),
			"deep water must retain the low-frequency caustics layer");
	}

	void TestWorldLock()
	{
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
			std::array<float, 2> uv{};
			ComputeCausticsMult(
				0.0f,
				reconstructed,
				0.0f,
				[&](std::array<float, 2> a_uv) {
					uv = a_uv;
					return 0.25f;
				});
			return uv;
		};

		const auto near = uvSeenFrom({ 900.0f, 1900.0f, 100.0f });
		const auto far = uvSeenFrom({ -40000.0f, 65000.0f, 3000.0f });
		Check(
			Near(near[0], far[0], 1.0e-3f)
				&& Near(near[1], far[1], 1.0e-3f),
			"caustics must remain locked to world space across camera motion");
	}

	void TestWaterHeightSanitization()
	{
		Check(
			IsUsableWaterHeight(0.0f)
				&& IsUsableWaterHeight(-4096.0f),
			"finite water planes must remain usable");
		Check(
			!IsUsableWaterHeight(kNoWaterHeight)
				&& !IsUsableWaterHeight(-3.4e38f)
				&& !IsUsableWaterHeight(
					std::numeric_limits<float>::quiet_NaN())
				&& !IsUsableWaterHeight(
					std::numeric_limits<float>::infinity()),
			"sentinel and non-finite water planes must be rejected");
	}
}

int main()
{
	TestCausticsMultiplier();
	TestWorldLock();
	TestWaterHeightSanitization();
	return failures == 0 ? 0 : 1;
}

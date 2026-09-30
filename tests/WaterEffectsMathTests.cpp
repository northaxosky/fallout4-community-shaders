#include "World/WaterData.h"

#include <cmath>
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

	void TestCellSelection()
	{
		using cs::engine::WaterTileIndex;
		Check(WaterTileIndex(-1, -1, -0.5f, -0.5f) == 12,
			"negative coordinates must floor into the camera cell");
		Check(WaterTileIndex(-3, 1, -0.5f, -0.5f) == 20,
			"loaded cells must map x fastest into the 5x5 table");
		Check(WaterTileIndex(2, -1, -0.5f, -0.5f) == -1,
			"cells outside the eye-centred table must not overwrite edge tiles");
		Check(WaterTileIndex(0, 0, 4095.5f, 1.0f) == 12 &&
				  WaterTileIndex(0, 0, 4096.5f, 1.0f) == 11,
			"crossing a cell border must reindex the same loaded cell");
	}

	void TestRelativeHeight()
	{
		using namespace cs::engine;
		const float absoluteWater = 200.0f, absoluteSurface = 150.0f;
		const auto distance = [&](float a_origin) {
			return RelativeWaterHeight(absoluteWater, a_origin) - (absoluteSurface - a_origin);
		};
		Check(distance(-6000.0f) == 50.0f && distance(8000.0f) == 50.0f,
			"camera motion must preserve submersion distance");
		Check(RelativeWaterHeight(kNoWaterHeight, 1000.0f) == kNoWaterHeight &&
				  RelativeWaterHeight(std::numeric_limits<float>::quiet_NaN(), 1000.0f) == kNoWaterHeight &&
				  RelativeWaterHeight(std::numeric_limits<float>::infinity(), 1000.0f) == kNoWaterHeight,
			"engine absent/nonfinite water must remain inert");
	}

	void TestPackedColor()
	{
		using cs::engine::WaterColorChannel;
		Check(std::abs(WaterColorChannel(0x00102040, 0x00806020, 0) - 48.0f / 255.0f) < 1.0e-6f &&
				  std::abs(WaterColorChannel(0x00102040, 0x00806020, 8) - 64.0f / 255.0f) < 1.0e-6f &&
				  std::abs(WaterColorChannel(0x00102040, 0x00806020, 16) - 72.0f / 255.0f) < 1.0e-6f,
			"water-form bytes must average shallow/deep RGB without a gamma conversion");
	}
}

int main()
{
	TestCellSelection();
	TestRelativeHeight();
	TestPackedColor();
	return failures == 0 ? 0 : 1;
}

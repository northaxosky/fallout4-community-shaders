#include "TerrainShadowsMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
	namespace ts = cs::features::terrain_shadows;

	int failures = 0;

	void Check(bool a_condition, std::string_view a_message)
	{
		if (!a_condition) {
			std::cerr << "FAIL: " << a_message << '\n';
			++failures;
		}
	}

	void CheckNear(
		double a_actual,
		double a_expected,
		double a_tolerance,
		std::string_view a_message)
	{
		if (!std::isfinite(a_actual)
			|| std::abs(a_actual - a_expected) > a_tolerance) {
			std::cerr << "FAIL: " << a_message << " (actual " << a_actual
					  << ", expected " << a_expected << ")\n";
			++failures;
		}
	}

	void TestXLodGenParsing()
	{
		const auto parsed = ts::ParseHeightMapStem(
			"Commonwealth.Terrain.HeightMap.-64.-64.63.63.-100.400",
			ts::HeightMapSource::kXLodGen);
		Check(parsed.has_value(), "xLODGen stem parses");
		if (!parsed)
			return;
		Check(parsed->worldspace == "Commonwealth", "worldspace is field zero");
		CheckNear(parsed->pos0[0], -64.0 * 4096.0, 1e-3, "west edge");
		CheckNear(parsed->pos1[0], 64.0 * 4096.0, 1e-3, "east edge is exclusive+1");
		CheckNear(parsed->pos1[1], -64.0 * 4096.0, 1e-3, "south edge");
		CheckNear(parsed->pos0[1], 64.0 * 4096.0, 1e-3, "north edge is exclusive+1");
		CheckNear(parsed->pos0[2], -32767.0 * 8.0, 1e-3, "sample 0 maps to -32767*8");
		CheckNear(parsed->pos1[2], 32767.0 * 8.0, 1e-3, "sample 1 maps to 32767*8");
		CheckNear(parsed->zRange[0], -800.0, 1e-3, "min z is quantized by 8");
		CheckNear(parsed->zRange[1], 3200.0, 1e-3, "max z is quantized by 8");

		const double normalized = 32767.0 / 65535.0;
		const double decoded = parsed->pos0[2]
			+ normalized * (parsed->pos1[2] - parsed->pos0[2]);
		CheckNear(decoded, 0.0, 4.0, "the xLODGen midpoint decodes to game Z zero");

		Check(
			!ts::ParseHeightMapStem(
				"Commonwealth.HeightMap.-64.-64.63.63.-100.400",
				ts::HeightMapSource::kXLodGen),
			"a custom stem is rejected as xLODGen");
		Check(
			!ts::ParseHeightMapStem(
				"Commonwealth.Terrain.HeightMap.-64.-64.63.63.400.-100",
				ts::HeightMapSource::kXLodGen),
			"an inverted z range is rejected");
		Check(
			!ts::ParseHeightMapStem(
				"Commonwealth.Terrain.HeightMap.63.-64.-64.63.-100.400",
				ts::HeightMapSource::kXLodGen),
			"an inverted east/west extent is rejected");
		Check(
			!ts::ParseHeightMapStem(
				"Commonwealth.Terrain.HeightMap.-64.-64.6x.63.-100.400",
				ts::HeightMapSource::kXLodGen),
			"a non-integer field is rejected");
	}

	void TestCustomParsing()
	{
		const auto parsed = ts::ParseHeightMapStem(
			"Commonwealth.HeightMap.-64.-64.63.63.-1000.5000.-100.400",
			ts::HeightMapSource::kCustom);
		Check(parsed.has_value(), "custom stem parses");
		if (!parsed)
			return;
		Check(parsed->source == ts::HeightMapSource::kCustom, "source is custom");
		CheckNear(parsed->pos0[2], -8000.0, 1e-3, "black point is quantized by 8");
		CheckNear(parsed->pos1[2], 40000.0, 1e-3, "white point is quantized by 8");
		CheckNear(parsed->zRange[0], -800.0, 1e-3, "custom min z");
		CheckNear(parsed->zRange[1], 3200.0, 1e-3, "custom max z");

		Check(
			!ts::ParseHeightMapStem(
				"Commonwealth.Terrain.HeightMap.-64.-64.63.63.-100.400",
				ts::HeightMapSource::kCustom),
			"an xLODGen stem is rejected as custom");
	}

	ts::HeightMapMetadata MakeMetadata()
	{
		auto parsed = ts::ParseHeightMapStem(
			"Commonwealth.Terrain.HeightMap.-64.-64.63.63.-100.400",
			ts::HeightMapSource::kXLodGen);
		return parsed ? *parsed : ts::HeightMapMetadata{};
	}

	void TestFeatureBlock()
	{
		const auto metadata = MakeMetadata();
		const auto block = ts::BuildFeatureBlock(metadata, true);
		Check(block.enableTerrainShadow == 1u, "enabled block publishes one");

		const double uWest = metadata.pos0[0] * block.scale[0] + block.offset[0];
		const double uEast = metadata.pos1[0] * block.scale[0] + block.offset[0];
		const double vNorth = metadata.pos0[1] * block.scale[1] + block.offset[1];
		const double vSouth = metadata.pos1[1] * block.scale[1] + block.offset[1];
		CheckNear(uWest, 0.0, 1e-5, "west edge maps to U=0");
		CheckNear(uEast, 1.0, 1e-5, "east edge maps to U=1");
		CheckNear(vNorth, 0.0, 1e-5, "north edge maps to V=0");
		CheckNear(vSouth, 1.0, 1e-5, "south edge maps to V=1");
		Check(block.scale[1] < 0.0f, "the V axis is flipped against world north");

		const auto disabled = ts::BuildFeatureBlock(metadata, false);
		Check(
			disabled.enableTerrainShadow == 0u,
			"the disabled block publishes shader identity");
	}

	void TestDdaPlan()
	{
		const auto metadata = MakeMetadata();
		constexpr std::uint32_t width = 4096;
		constexpr std::uint32_t height = 4096;

		const auto eastward = ts::BuildDdaPlan(
			{ 0.8f, 0.0f, -0.6f }, metadata, width, height);
		Check(eastward.valid, "an eastward descending sun yields a plan");
		Check(!eastward.vertical, "the eastward sweep runs along X");
		Check(eastward.signDir == 1, "the eastward sweep advances");
		Check(eastward.edgePxCoord == 0, "the eastward sweep starts at the west edge");
		Check(
			eastward.dispatchCount == height,
			"a horizontal sweep dispatches one group per row");
		Check(
			eastward.maxUpdates == width / ts::kUpdateLength,
			"the sweep covers the width in 128-pixel slices");
		Check(
			eastward.lightDeltaZ[0] <= 0.0f && eastward.lightDeltaZ[1] <= 0.0f,
			"penumbra deltas descend");
		Check(
			eastward.lightDeltaZ[0] >= eastward.lightDeltaZ[1],
			"the upper penumbra decays no faster than the lower");

		const auto westward = ts::BuildDdaPlan(
			{ -0.8f, 0.0f, -0.6f }, metadata, width, height);
		Check(westward.valid, "a westward descending sun yields a plan");
		Check(westward.signDir == -1, "the westward sweep descends");
		Check(
			westward.edgePxCoord == width - 1,
			"the westward sweep starts at the east edge");
		for (std::uint32_t update = 0; update < westward.maxUpdates; ++update) {
			const auto start = ts::SliceStartCoord(westward, update);
			Check(start < width, "every descending slice start stays in range");
		}
		Check(
			ts::SliceStartCoord(westward, westward.maxUpdates + 8) < width,
			"an over-large slice index clamps instead of wrapping");

		const auto overhead = ts::BuildDdaPlan(
			{ 0.0f, 0.0f, -1.0f }, metadata, width, height);
		Check(!overhead.valid, "a zero-horizontal sun is rejected");

		const auto degenerate = ts::BuildDdaPlan(
			{ 0.8f, 0.0f, -0.6f }, metadata, 0, height);
		Check(!degenerate.valid, "a zero-width heightmap is rejected");

		const auto northward = ts::BuildDdaPlan(
			{ 0.0f, 0.9f, -0.4f }, metadata, width, height);
		Check(northward.valid, "a northward descending sun yields a plan");
		Check(northward.vertical, "the northward sweep runs along Y");
		Check(
			northward.signDir == -1,
			"a northward sun sweeps toward the top of the image");
	}

	void TestDownsample()
	{
		Check(
			ts::kDefaultDownsampleFactor == 4,
			"factor 4 is the default");
		Check(ts::IsValidDownsampleFactor(1), "factor 1 is valid");
		Check(ts::IsValidDownsampleFactor(2), "factor 2 is valid");
		Check(ts::IsValidDownsampleFactor(4), "factor 4 is valid");
		Check(!ts::IsValidDownsampleFactor(0), "factor 0 is rejected");
		Check(!ts::IsValidDownsampleFactor(3), "factor 3 is rejected");
		Check(!ts::IsValidDownsampleFactor(8), "factor 8 is rejected");

		Check(ts::ApplyDownsample(4096, 1) == 4096, "factor 1 is faithful");
		Check(ts::ApplyDownsample(4096, 2) == 2048, "factor 2 halves");
		Check(ts::ApplyDownsample(4096, 4) == 1024, "factor 4 quarters");
		Check(ts::ApplyDownsample(2, 4) == 1, "a small extent never collapses");
		Check(ts::ApplyDownsample(0, 4) == 0, "an empty extent stays empty");

		const auto full = ts::ComputeVramCost(4096, 4096);
		Check(full.heightBytes == 4096ull * 4096ull * 2ull, "R16 heights cost two bytes");
		Check(full.shadowBytes == 4096ull * 4096ull * 4ull, "R16G16 shadows cost four");
		Check(
			full.totalBytes == full.heightBytes + full.shadowBytes,
			"the total is the sum");
		const auto quarter = ts::ComputeVramCost(1024, 1024);
		Check(
			quarter.totalBytes * 16 == full.totalBytes,
			"factor 4 costs a sixteenth");
		CheckNear(ts::BytesToMiB(1024 * 1024), 1.0, 1e-9, "one MiB converts");
	}

	void TestGameHourJump()
	{
		Check(
			!ts::IsGameHourJump(10.0f, 10.001f),
			"normal progression is not a jump");
		Check(ts::IsGameHourJump(10.0f, 12.0f), "waiting two hours is a jump");
		Check(
			ts::IsGameHourJump(23.0f, 2.0f),
			"sleeping across midnight is a jump");
		Check(
			ts::IsGameHourJump(20.0f, 5.0f),
			"setting the hour backwards is a jump");
		Check(
			!ts::IsGameHourJump(23.99f, 0.01f),
			"a smooth midnight wrap is not a jump");
		Check(
			!ts::IsGameHourJump(
				std::numeric_limits<float>::quiet_NaN(), 3.0f),
			"a non-finite hour is ignored");
	}

	void TestBootstrapReadiness()
	{
		ts::BootstrapReadiness readiness;
		Check(
			!ts::IsReadyForInjectionFreeze(readiness),
			"an empty bootstrap is not ready");
		Check(
			!ts::MissingBootstrapPrerequisites(readiness).empty(),
			"an empty bootstrap names its gaps");

		readiness.registrationsInstalled = true;
		readiness.renderCallbacksInstalled = true;
		readiness.computeShaderReady = true;
		readiness.samplerReady = true;
		Check(
			!ts::IsReadyForInjectionFreeze(readiness),
			"a missing constant buffer blocks the freeze");
		readiness.constantBufferReady = true;
		Check(
			ts::IsReadyForInjectionFreeze(readiness),
			"a complete bootstrap freezes ready");
		Check(
			ts::MissingBootstrapPrerequisites(readiness).empty(),
			"a complete bootstrap names no gaps");
	}

	void TestHeightPercentileRange()
	{
		constexpr std::uint32_t width = 4;
		constexpr std::uint32_t height = 4;
		constexpr std::size_t rowPitch = width * sizeof(std::uint16_t) + 8;
		// Padding must not affect percentiles.
		std::vector<std::uint8_t> pixels(rowPitch * height, 0xFF);
		std::uint16_t sample = 0;
		for (std::uint32_t y = 0; y < height; ++y) {
			auto* row = pixels.data() + static_cast<std::size_t>(y) * rowPitch;
			for (std::uint32_t x = 0; x < width; ++x) {
				std::memcpy(
					row + static_cast<std::size_t>(x) * sizeof(sample),
					&sample,
					sizeof(sample));
				++sample;
			}
		}
		const auto range = ts::ComputeHeightPercentileRange(
			pixels.data(), rowPitch, width, height, 0.0f, 65535.0f);
		Check(range.p01 <= range.p99, "p01 never exceeds p99");
		Check(
			range.p99 < 100.0f,
			"the row pitch padding is skipped, not folded into the histogram");

		std::vector<std::uint8_t> flat(rowPitch * height, 0);
		for (std::uint32_t y = 0; y < height; ++y) {
			auto* row = flat.data() + static_cast<std::size_t>(y) * rowPitch;
			for (std::uint32_t x = 0; x < width; ++x) {
				const std::uint16_t value = 12345;
				std::memcpy(
					row + static_cast<std::size_t>(x) * sizeof(value),
					&value,
					sizeof(value));
			}
		}
		const auto flatRange = ts::ComputeHeightPercentileRange(
			flat.data(), rowPitch, width, height, 0.0f, 65535.0f);
		CheckNear(
			flatRange.p01,
			flatRange.p99,
			1e-3,
			"a degenerate single-valued heightmap collapses p01 and p99");

		const auto emptyRange = ts::ComputeHeightPercentileRange(
			nullptr, 0, 0, 0, -100.0f, 400.0f);
		Check(
			emptyRange.p01 == -100.0f && emptyRange.p99 == 400.0f,
			"an empty heightmap falls back to the full decode range");

		std::vector<std::uint8_t> onePixel(sizeof(std::uint16_t), 0);
		const std::uint16_t midpoint = 32767;
		std::memcpy(onePixel.data(), &midpoint, sizeof(midpoint));
		const auto singleRange = ts::ComputeHeightPercentileRange(
			onePixel.data(),
			sizeof(std::uint16_t),
			1,
			1,
			-100.0f,
			400.0f);
		CheckNear(
			singleRange.p01,
			singleRange.p99,
			1e-3,
			"a single sample yields a degenerate but stable range");
	}

}

int main()
{
	TestXLodGenParsing();
	TestCustomParsing();
	TestFeatureBlock();
	TestDdaPlan();
	TestDownsample();
	TestGameHourJump();
	TestBootstrapReadiness();
	TestHeightPercentileRange();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "TerrainShadows math tests passed\n";
	return 0;
}

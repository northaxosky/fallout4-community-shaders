#include "FeatureBuffer.h"
#include "TerrainShadowsMath.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
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

	std::string ReadAll(const std::filesystem::path& a_path)
	{
		std::ifstream stream(a_path);
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

	std::size_t CountOccurrences(
		std::string_view a_text,
		std::string_view a_needle)
	{
		std::size_t count = 0;
		for (auto cursor = a_text.find(a_needle);
			cursor != std::string_view::npos;
			cursor = a_text.find(a_needle, cursor + a_needle.size())) {
			++count;
		}
		return count;
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

	void TestFeatureBlockLayout()
	{
		using cs::FeatureDataCB;
		using cs::TerrainShadowsFeatureData;

		static_assert(sizeof(TerrainShadowsFeatureData) == 48);
		static_assert(offsetof(FeatureDataCB, terrainShadowsSettings) == 48);
		static_assert(sizeof(FeatureDataCB) == 112);
		static_assert(offsetof(TerrainShadowsFeatureData, TerrainShadowMode) == 0);
		static_assert(offsetof(TerrainShadowsFeatureData, Scale) == 4);
		static_assert(offsetof(TerrainShadowsFeatureData, ZRange) == 16);
		static_assert(offsetof(TerrainShadowsFeatureData, Offset) == 24);
		static_assert(offsetof(TerrainShadowsFeatureData, HeightRange) == 32);
		static_assert(offsetof(TerrainShadowsFeatureData, DebugHeightRange) == 40);

		const TerrainShadowsFeatureData zeroed;
		Check(
			zeroed.TerrainShadowMode == 0u,
			"an unloaded contributor leaves the block disabled");
		Check(
			zeroed.Scale[0] == 0.0f && zeroed.ZRange[0] == 0.0f
				&& zeroed.Offset[0] == 0.0f
				&& zeroed.HeightRange[0] == 0.0f
				&& zeroed.DebugHeightRange[0] == 0.0f,
			"an unloaded contributor zeroes the projection");
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

	std::array<double, 3> TransformViewPosition(
		std::array<double, 3> a_viewPosition,
		const std::array<std::array<double, 4>, 3>& a_viewToWorldRows,
		std::array<double, 3> a_cameraPositionAdjust)
	{
		const std::array position{
			a_viewPosition[0],
			a_viewPosition[1],
			a_viewPosition[2],
			1.0
		};
		auto result = a_cameraPositionAdjust;
		for (std::size_t axis = 0; axis < result.size(); ++axis) {
			for (std::size_t component = 0; component < position.size(); ++component)
				result[axis] += a_viewToWorldRows[axis][component] * position[component];
		}
		return result;
	}

	void TestNativeViewToWorldPosition()
	{
		const std::array rows{
			std::array{ 1.0, 0.0, 0.0, 10.0 },
			std::array{ 0.0, 1.0, 0.0, -20.0 },
			std::array{ 0.0, 0.0, 1.0, 30.0 }
		};
		const auto worldPosition = TransformViewPosition(
			{ 2.0, 3.0, 4.0 },
			rows,
			{ 100.0, 200.0, 300.0 });
		CheckNear(worldPosition[0], 112.0, 1e-9, "row zero produces world X");
		CheckNear(worldPosition[1], 183.0, 1e-9, "row one produces world Y");
		CheckNear(worldPosition[2], 334.0, 1e-9, "row two produces world Z");
	}

	void TestNativeRowsWorldLock()
	{
		const std::array identityRows{
			std::array{ 1.0, 0.0, 0.0, 10.0 },
			std::array{ 0.0, 1.0, 0.0, 20.0 },
			std::array{ 0.0, 0.0, 1.0, 30.0 }
		};
		const std::array yawRows{
			std::array{ 0.0, -1.0, 0.0, 10.0 },
			std::array{ 1.0, 0.0, 0.0, 20.0 },
			std::array{ 0.0, 0.0, 1.0, 30.0 }
		};
		constexpr std::array positionAdjust{ 1000.0, 2000.0, 3000.0 };
		const auto identityPosition =
			TransformViewPosition({ 5.0, 6.0, 7.0 }, identityRows, positionAdjust);
		const auto yawPosition =
			TransformViewPosition({ 6.0, -5.0, 7.0 }, yawRows, positionAdjust);

		for (std::size_t axis = 0; axis < identityPosition.size(); ++axis) {
			CheckNear(
				yawPosition[axis],
				identityPosition[axis],
				1e-9,
				"native rows keep the same world point across camera orientation");
		}
	}

	void TestShaderSources(
		const std::filesystem::path& a_sharedDataPath,
		const std::filesystem::path& a_sharedDataSourcePath,
		const std::filesystem::path& a_terrainShadowsPath,
		const std::filesystem::path& a_shadowUpdatePath,
		const std::filesystem::path& a_bsdfLightPath,
		const std::filesystem::path& a_bsdfCompositePath,
		const std::filesystem::path& a_featureSourcePath)
	{
		const auto sharedData = ReadAll(a_sharedDataPath);
		Check(
			!sharedData.contains("ViewToWorld")
				&& !sharedData.contains("CameraPositionWS")
				&& !sharedData.contains("WorldUpView"),
			"the shared substrate does not publish a second camera transform");
		Check(
			!sharedData.contains("viewPosition.xzy")
				&& !sharedData.contains("viewPosition.zyx"),
			"the substrate does not hand-swizzle engine view coordinates");

		const auto sharedDataSource = ReadAll(a_sharedDataSourcePath);
		Check(
			!sharedDataSource.contains("TryGetCameraMatrices")
				&& !sharedDataSource.contains(".invView")
				&& !sharedDataSource.contains("cameraState.posAdjust"),
			"the C++ substrate does not derive or publish camera world transforms");

		const auto terrainShadows = ReadAll(a_terrainShadowsPath);
		Check(
			terrainShadows.contains(
				"Texture2D<float2> ShadowHeightTexture : register(t30);"),
			"the consumer samples shadow heights at t30");
		Check(
			terrainShadows.contains(
				"Texture2D<float> SceneDepthTexture : register(t31);"),
			"fullscreen debug samples canonical scene depth at t31");
		Check(
			terrainShadows.contains(
				"SamplerState TerrainShadowsSampler : register(s13);"),
			"the consumer owns the dedicated sampler at s13");
		Check(
			terrainShadows.contains("return z - 256;"),
			"the consumer keeps the upstream 256-unit bias");
		Check(
			terrainShadows.contains("MODE_SHADOW_TERM")
				&& terrainShadows.contains("MODE_HEIGHTMAP"),
			"the consumer exposes both terrain debug views");
		Check(
			terrainShadows.contains("color = float4(value.xxx, 1.0);"),
			"a debug sample maps directly to output luminance");
		Check(
			terrainShadows.contains(
				"GetTerrainShadowMultFromViewPosition")
				&& terrainShadows.contains(
					"TryGetDebugColorFromViewPosition")
				&& terrainShadows.contains(
					"TryGetDebugColorFromScreenPosition"),
			"debug can reuse stock view position or use the depth fallback");
		Check(
			terrainShadows.contains(
				"float4 positionView = float4(viewPosition, 1.0);")
				&& terrainShadows.contains(
					"dot(viewToWorldRow0, positionView) + cameraPosAdjust.x")
				&& terrainShadows.contains(
					"dot(viewToWorldRow1, positionView) + cameraPosAdjust.y")
				&& terrainShadows.contains(
					"dot(viewToWorldRow2, positionView) + cameraPosAdjust.z"),
			"world position uses native b12 row dots and position adjustment");
		Check(
			terrainShadows.contains("rawDepth <= 0.01")
				&& terrainShadows.contains("rawDepth * 100.0")
				&& terrainShadows.contains("rawDepth * 1.01 - 0.01")
				&& terrainShadows.contains("viewPositionH.xyz / viewPositionH.w"),
			"the fallback mirrors the stock near/far reprojection contract");
		Check(
			terrainShadows.contains(
				"uint2 depthPixel = min(uint2(pixelPosition), depthDimensions - 1);")
				&& terrainShadows.contains(
					"pixelPosition * SharedData::BufferDim.zw *")
				&& terrainShadows.contains(
					"SharedData::DynamicResolution.zw;"),
			"the fallback loads render pixels directly and scales only the view ray");

		const auto shadowUpdate = ReadAll(a_shadowUpdatePath);
		Check(
			shadowUpdate.contains("#define NTHREADS 128"),
			"the compute slice width matches kUpdateLength");
		Check(
			!shadowUpdate.contains("all(lerpPxCoordA > 0)"),
			"the interpolator lower bound is inclusive");
		Check(
			shadowUpdate.contains("all(lerpPxCoordA >= 0)"),
			"the interpolator accepts row and column zero");

		const auto bsdfLight = ReadAll(a_bsdfLightPath);
		std::size_t consumerSites = 0;
		for (std::size_t cursor = bsdfLight.find(
				 "TerrainShadows::GetTerrainShadowMultFromViewPosition");
			cursor != std::string::npos;
			cursor = bsdfLight.find(
				"TerrainShadows::GetTerrainShadowMultFromViewPosition",
				cursor + 1)) {
			++consumerSites;
		}
		Check(
			consumerSites == 7,
			"all seven directional shadow paths multiply terrain visibility");
		Check(
			!bsdfLight.contains("GetTerrainShadowMultFromScreenPosition")
				&& bsdfLight.contains(
					"TerrainShadows::GetTerrainShadowMultFromViewPosition(\n        posView,"),
			"lighting passes each family's stock-reconstructed view position");
		Check(
			CountOccurrences(
				bsdfLight,
				"float4(ViewToWorld_row2.xyz, 1.0)")
					== 6
				&& !bsdfLight.contains(
					"float4(ViewToWorld_row0.z, ViewToWorld_row1.z, ViewToWorld_row2.z, 1.0)"),
			"all six light wetness paths use the native third row");
		Check(
			!bsdfLight.contains("TerrainShadows::TryGetDebugValue"),
			"lighting paths never compensate debug output through albedo");

		const auto bsdfComposite = ReadAll(a_bsdfCompositePath);
		constexpr std::array compositeFamilies{
			"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB31_FAMILY",
			"BSDFCOMPOSITE_PS_AMBIENT_IBL_CB47_FAMILY",
			"BSDFCOMPOSITE_PS_AMBIENT_IBL_COMPACT_FAMILY",
			"BSDFCOMPOSITE_PS_AMBIENT_IBL_MINIMAL_FAMILY",
			"BSDFCOMPOSITE_PS_2D_ACCUMULATOR",
			"BSDFCOMPOSITE_PS_2D_FOG",
			"BSDFCOMPOSITE_PS_NO_SRV_POSITION_TEXCOORD",
			"BSDFCOMPOSITE_PS_NO_SRV_POSITION",
			"BSDFCOMPOSITE_PS_CUBE_IBL",
			"BSDFCOMPOSITE_PS_NO_T0_ACCUMULATOR",
			"BSDFCOMPOSITE_PS_NO_T0_FOG",
			"BSDFCOMPOSITE_PS_SSS_MRT_RECORD_NORMAL",
			"BSDFCOMPOSITE_PS_SSS_MRT_SURFACE_CONTACT"
		};
		for (const auto* family : compositeFamilies) {
			const auto marker = std::string("#ifdef ") + family;
			const auto begin = bsdfComposite.find(marker);
			const auto end = bsdfComposite.find(
				"\n#ifdef BSDFCOMPOSITE_PS_",
				begin == std::string::npos ? 0 : begin + marker.size());
			const auto block = begin == std::string::npos ?
				std::string_view{} :
				std::string_view(bsdfComposite).substr(
					begin,
					end == std::string::npos ? std::string::npos : end - begin);
			Check(
				block.contains(
					"TerrainShadows::TryGetDebugColorFromViewPosition")
					|| block.contains(
						"TerrainShadows::TryGetDebugColorFromScreenPosition"),
				std::string(family) + " exposes terrain debug output");
			Check(
				block.contains("WetnessEffects::TryGetDebugColor(")
					|| block.contains(
						"WetnessEffects::TryGetDebugColorFromScreenPosition"),
				std::string(family) + " exposes wetness debug output");
		}
		Check(
			bsdfComposite.contains("output.color = terrainDebugColor;")
				&& bsdfComposite.contains("return terrainDebugColor;"),
			"final composite families return the direct debug color");
		Check(
			!bsdfComposite.contains("terrainDebugColor / max("),
			"the final composite does not compensate through material data");
		std::size_t viewDebugSites = 0;
		for (std::size_t cursor = bsdfComposite.find(
				 "TerrainShadows::TryGetDebugColorFromViewPosition");
			cursor != std::string::npos;
			cursor = bsdfComposite.find(
				"TerrainShadows::TryGetDebugColorFromViewPosition",
				cursor + 1)) {
			++viewDebugSites;
		}
		std::size_t fallbackDebugSites = 0;
		for (std::size_t cursor = bsdfComposite.find(
				 "TerrainShadows::TryGetDebugColorFromScreenPosition");
			cursor != std::string::npos;
			cursor = bsdfComposite.find(
				"TerrainShadows::TryGetDebugColorFromScreenPosition",
				cursor + 1)) {
			++fallbackDebugSites;
		}
		Check(
			viewDebugSites == 9,
			"nine composite debug paths reuse stock view positions");
		Check(
			fallbackDebugSites == 7,
			"seven positionless composite paths use the depth fallback");
		Check(
			bsdfComposite.contains("ambientFrame[35]")
				&& bsdfComposite.contains("g_PF[35]")
				&& bsdfComposite.contains("scene[35]")
				&& bsdfComposite.contains("cb12[35]"),
			"array-backed composite families source CameraPosAdjust at index 35");
		Check(
			bsdfLight.contains(
				"#if defined(DIRECTIONAL) && defined(TERRAIN_SHADOWS)"),
			"the consumer is gated on DIRECTIONAL and TERRAIN_SHADOWS");
		Check(
			!bsdfComposite.contains("SharedData::WorldUpView")
				&& CountOccurrences(
					bsdfComposite,
					"WetnessEffects::GetSurfaceFromViewToWorldRow2(")
					== 7
				&& !bsdfComposite.contains(
					"float4(ViewToWorld_row0.z, ViewToWorld_row1.z, ViewToWorld_row2.z, 1.0)")
				&& !bsdfComposite.contains(
					"float4(ambientFrame[12].z, ambientFrame[13].z, ambientFrame[14].z, 1.0)")
				&& !bsdfComposite.contains(
					"float4(g_PF[12].z, g_PF[13].z, g_PF[14].z, 1.0)")
				&& !bsdfComposite.contains(
					"float4(scene[12].z, scene[13].z, scene[14].z, 1.0)"),
			"all seven composite wetness paths use the native third row");

		const auto featureSource = ReadAll(a_featureSourcePath);
		Check(
			featureSource.contains(
				"const bool becameEnabled = enabled && !_wasEnabledLastFrame;")
				&& featureSource.contains(
					"const bool refresh = _pendingFullRefresh || timeJump || becameEnabled;")
				&& featureSource.contains(
					"} else if (timeJump || becameEnabled) {"),
			"re-enabling requests an immediate full shadow refresh");
		Check(
			featureSource.contains("DirectX::DDS_FLAGS_NONE"),
			"legacy L16 heightmaps remain single-channel");
		Check(
			!featureSource.contains("DirectX::DDS_FLAGS_EXPAND_LUMINANCE"),
			"legacy L16 heightmaps are not expanded to four channels");
	}
}

int main(int a_argc, char** a_argv)
{
	if (a_argc != 8) {
		std::cerr << "usage: TerrainShadowsMathTests <SharedData.hlsli> <SharedData.cpp> "
					 "<TerrainShadows.hlsli> <ShadowUpdate.cs.hlsl> "
					 "<BSDFLightShader.hlsl> <BSDFCompositeShader.hlsl> "
					 "<TerrainShadows.cpp>\n";
		return 2;
	}

	TestXLodGenParsing();
	TestCustomParsing();
	TestFeatureBlock();
	TestDdaPlan();
	TestDownsample();
	TestGameHourJump();
	TestBootstrapReadiness();
	TestFeatureBlockLayout();
	TestHeightPercentileRange();
	TestNativeViewToWorldPosition();
	TestNativeRowsWorldLock();
	TestShaderSources(
		a_argv[1],
		a_argv[2],
		a_argv[3],
		a_argv[4],
		a_argv[5],
		a_argv[6],
		a_argv[7]);

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "TerrainShadows math tests passed\n";
	return 0;
}

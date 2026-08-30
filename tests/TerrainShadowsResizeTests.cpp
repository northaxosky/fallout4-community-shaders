#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "HeightMapResize.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

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

	// Commonwealth ships 192x192 cells at 32 px per cell.
	constexpr std::uint32_t kCommonwealthExtent = 6144;

	// A separable ramp whose 2x2 and 4x4 box means are exact integers.
	constexpr std::uint16_t RampSample(std::size_t a_x, std::size_t a_y) noexcept
	{
		return static_cast<std::uint16_t>(5u * (a_x + a_y));
	}

	bool FillRamp(DirectX::ScratchImage& a_image, std::uint32_t a_extent)
	{
		if (FAILED(a_image.Initialize2D(
				DXGI_FORMAT_R16_UNORM, a_extent, a_extent, 1, 1))) {
			Check(false, "could not allocate the source ScratchImage");
			return false;
		}
		const auto* image = a_image.GetImage(0, 0, 0);
		if (!image) {
			Check(false, "the allocated ScratchImage carries no base image");
			return false;
		}
		for (std::size_t y = 0; y < image->height; ++y) {
			auto* row = image->pixels + y * image->rowPitch;
			for (std::size_t x = 0; x < image->width; ++x) {
				const auto sample = RampSample(x, y);
				std::memcpy(row + x * sizeof(sample), &sample, sizeof(sample));
			}
		}
		return true;
	}

	std::uint16_t ReadSample(
		const DirectX::Image& a_image,
		std::size_t a_x,
		std::size_t a_y)
	{
		std::uint16_t sample = 0;
		std::memcpy(
			&sample,
			a_image.pixels + a_y * a_image.rowPitch + a_x * sizeof(sample),
			sizeof(sample));
		return sample;
	}

	void CheckSample(
		const DirectX::Image& a_image,
		std::size_t a_x,
		std::size_t a_y,
		int a_expected,
		std::string_view a_message)
	{
		const int actual = ReadSample(a_image, a_x, a_y);
		// One step of tolerance for the filter's float round trip.
		if (actual < a_expected - 1 || actual > a_expected + 1) {
			std::cerr << "FAIL: " << a_message << " at (" << a_x << ", " << a_y
					  << ") actual " << actual << ", expected " << a_expected
					  << '\n';
			++failures;
		}
	}

	// The shipped default (factor 4) regressed because a single box resize
	// only accepts an exact 2:1 step.
	void TestShippedExtentDownsamples()
	{
		DirectX::ScratchImage sourceImage;
		if (!FillRamp(sourceImage, kCommonwealthExtent))
			return;
		const auto* source = sourceImage.GetImage(0, 0, 0);

		{
			DirectX::ScratchImage storage;
			const auto result = ts::DownsampleHeightMap(
				*source, kCommonwealthExtent, kCommonwealthExtent, storage);
			Check(SUCCEEDED(result.hr), "factor 1 must succeed");
			Check(result.image == source, "factor 1 must reuse the source image");
			Check(!result.resampled, "factor 1 must not resample");
			Check(result.halvings == 0, "factor 1 must not halve");
			Check(
				std::string_view(ts::DescribeDownsampleFilter(result)) == "none",
				"factor 1 must report no filter");
		}

		{
			DirectX::ScratchImage storage;
			const auto result = ts::DownsampleHeightMap(
				*source,
				kCommonwealthExtent / 2,
				kCommonwealthExtent / 2,
				storage);
			Check(SUCCEEDED(result.hr), "factor 2 must succeed");
			Check(result.image != nullptr, "factor 2 must produce an image");
			Check(result.halvings == 1, "factor 2 must take one box halving");
			Check(
				!result.usedTriangleFallback,
				"factor 2 must not need the triangle fallback");
			if (!result.image)
				return;
			Check(
				result.image->width == kCommonwealthExtent / 2
					&& result.image->height == kCommonwealthExtent / 2,
				"factor 2 must halve both extents");
			Check(
				result.image->format == DXGI_FORMAT_R16_UNORM,
				"factor 2 must preserve R16_UNORM");
			// 2x2 mean of 5*(x+y) over the block at (i, j) is 10i + 10j + 5.
			CheckSample(*result.image, 0, 0, 5, "factor 2 must average 2x2 blocks");
			CheckSample(
				*result.image, 100, 250, 3505, "factor 2 must average 2x2 blocks");
		}

		{
			DirectX::ScratchImage storage;
			const auto result = ts::DownsampleHeightMap(
				*source,
				kCommonwealthExtent / 4,
				kCommonwealthExtent / 4,
				storage);
			Check(SUCCEEDED(result.hr), "factor 4 must succeed");
			Check(result.image != nullptr, "factor 4 must produce an image");
			Check(result.halvings == 2, "factor 4 must take two box halvings");
			Check(
				!result.usedTriangleFallback,
				"factor 4 must not need the triangle fallback");
			Check(
				std::string_view(ts::DescribeDownsampleFilter(result)) == "box",
				"factor 4 must report the box filter");
			if (!result.image)
				return;
			Check(
				result.image->width == kCommonwealthExtent / 4
					&& result.image->height == kCommonwealthExtent / 4,
				"factor 4 must quarter both extents");
			// 4x4 mean of 5*(x+y) over the block at (i, j) is 20i + 20j + 15.
			// A point sample would read 20i + 20j instead.
			CheckSample(*result.image, 0, 0, 15, "factor 4 must average 4x4 blocks");
			CheckSample(
				*result.image, 3, 7, 215, "factor 4 must average 4x4 blocks");
			CheckSample(
				*result.image, 700, 900, 32015, "factor 4 must average 4x4 blocks");
		}
	}

	void TestNonHalvableExtents()
	{
		{
			DirectX::ScratchImage sourceImage;
			if (!FillRamp(sourceImage, 12))
				return;
			DirectX::ScratchImage storage;
			const auto result =
				ts::DownsampleHeightMap(*sourceImage.GetImage(0, 0, 0), 5, 5, storage);
			Check(SUCCEEDED(result.hr), "a 12 -> 5 resize must succeed");
			Check(result.halvings == 1, "a 12 -> 5 resize must halve once first");
			Check(
				result.usedTriangleFallback,
				"a 12 -> 5 resize must finish on the triangle fallback");
			Check(
				result.image && result.image->width == 5 && result.image->height == 5,
				"a 12 -> 5 resize must reach the requested extent");
		}

		{
			DirectX::ScratchImage sourceImage;
			if (!FillRamp(sourceImage, 5))
				return;
			DirectX::ScratchImage storage;
			const auto result =
				ts::DownsampleHeightMap(*sourceImage.GetImage(0, 0, 0), 2, 2, storage);
			Check(SUCCEEDED(result.hr), "an odd 5 -> 2 resize must succeed");
			Check(result.halvings == 0, "an odd extent cannot be halved");
			Check(
				std::string_view(ts::DescribeDownsampleFilter(result)) == "triangle",
				"an odd extent must report the triangle filter");
			Check(
				result.image && result.image->width == 2 && result.image->height == 2,
				"an odd 5 -> 2 resize must reach the requested extent");
		}
	}

	void TestRejectedRequests()
	{
		DirectX::ScratchImage sourceImage;
		if (!FillRamp(sourceImage, 8))
			return;
		const auto* source = sourceImage.GetImage(0, 0, 0);

		DirectX::ScratchImage storage;
		Check(
			ts::DownsampleHeightMap(*source, 0, 4, storage).hr == E_INVALIDARG,
			"a zero target extent must be rejected");
		Check(
			ts::DownsampleHeightMap(*source, 16, 16, storage).hr == E_INVALIDARG,
			"an upscale request must be rejected");
	}
}

int main()
{
	TestShippedExtentDownsamples();
	TestNonHalvableExtents();
	TestRejectedRequests();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "TerrainShadows resize tests passed\n";
	return 0;
}

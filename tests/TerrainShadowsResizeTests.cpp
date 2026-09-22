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

		DirectX::ScratchImage storage;
		const auto result = ts::DownsampleHeightMap(
			*source,
			kCommonwealthExtent / 4,
			kCommonwealthExtent / 4,
			storage);
		Check(SUCCEEDED(result.hr), "shipped factor-4 resize must succeed");
		Check(
			result.image
				&& result.image->width == kCommonwealthExtent / 4
				&& result.image->height == kCommonwealthExtent / 4
				&& result.image->format == DXGI_FORMAT_R16_UNORM
				&& result.halvings == 2
				&& !result.usedTriangleFallback,
			"shipped resize must use two R16 box halvings");
		if (!result.image)
			return;
		CheckSample(
			*result.image,
			0,
			0,
			15,
			"factor 4 must average rather than point sample");
		CheckSample(
			*result.image,
			700,
			900,
			32015,
			"factor 4 must preserve the shipped height ramp");
	}

	void TestNonHalvableExtents()
	{
		DirectX::ScratchImage sourceImage;
		if (!FillRamp(sourceImage, 12))
			return;
		DirectX::ScratchImage storage;
		const auto result = ts::DownsampleHeightMap(
			*sourceImage.GetImage(0, 0, 0), 5, 5, storage);
		Check(
			SUCCEEDED(result.hr) && result.halvings == 1
				&& result.usedTriangleFallback && result.image
				&& result.image->width == 5 && result.image->height == 5,
			"non-halvable extents must finish with the triangle fallback");
	}
}

int main()
{
	TestShippedExtentDownsamples();
	TestNonHalvableExtents();

	if (failures != 0) {
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "TerrainShadows resize tests passed\n";
	return 0;
}

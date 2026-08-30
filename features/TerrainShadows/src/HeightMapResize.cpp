#include "HeightMapResize.h"

#include <cstddef>
#include <utility>

namespace cs::features::terrain_shadows
{
	namespace
	{
		constexpr auto kBoxFilter =
			DirectX::TEX_FILTER_BOX | DirectX::TEX_FILTER_FORCE_NON_WIC;
		constexpr auto kTriangleFilter =
			DirectX::TEX_FILTER_TRIANGLE | DirectX::TEX_FILTER_FORCE_NON_WIC;

		[[nodiscard]] bool CanHalveToward(
			std::size_t a_extent,
			std::uint32_t a_target) noexcept
		{
			return a_extent % 2 == 0 && a_extent / 2 >= a_target;
		}
	}

	HeightMapDownsample DownsampleHeightMap(
		const DirectX::Image& a_source,
		std::uint32_t a_targetWidth,
		std::uint32_t a_targetHeight,
		DirectX::ScratchImage& a_storage) noexcept
	{
		HeightMapDownsample result;
		if (!a_source.pixels
			|| a_targetWidth == 0
			|| a_targetHeight == 0
			|| a_targetWidth > a_source.width
			|| a_targetHeight > a_source.height) {
			result.hr = E_INVALIDARG;
			return result;
		}
		if (a_targetWidth == a_source.width && a_targetHeight == a_source.height) {
			result.hr = S_OK;
			result.image = &a_source;
			return result;
		}

		std::size_t width = a_source.width;
		std::size_t height = a_source.height;
		const DirectX::Image* current = &a_source;
		DirectX::ScratchImage staged;

		// The box filter only accepts an exact 2:1 step, so walk the extent down in halves.
		while (CanHalveToward(width, a_targetWidth)
			&& CanHalveToward(height, a_targetHeight)) {
			DirectX::ScratchImage halved;
			result.hr =
				DirectX::Resize(*current, width / 2, height / 2, kBoxFilter, halved);
			if (FAILED(result.hr))
				return result;
			staged = std::move(halved);
			current = staged.GetImage(0, 0, 0);
			if (!current) {
				result.hr = E_POINTER;
				return result;
			}
			width /= 2;
			height /= 2;
			++result.halvings;
		}

		result.resampled = true;
		if (width == a_targetWidth && height == a_targetHeight) {
			a_storage = std::move(staged);
		} else {
			// Extents that do not halve onto the target still need an averaging filter.
			result.usedTriangleFallback = true;
			result.hr = DirectX::Resize(
				*current, a_targetWidth, a_targetHeight, kTriangleFilter, a_storage);
			if (FAILED(result.hr))
				return result;
		}

		result.image = a_storage.GetImage(0, 0, 0);
		result.hr = result.image ? S_OK : E_POINTER;
		return result;
	}

	const char* DescribeDownsampleFilter(
		const HeightMapDownsample& a_result) noexcept
	{
		if (!a_result.resampled)
			return "none";
		if (!a_result.usedTriangleFallback)
			return "box";
		return a_result.halvings > 0 ? "box+triangle" : "triangle";
	}
}

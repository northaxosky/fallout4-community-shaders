#pragma once

#include <cstdint>

#include <DirectXTex.h>

namespace cs::features::terrain_shadows
{
	struct HeightMapDownsample
	{
		HRESULT hr = E_FAIL;
		// Points at the source when no resampling was needed, else into the storage image.
		const DirectX::Image* image = nullptr;
		std::uint32_t         halvings = 0;
		bool                  resampled = false;
		bool                  usedTriangleFallback = false;
	};

	// Downsamples a heightmap to the requested extent with an area-averaging filter.
	[[nodiscard]] HeightMapDownsample DownsampleHeightMap(
		const DirectX::Image& a_source,
		std::uint32_t a_targetWidth,
		std::uint32_t a_targetHeight,
		DirectX::ScratchImage& a_storage) noexcept;

	[[nodiscard]] const char* DescribeDownsampleFilter(
		const HeightMapDownsample& a_result) noexcept;
}

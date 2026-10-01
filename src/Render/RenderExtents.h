#pragma once

#include "Render/Engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs::render
{
	inline std::uint32_t ScaledExtent(std::uint32_t a_extent, float a_ratio) noexcept
	{
		if (!std::isfinite(a_ratio) || a_ratio <= 0.0f)
			return 0;
		return std::min(a_extent, static_cast<std::uint32_t>(static_cast<double>(a_extent) * a_ratio));
	}

	struct ActiveExtentSnapshot
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;
		float widthRatio = 1.0f;
		float heightRatio = 1.0f;
	};

	inline ActiveExtentSnapshot GetActiveExtent(std::uint32_t a_fullWidth, std::uint32_t a_fullHeight) noexcept
	{
		ActiveExtentSnapshot snapshot;
		if (const auto* manager = engine::GetRenderTargetManager()) {
			snapshot.widthRatio = manager->GetDynamicWidthRatio();
			snapshot.heightRatio = manager->GetDynamicHeightRatio();
		}
		snapshot.width = ScaledExtent(a_fullWidth, snapshot.widthRatio);
		snapshot.height = ScaledExtent(a_fullHeight, snapshot.heightRatio);
		return snapshot;
	}
}

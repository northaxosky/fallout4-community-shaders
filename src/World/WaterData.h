#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace cs::engine
{
	inline constexpr float kNoWaterHeight = -(std::numeric_limits<float>::max)();

	[[nodiscard]] inline int WaterTileIndex(
		std::int32_t a_cellX, std::int32_t a_cellY, float a_cameraX, float a_cameraY) noexcept
	{
		const auto x = a_cellX - static_cast<std::int32_t>(std::floor(a_cameraX / 4096.0f)) + 2;
		const auto y = a_cellY - static_cast<std::int32_t>(std::floor(a_cameraY / 4096.0f)) + 2;
		return x >= 0 && x < 5 && y >= 0 && y < 5 ? x + y * 5 : -1;
	}

	[[nodiscard]] constexpr float WaterColorChannel(
		std::uint32_t a_shallow, std::uint32_t a_deep, unsigned a_shift) noexcept
	{
		return static_cast<float>(((a_shallow >> a_shift) & 255) + ((a_deep >> a_shift) & 255)) / 510.0f;
	}

	[[nodiscard]] inline float RelativeWaterHeight(float a_height, float a_originZ) noexcept
	{
		return std::isfinite(a_height) && a_height != kNoWaterHeight ?
		           a_height - a_originZ :
		           kNoWaterHeight;
	}
}

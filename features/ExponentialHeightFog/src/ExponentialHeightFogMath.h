#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cs::features::exponential_height_fog
{
	inline float Halton(std::uint32_t a_index, std::uint32_t a_base)
	{
		float result = 0.0f;
		const float invBase = 1.0f / static_cast<float>(a_base);
		float fraction = invBase;
		while (a_index > 0) {
			result += static_cast<float>(a_index % a_base) * fraction;
			a_index /= a_base;
			fraction *= invBase;
		}
		return result;
	}

	// Near volume spans [nearPlane, nearEnd]; the far volume runs to totalFar.
	struct DepthRanges
	{
		double nearPlane, nearEnd, totalFar;
		bool farEnabled;
	};

	inline DepthRanges ComputeDepthRanges(float a_near, float a_start, float a_distance, float a_nearGridDistance)
	{
		const double nearPlane = std::max(static_cast<double>(a_near), static_cast<double>(std::max(a_start, 0.0f)));
		const double totalFar = std::max(nearPlane + 1.0, static_cast<double>(std::max(a_distance, a_start + 1.0f)));
		const double nearEnd = std::min(std::max(static_cast<double>(std::max(a_nearGridDistance, 0.0f)), nearPlane + 1.0), totalFar);
		return { nearPlane, nearEnd, totalFar, nearEnd + 1.0 < totalFar };
	}

	inline std::array<float, 4> GridZParameters(double a_nearPlane, double a_farPlane,
		float a_distribution, std::uint32_t a_slices)
	{
		const double nearOffset = a_nearPlane + 0.095 * 100.0;
		const double distribution = std::max(static_cast<double>(a_distribution), static_cast<double>(a_slices) / 120.0);
		const double farExp = std::exp2(std::min(static_cast<double>(a_slices) / distribution, 120.0));
		const double offset = (a_farPlane - nearOffset * farExp) / (a_farPlane - nearOffset);
		return { static_cast<float>((1.0 - offset) / nearOffset), static_cast<float>(offset), static_cast<float>(distribution), 0 };
	}
}

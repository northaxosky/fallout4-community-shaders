#pragma once

namespace cs::features::imagespace
{
	inline constexpr int kDofQualityMin = 0;
	inline constexpr int kDofQualityMax = 1;

	constexpr int ClampDofQuality(int a_quality) noexcept
	{
		return a_quality < kDofQualityMin ?
			kDofQualityMin :
			(a_quality > kDofQualityMax ? kDofQualityMax : a_quality);
	}
}

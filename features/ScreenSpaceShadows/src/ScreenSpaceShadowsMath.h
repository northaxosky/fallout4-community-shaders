#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs::features::sss_math
{
	inline constexpr float kReferenceWidth = 1920.0f;
	inline constexpr float kReferenceHeight = 1080.0f;
	inline constexpr float kSamplesPerMultiplier = 60.0f;
	inline constexpr std::uint32_t kMinSampleCount = 8;
	inline constexpr std::uint32_t kMinSampleMultiplier = 1;
	inline constexpr std::uint32_t kMaxSampleMultiplier = 4;

	inline std::uint32_t ScaleSampleCount(
		std::uint32_t a_sampleMultiplier,
		float a_renderWidth,
		float a_renderHeight)
	{
		const float referenceArea = kReferenceWidth * kReferenceHeight;
		const float currentArea = a_renderWidth * a_renderHeight;
		const float areaScale = std::sqrt(currentArea / referenceArea);
		auto scaled = static_cast<std::uint32_t>(
			std::round(static_cast<float>(a_sampleMultiplier) * kSamplesPerMultiplier * areaScale));
		scaled = ((scaled + 7u) / 8u) * 8u;
		return std::max(scaled, kMinSampleCount);
	}

	inline std::uint32_t MigrateLegacyShadowLengthPercent(float a_percent)
	{
		const float legacySamples = (a_percent / 100.0f) * kReferenceHeight;
		const auto multiplier = static_cast<std::uint32_t>(
			std::round(legacySamples / kSamplesPerMultiplier));
		return std::clamp(multiplier, kMinSampleMultiplier, kMaxSampleMultiplier);
	}
}

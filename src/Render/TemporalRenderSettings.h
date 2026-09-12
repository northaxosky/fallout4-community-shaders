#pragma once

#include <cstdint>

namespace cs::render::temporal
{
		enum class UpscaleMethod
		{
			kNONE,
			kTAA,
			kFSR,
			kDLSS,
			kCount
		};

		inline constexpr std::uint32_t kMaxUpscaleMethodValue =
			static_cast<std::uint32_t>(UpscaleMethod::kDLSS);

		constexpr bool IsExternalUpscaler(UpscaleMethod a_method) noexcept
		{
			switch (a_method) {
			case UpscaleMethod::kFSR:
			case UpscaleMethod::kDLSS:
				return true;
			default:
				return false;
			}
		}

		struct UpscalingSettings
		{
			bool enabled = true;
			std::uint32_t upscaleMethod = (std::uint32_t)UpscaleMethod::kDLSS;
			std::uint32_t upscaleMethodNoDLSS = (std::uint32_t)UpscaleMethod::kFSR;
			std::uint32_t qualityMode = 1;  // 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA
			std::uint32_t streamlineLogLevel = 0;
			float sharpnessFSR = 0.0f;
			bool sharpnessEnabledDLSS = false;
			float sharpnessDLSS = 0.0f;
			std::uint32_t presetDLSS = 0;  // 0=Default, 1=J, 2=K, 3=L, 4=M
		};
}

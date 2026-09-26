#pragma once

#include "Settings/SettingsSchema.h"

#include <array>

#include <cstdint>

namespace cs::render::temporal
{
	enum class UpscaleMethod
	{
		kNONE,
		kTAA,
		kFSR,
		kDLSS,
		kFSR4,
		kCount
	};

	inline constexpr std::uint32_t kMaxUpscaleMethodValue =
		static_cast<std::uint32_t>(UpscaleMethod::kFSR4);

	constexpr bool IsExternalUpscaler(UpscaleMethod a_method) noexcept
	{
		switch (a_method) {
		case UpscaleMethod::kFSR:
		case UpscaleMethod::kDLSS:
		case UpscaleMethod::kFSR4:
			return true;
		default:
			return false;
		}
	}

	struct UpscalingSettings
	{
		// Renderer-facing enablement is derived from upscaleMethod. It is not
		// persisted as an independent user setting.
		bool enabled = true;
		std::uint32_t upscaleMethod = (std::uint32_t)UpscaleMethod::kDLSS;
		std::uint32_t qualityMode = 1;  // 1=Quality, 2=Balanced, 3=Performance, 4=Ultra Performance, 0=Native AA
		std::uint32_t streamlineLogLevel = 0;
		float sharpnessFSR = 0.0f;
		bool sharpnessEnabledDLSS = false;
		float sharpnessDLSS = 0.0f;
		std::uint32_t presetDLSS = 0;  // 0=Default, 1=J, 2=K, 3=L, 4=M
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "upscale_method", "Method: 0 Off, 1 TAA, 2 FSR 3, 3 DLSS, 4 FSR 4; admitted providers switch at a frame boundary.", &UpscalingSettings::upscaleMethod, settings::Range{ 0u, kMaxUpscaleMethodValue } },
			settings::Field{ "quality_mode", "Quality: 0 Native AA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance.", &UpscalingSettings::qualityMode, settings::Range{ 0u, 4u } },
			settings::Field{ "streamline_log_level", "Streamline logging: 0 Off, 1 Default, 2 Verbose.", &UpscalingSettings::streamlineLogLevel, settings::Range{ 0u, 2u }, settings::ApplyTiming::kNextLaunch },
			settings::Field{ "preset_dlss", "DLSS preset: 0 Default, 1 J, 2 K, 3 L, 4 M.", &UpscalingSettings::presetDLSS, settings::Range{ 0u, 4u } },
			settings::Field{ "sharpness_fsr", "FSR sharpening strength.", &UpscalingSettings::sharpnessFSR, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "sharpness_enabled_dlss", "Enable DLSS sharpening.", &UpscalingSettings::sharpnessEnabledDLSS },
			settings::Field{ "sharpness_dlss", "DLSS sharpening strength.", &UpscalingSettings::sharpnessDLSS, settings::Range{ 0.0f, 1.0f } }
		}
	};
}

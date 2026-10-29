#pragma once

#include "Settings/SettingsSchema.h"

#include <array>

#include <cstdint>

namespace cs::render::temporal
{
	enum class SuperResolutionMethod : std::uint8_t
	{
		kNone,
		kTAA,
		kFSR3,
		kDLSS,
		kFSR4,
		kCount
	};
	using UpscaleMethod = SuperResolutionMethod;

	enum class FrameGenerationMethod : std::uint8_t
	{
		kOff,
		kFSR3,
		kDLSSG,
		kFSR4,
		kCount
	};

	inline constexpr std::uint32_t kMaxFrameGenerationMethodValue =
		static_cast<std::uint32_t>(FrameGenerationMethod::kFSR4);

	inline constexpr std::uint32_t kMaxLatencyReductionValue = 2;

	inline constexpr std::uint32_t kMaxUpscaleMethodValue =
		static_cast<std::uint32_t>(UpscaleMethod::kFSR4);

	constexpr bool IsExternalUpscaler(UpscaleMethod a_method) noexcept
	{
		switch (a_method) {
		case UpscaleMethod::kFSR3:
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
		std::uint32_t presetDLSS = 0;        // 0=Default, 1=J, 2=K, 3=L, 4=M
		std::uint32_t latencyReduction = 1;  // 0=Off, 1=On, 2=On + Boost
		bool latencyFpsLimitEnabled = false;
		float latencyFpsLimit = 60.0f;
		bool latencyUseMarkers = false;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "upscale_method", "Method: 0 Off, 1 TAA, 2 FSR 3, 3 DLSS, 4 FSR 4; admitted providers switch at a frame boundary.", &UpscalingSettings::upscaleMethod, settings::Range{ 0u, kMaxUpscaleMethodValue } },
			settings::Field{ "quality_mode", "Quality: 0 Native AA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance.", &UpscalingSettings::qualityMode, settings::Range{ 0u, 4u } },
			settings::Field{ "streamline_log_level", "Streamline logging: 0 Off, 1 Default, 2 Verbose.", &UpscalingSettings::streamlineLogLevel, settings::Range{ 0u, 2u }, settings::ApplyTiming::kNextLaunch },
			settings::Field{ "preset_dlss", "DLSS preset: 0 Default, 1 J, 2 K, 3 L, 4 M.", &UpscalingSettings::presetDLSS, settings::Range{ 0u, 4u } },
			settings::Field{ "sharpness_fsr", "FSR sharpening strength.", &UpscalingSettings::sharpnessFSR, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "sharpness_enabled_dlss", "Enable DLSS sharpening.", &UpscalingSettings::sharpnessEnabledDLSS },
			settings::Field{ "sharpness_dlss", "DLSS sharpening strength.", &UpscalingSettings::sharpnessDLSS, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "latency_reduction", "Latency reduction: 0 Off, 1 On, 2 On + Boost; Boost is NVIDIA Reflex only.", &UpscalingSettings::latencyReduction, settings::Range{ 0u, kMaxLatencyReductionValue } },
			settings::Field{ "latency_fps_limit_enabled", "Enable the latency backend's frame rate limit.", &UpscalingSettings::latencyFpsLimitEnabled },
			settings::Field{ "latency_fps_limit", "Latency backend frame rate limit in FPS.", &UpscalingSettings::latencyFpsLimit, settings::Range{ 20.0f, 240.0f } },
			settings::Field{ "latency_use_markers", "Let NVIDIA Reflex optimize pacing from PCL markers.", &UpscalingSettings::latencyUseMarkers } }
	};
}

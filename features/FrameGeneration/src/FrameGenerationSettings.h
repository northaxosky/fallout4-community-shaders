#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::frame_generation
{
	enum class Method : std::uint32_t
	{
		kOff,
		kFSR3,
		kDLSSG,
		kFSR4
	};

	struct Settings
	{
		std::uint32_t frameGenerationMethod = static_cast<std::uint32_t>(Method::kFSR3);
		bool frameGenerationAllowInMenus = false;
		std::uint32_t dlssgMode = 0;
		std::uint32_t dlssgFixedMultiplier = 2;
		float dlssgDynamicTargetFps = 0.0f;
		bool detailedDiagnostics = false;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "frame_generation_method", "Method: 0 Off, 1 FSR 3, 2 DLSS, 3 FSR 4; admitted providers switch at a frame boundary.", &Settings::frameGenerationMethod,
				settings::Range{ 0u, static_cast<std::uint32_t>(Method::kFSR4) } },
			settings::Field{ "frame_generation_allow_in_menus", "Allow generated frames in pause, main, loading, and Pip-Boy menus.", &Settings::frameGenerationAllowInMenus },
			settings::Field{ "dlssg_mode", "DLSS-G mode: 0 fixed multiplier, 1 dynamic MFG.", &Settings::dlssgMode, settings::Range{ 0u, 1u } },
			settings::Field{ "dlssg_fixed_multiplier", "Fixed multiplier including the real frame; requests above the runtime maximum are rejected.", &Settings::dlssgFixedMultiplier,
				settings::Range{ 2u, std::numeric_limits<std::uint32_t>::max() } },
			settings::Field{ "dlssg_dynamic_target_fps", "Dynamic MFG target FPS; 0 selects the SDK display target, and VSync overrides custom targets.", &Settings::dlssgDynamicTargetFps,
				settings::Range{ 0.0f, std::numeric_limits<float>::max() } },
			settings::Field{ "detailed_diagnostics", "Keep a bounded in-memory phase trace for failure logs.", &Settings::detailedDiagnostics }
		}
	};
}

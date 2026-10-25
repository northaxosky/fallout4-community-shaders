#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::rain_refraction
{
	// Upstream RainRefraction::Settings without Enable, which is the framework switch.
	struct Settings
	{
		float MotionStretch = 2.0f;
		float MotionTransparency = 0.91f;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "MotionStretch", "Motion Stretch", &Settings::MotionStretch, settings::Range{ 0.01f, 3.0f } },
			settings::Field{ "MotionTransparency", "Motion Transparency", &Settings::MotionTransparency, settings::Range{ 0.0f, 1.0f } } }
	};
}

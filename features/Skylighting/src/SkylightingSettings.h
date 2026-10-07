#pragma once

#include "Settings/SettingsSchema.h"

#include <numbers>

namespace cs::features::skylighting
{
	struct Settings
	{
		bool enabled = true;
		float MaxZenith = 3.1415926f / 2.f;  // 90 deg
		float MinDiffuseVisibility = 0.1f;
		float MinSpecularVisibility = 0.1f;
	};

	// Radians; the upstream UI slider and load clamp both span 0 to 90 degrees.
	inline constexpr float kMaxZenithLimit = std::numbers::pi_v<float> / 2.0f;
	static_assert(Settings{}.MaxZenith <= kMaxZenithLimit);

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable Skylighting.", &Settings::enabled },
			settings::Field{ "MaxZenith", "Max Zenith Angle", &Settings::MaxZenith, settings::Range{ 0.0f, kMaxZenithLimit } },
			settings::Field{ "MinDiffuseVisibility", "Diffuse Min Visibility", &Settings::MinDiffuseVisibility, settings::Range{ 0.01f, 1.0f } },
			settings::Field{ "MinSpecularVisibility", "Specular Min Visibility", &Settings::MinSpecularVisibility, settings::Range{ 0.01f, 1.0f } } }
	};
}

#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::vanilla_pbr
{
	struct Settings
	{
		bool EnableGGX = true;
		bool EnableEyeSpecialHandling = true;
		float RoughnessMultiplier = 1.0f;
		float SpecularRoughnessBlend = 0.0f;
		float BaseF0Multiplier = 0.2f;
		float MinF0 = 0.02f;
		bool SplitScreen = false;
		float SplitPosition = 0.5f;
	};
	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "EnableGGX", "Enable Phong to GGX", &Settings::EnableGGX },
			settings::Field{ "EnableEyeSpecialHandling", "Enable Eye Special Handling", &Settings::EnableEyeSpecialHandling },
			settings::Field{ "RoughnessMultiplier", "Roughness Multiplier", &Settings::RoughnessMultiplier, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "SpecularRoughnessBlend", "Specular Roughness Blend", &Settings::SpecularRoughnessBlend, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "BaseF0Multiplier", "Base F0 Multiplier", &Settings::BaseF0Multiplier, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "MinF0", "Min F0", &Settings::MinF0, settings::Range{ 0.0f, 0.04f } },
			settings::Field{ "SplitScreen", "Split-Screen A/B", &Settings::SplitScreen },
			settings::Field{ "SplitPosition", "Split Position", &Settings::SplitPosition, settings::Range{ 0.0f, 1.0f } } }
	};
}

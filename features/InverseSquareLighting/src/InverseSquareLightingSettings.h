#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::inverse_square_lighting
{
	struct Settings
	{
		bool deriveUnauthoredLights = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "derive_unauthored_lights", "Derive Unauthored Lights", &Settings::deriveUnauthoredLights, {}, settings::ApplyTiming::kNextLaunch } }
	};
}

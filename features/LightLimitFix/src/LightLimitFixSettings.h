#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::light_limit_fix
{
	struct Settings
	{
		bool deriveUnauthoredLights = false;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "derive_unauthored_lights", "Derive Unauthored Lights", &Settings::deriveUnauthoredLights, {}, settings::ApplyTiming::kNextLaunch } }
	};
}

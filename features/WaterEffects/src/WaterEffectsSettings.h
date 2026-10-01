#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::water_effects
{
	struct Settings
	{
		bool enabled = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{ settings::Field{ "enabled", "Enable water caustics.", &Settings::enabled } }
	};
}

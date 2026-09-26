#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::dynamic_cubemaps
{
	struct Settings
	{
		bool enabled = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{ settings::Field{ "enabled", "Enable cubemap capture and deferred reflections.", &Settings::enabled } }
	};
}

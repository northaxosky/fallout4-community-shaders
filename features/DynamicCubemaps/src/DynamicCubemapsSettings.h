#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::dynamic_cubemaps
{
	struct Settings
	{
		bool enabled = true;
		bool enabledSSR = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable cubemap capture and dynamic water reflections.", &Settings::enabled },
			settings::Field{ "enabled_ssr", "Enable all screen-space reflections, including water.", &Settings::enabledSSR } }
	};
}

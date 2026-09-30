#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::terrain_shadows
{
	struct Settings
	{
		bool EnableTerrainShadow = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "EnableTerrainShadow", "Enable terrain shadows.", &Settings::EnableTerrainShadow } }
	};
}

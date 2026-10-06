#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::terrain_variation
{
	struct Settings
	{
		bool enabled = true;
		bool enableLODTerrainTilingFix = true;
		bool enableMeshSupport = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable terrain variation.", &Settings::enabled },
			settings::Field{ "enableLODTerrainTilingFix", "Apply to LOD Terrain", &Settings::enableLODTerrainTilingFix },
			settings::Field{ "enableMeshSupport", "Enable Mesh Support", &Settings::enableMeshSupport } }
	};
}

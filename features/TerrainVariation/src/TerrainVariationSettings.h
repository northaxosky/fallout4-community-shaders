#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::terrain_variation
{
	struct Settings
	{
		bool enableLODTerrainTilingFix = true;
		bool enableMeshSupport = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enableLODTerrainTilingFix", "Apply to LOD Terrain", &Settings::enableLODTerrainTilingFix }.Help("Applies the tiling fix to LOD terrain objects. This helps reduce the visible tiling effect on distant terrain."),
			settings::Field{ "enableMeshSupport", "Enable Mesh Support", &Settings::enableMeshSupport }.Help("Applies the tiling fix to meshes that use landscape textures, such as dirt cliffs and mountain slabs. Alpha tested meshes like foliage and decals are never affected.") }
	};
}

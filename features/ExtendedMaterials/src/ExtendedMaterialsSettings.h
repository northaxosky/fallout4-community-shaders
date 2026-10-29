#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::extended_materials
{
	// FO4: Complex Material is unported, so it has no key.
	struct Settings
	{
		bool EnableParallax = true;
		bool EnableTerrain = true;
		bool EnableHeightBlending = true;
		bool EnableShadows = true;
		bool EnableParallaxWarpingFix = true;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "EnableParallax", "Enable Parallax", &Settings::EnableParallax },
			settings::Field{ "EnableTerrain", "Enable Terrain Parallax", &Settings::EnableTerrain },
			settings::Field{ "EnableHeightBlending", "Enable Terrain Height Blending", &Settings::EnableHeightBlending },
			settings::Field{ "EnableShadows", "Enable Shadows", &Settings::EnableShadows },
			settings::Field{ "EnableParallaxWarpingFix", "Enable Parallax Warping Fix", &Settings::EnableParallaxWarpingFix } }
	};
}

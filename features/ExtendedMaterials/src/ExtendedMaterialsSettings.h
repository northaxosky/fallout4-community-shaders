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
			settings::Field{ "EnableParallax", "Enable Parallax", &Settings::EnableParallax }.Help("Enables parallax on standard meshes made for parallax."),
			settings::Field{ "EnableTerrain", "Enable Terrain Parallax", &Settings::EnableTerrain }.Help("Enables terrain parallax using the alpha channel of each landscape specular (_s) texture. Layers without a height map stay flat."),
			settings::Field{ "EnableHeightBlending", "Enable Terrain Height Blending", &Settings::EnableHeightBlending }.Help("Enables landscape texture blending based on parallax."),
			settings::Field{ "EnableShadows", "Enable Shadows", &Settings::EnableShadows }.Help("Enables cheap soft shadows when using parallax. This applies to sunlight."),
			settings::Field{ "EnableParallaxWarpingFix", "Enable Parallax Warping Fix", &Settings::EnableParallaxWarpingFix }.Help("Enables a fix reducing parallax scale on curved and smooth normal triangles.") }
	};
}

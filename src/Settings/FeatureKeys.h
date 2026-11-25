#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 27> kAllFeatureKeys{
		"ScreenSpaceGI",
		"SubsurfaceScattering",
		"Skylighting",
		"LightLimitFix",
		"VanillaPBR",
		"DynamicCubemaps",
		"ImageBasedLighting",
		"WaterEffects",
		"UnifiedWater",
		"ScreenSpaceShadows",
		"TerrainShadows",
		"SkySync",
		"VolumetricShadows",
		"CloudShadows",
		"LODBlending",
		"TerrainVariation",
		"TerrainBlending",
		"ExtendedMaterials",
		"MotionVectorFixes",
		"Upscaling",
		"FrameGeneration",
		"HDRDisplay",
		"PerformanceOverlay",
		"RenderDoc",
		"Screenshot",
		"RemoteControl",
		"CSEditor"
	};

	// Activation follows this order, and IBL fails to load before Dynamic Cubemaps.
	static_assert(std::ranges::find(kAllFeatureKeys, "DynamicCubemaps") < std::ranges::find(kAllFeatureKeys, "ImageBasedLighting"));
}

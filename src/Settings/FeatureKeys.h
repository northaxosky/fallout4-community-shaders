#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 24> kAllFeatureKeys{
		"ScreenSpaceGI",
		"Skylighting",
		"LightLimitFix",
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
		"RainRefraction",
		"ExtendedMaterials",
		"MotionVectorFixes",
		"Upscaling",
		"FrameGeneration",
		"PerformanceOverlay",
		"RenderDoc",
		"RemoteControl",
		"CSEditor"
	};

	// Activation order; IBL and Rain Refraction fail to load before Dynamic Cubemaps.
	static_assert(std::ranges::find(kAllFeatureKeys, "DynamicCubemaps") < std::ranges::find(kAllFeatureKeys, "ImageBasedLighting"));
	static_assert(std::ranges::find(kAllFeatureKeys, "DynamicCubemaps") < std::ranges::find(kAllFeatureKeys, "RainRefraction"));
}

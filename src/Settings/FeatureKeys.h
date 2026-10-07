#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 15> kAllFeatureKeys{
		"ScreenSpaceGI",
		"Skylighting",
		"InverseSquareLighting",
		"ExponentialHeightFog",
		"DynamicCubemaps",
		"WaterEffects",
		"ScreenSpaceShadows",
		"TerrainShadows",
		"LODBlending",
		"TerrainVariation",
		"MotionVectorFixes",
		"Upscaling",
		"FrameGeneration",
		"PerformanceOverlay",
		"RenderDoc"
	};
}

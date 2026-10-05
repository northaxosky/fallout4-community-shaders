#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 13> kAllFeatureKeys{
		"ScreenSpaceGI",
		"InverseSquareLighting",
		"ExponentialHeightFog",
		"DynamicCubemaps",
		"WaterEffects",
		"ScreenSpaceShadows",
		"TerrainShadows",
		"LODBlending",
		"MotionVectorFixes",
		"Upscaling",
		"FrameGeneration",
		"PerformanceOverlay",
		"RenderDoc"
	};
}

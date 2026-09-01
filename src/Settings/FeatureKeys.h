#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 11> kAllFeatureKeys{
		"ScreenSpaceGI",
		"InverseSquareLighting",
		"DynamicCubemaps",
		"WetnessEffects",
		"WaterEffects",
		"ScreenSpaceShadows",
		"TerrainShadows",
		"MotionVectorFixes",
		"Upscaling",
		"PerformanceOverlay",
		"RenderDoc"
	};
}

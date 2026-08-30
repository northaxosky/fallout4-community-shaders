#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 10> kAllFeatureKeys{
		"ScreenSpaceGI",
		"InverseSquareLighting",
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

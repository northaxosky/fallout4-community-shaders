#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 9> kAllFeatureKeys{
		"ScreenSpaceGI",
		"InverseSquareLighting",
		"WetnessEffects",
		"ScreenSpaceShadows",
		"TerrainShadows",
		"MotionVectorFixes",
		"Upscaling",
		"PerformanceOverlay",
		"RenderDoc"
	};
}

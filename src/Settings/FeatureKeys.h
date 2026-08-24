#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 7> kAllFeatureKeys{
		"ScreenSpaceGI",
		"WetnessEffects",
		"ScreenSpaceShadows",
		"Upscaling",
		"FrameGeneration",
		"PerformanceOverlay",
		"RenderDoc"
	};
}

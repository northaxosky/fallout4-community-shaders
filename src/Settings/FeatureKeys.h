#pragma once

#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::array<std::string_view, 10> kAllFeatureKeys{
		"ScreenSpaceGI",
		"ScreenSpaceShadows",
		"Imagespace",
		"Upscaling",
		"FrameGeneration",
		"MotionVectorFixes",
		"PerformanceOverlay",
		"RenderDoc",
		"ShaderCatalog",
		"ShaderReplacement"
	};
}

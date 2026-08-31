#pragma once

#include <array>
#include <string_view>

namespace cs::FeatureCategories
{
	inline constexpr char kLighting[]      = "Lighting";
	inline constexpr char kMaterials[]     = "Materials";
	inline constexpr char kPostProcess[]   = "Post-process";
	inline constexpr char kCompatibility[] = "Compatibility";
	inline constexpr char kPerformance[]   = "Performance";
	inline constexpr char kDevTools[]      = "Dev Tools";
	inline constexpr char kMisc[]          = "Misc";

	inline constexpr std::array<std::string_view, 6> kRenderOrder{
		kLighting,
		kMaterials,
		kPostProcess,
		kCompatibility,
		kPerformance,
		kDevTools
	};
}

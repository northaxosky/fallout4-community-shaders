#pragma once

#include <array>
#include <string_view>

namespace cs::FeatureCategories
{
	inline constexpr char kDisplay[] = "Display";
	inline constexpr char kLandscapeAndTextures[] = "Landscape & Textures";
	inline constexpr char kLighting[] = "Lighting";
	inline constexpr char kMaterials[] = "Materials";
	inline constexpr char kOther[] = "Other";
	inline constexpr char kUtility[] = "Utility";
	inline constexpr char kWater[] = "Water";

	struct Category
	{
		std::string_view id;
		std::string_view displayName;
		std::string_view iconName{};
	};

	inline constexpr std::array kCategories{
		Category{ "display", kDisplay, "monitor" },
		Category{ "utility", kUtility, "wrench" },
		Category{ "lighting", kLighting, "sun-horizon" },
		Category{ "materials", kMaterials, "cube" },
		Category{ "landscape-textures", kLandscapeAndTextures, "mountains" },
		Category{ "water", kWater, "waves" }
	};
}

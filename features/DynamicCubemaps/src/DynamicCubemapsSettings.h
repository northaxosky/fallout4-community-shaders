#pragma once

#include "Settings/SettingsSchema.h"

#include <array>

namespace cs::features::dynamic_cubemaps
{
	enum class EngineCubeSize : std::uint32_t
	{
		k256 = 256,
		k512 = 512,
		k1024 = 1024,
		k2048 = 2048
	};

	inline constexpr std::array kEngineCubeSizes{
		settings::Choice{ "256", EngineCubeSize::k256 },
		settings::Choice{ "512", EngineCubeSize::k512 },
		settings::Choice{ "1024", EngineCubeSize::k1024 },
		settings::Choice{ "2048", EngineCubeSize::k2048 }
	};

	struct Settings
	{
		bool enabledSSR = true;
		bool engineCubeLod = true;
		EngineCubeSize engineCubeSize = EngineCubeSize::k1024;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled_ssr", "Enable all screen-space reflections, including water.", &Settings::enabledSSR },
			settings::Field{ "engine_cube_lod", "Draw LOD land, objects and trees into the engine reflection cube that far water blends toward.", &Settings::engineCubeLod },
			settings::ChoiceField{ "engine_cube_size", "Engine reflection cube render size in texels, filtered to 256 for reflections; FO4 object LOD needs 1024 or more to stay stable.", &Settings::engineCubeSize, kEngineCubeSizes, settings::ApplyTiming::kNextLaunch } }
	};
}

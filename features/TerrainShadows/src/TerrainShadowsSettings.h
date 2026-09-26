#pragma once

#include "Settings/SettingsSchema.h"
#include "TerrainShadowsMath.h"

namespace cs::features::terrain_shadows
{
	struct Settings
	{
		bool enabled = true;
		std::uint32_t downsampleFactor = kDefaultDownsampleFactor;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable terrain shadows.", &Settings::enabled },
			settings::Field{ "downsample_factor", "Heightmap resolution divisor; 4 uses one-sixteenth the VRAM of 1.", &Settings::downsampleFactor,
				settings::Range{ kDownsampleFactors.front(), kDownsampleFactors.back() } }
		}
	};
}

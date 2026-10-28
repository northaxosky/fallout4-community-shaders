#pragma once

#include "Settings/SettingsSchema.h"

#include <cstdint>

namespace cs::features::volumetric_lighting
{
	struct Settings
	{
		bool ExteriorEnabled = true;
		std::int32_t ExteriorQuality = 2;
		bool InteriorEnabled = true;
		std::int32_t InteriorQuality = 2;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "ExteriorEnabled", "Enable Volumetric Lighting in Exteriors", &Settings::ExteriorEnabled },
			settings::Field{ "ExteriorQuality", "Exterior Quality", &Settings::ExteriorQuality, settings::Range<std::int32_t>{ 0, 3 } },
			settings::Field{ "InteriorEnabled", "Enable Volumetric Lighting in Interiors", &Settings::InteriorEnabled },
			settings::Field{ "InteriorQuality", "Interior Quality", &Settings::InteriorQuality, settings::Range<std::int32_t>{ 0, 3 } } }
	};
}

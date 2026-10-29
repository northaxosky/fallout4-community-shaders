#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::cloud_shadows
{
	struct Settings
	{
		float Opacity = 0.5f;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "Opacity", "Opacity", &Settings::Opacity, settings::Range{ 0.0f, 4.0f } } }
	};
}

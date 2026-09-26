#pragma once

#include "ScreenSpaceShadowsMath.h"
#include "Settings/SettingsSchema.h"

#include <cstdint>
#include <tuple>

namespace cs::features::sss_settings
{
	struct Settings
	{
		bool          enabled = true;
		float         surfaceThickness = 0.02f;
		float         bilinearThreshold = 0.02f;
		float         shadowContrast = 1.0f;
		std::uint32_t sampleCount = 1;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable screen-space contact shadows.", &Settings::enabled },
			settings::Field{ "surface_thickness", "Surface thickness used by the shadow ray march.", &Settings::surfaceThickness, settings::Range{ 0.005f, 0.05f } },
			settings::Field{ "bilinear_threshold", "Depth threshold for bilinear shadow sampling.", &Settings::bilinearThreshold, settings::Range{ 0.02f, 1.0f } },
			settings::Field{ "shadow_contrast", "Contrast of the screen-space shadow result.", &Settings::shadowContrast, settings::Range{ 0.0f, 4.0f } },
			settings::Field{ "sample_count", "Shadow ray sample-count multiplier.", &Settings::sampleCount,
				settings::Range{ sss_math::kMinSampleMultiplier, sss_math::kMaxSampleMultiplier } }
		}
	};
}

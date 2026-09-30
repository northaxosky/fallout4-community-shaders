#pragma once

#include "ScreenSpaceShadowsMath.h"
#include "Settings/SettingsSchema.h"

#include <cstdint>
#include <tuple>

namespace cs::features::sss_settings
{
	struct BendSettings
	{
		float SurfaceThickness = 0.02f;
		float BilinearThreshold = 0.02f;
		float ShadowContrast = 1.0f;
		std::uint32_t Enable = 1;
		std::uint32_t SampleCount = 1;
		std::uint32_t pad0[3]{};
	};
	static_assert(sizeof(BendSettings) == 32);
	using Settings = BendSettings;

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "Enable", "Enable screen-space contact shadows from the sun/moon direction.", &BendSettings::Enable, settings::Range{ 0u, 1u } },
			settings::Field{ "SurfaceThickness", "Assumed thickness of surfaces for shadow detection.", &BendSettings::SurfaceThickness, settings::Range{ 0.005f, 0.05f } },
			settings::Field{ "BilinearThreshold", "Depth threshold for edge detection during bilinear interpolation.", &BendSettings::BilinearThreshold, settings::Range{ 0.02f, 1.0f } },
			settings::Field{ "ShadowContrast", "Contrast boost for the shadow transition.", &BendSettings::ShadowContrast, settings::Range{ 0.0f, 4.0f } },
			settings::Field{ "SampleCount", "Resolution-adaptive shadow ray sample-count multiplier.", &BendSettings::SampleCount,
				settings::Range{ sss_math::kMinSampleMultiplier, sss_math::kMaxSampleMultiplier } } }
	};
}

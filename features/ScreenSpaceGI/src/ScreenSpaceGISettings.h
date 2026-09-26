#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::ssgi_settings
{
	struct Settings
	{
		bool  denoiseEnabled = true;
		float denoiseRadius = 2.0f;
		float aoRadius = 256.0f;
		float giRadius = 256.0f;
		float aoPower = 1.0f;
		float depthFadeStart = 40000.0f;
		float depthFadeEnd = 50000.0f;
		float bounceStrength = 1.0f;
		int   numSlices = 4;
		int   numSteps = 8;
		bool  enabled = true;
		bool  enableTemporalDenoiser = true;
		float depthDisocclusion = 0.1f;
		int   maxAccumFrames = 16;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "denoise_enabled", "Enable spatial denoising.", &Settings::denoiseEnabled },
			settings::Field{ "denoise_radius", "Spatial denoiser radius.", &Settings::denoiseRadius, {}, settings::Range{ 0.5f, 4.0f } },
			settings::Field{ "ao_radius", "Ambient occlusion sweep radius in game units.", &Settings::aoRadius, {}, settings::Range{ 16.0f, 512.0f } },
			settings::Field{ "gi_radius", "Indirect lighting sweep radius in game units.", &Settings::giRadius, {}, settings::Range{ 16.0f, 512.0f } },
			settings::Field{ "ao_power", "Ambient occlusion power.", &Settings::aoPower, {}, settings::Range{ 0.5f, 5.0f } },
			settings::Field{ "bounce_strength", "Indirect light bounce strength.", &Settings::bounceStrength, settings::Range{ 0.0f, 8.0f } },
			settings::Field{ "depth_fade_start", "Distance where the effect starts fading, in game units.", &Settings::depthFadeStart, {}, settings::Range{ 0.0f, 60000.0f } },
			settings::Field{ "depth_fade_end", "Distance where the effect finishes fading, in game units.", &Settings::depthFadeEnd, {}, settings::Range{ 0.0f, 80000.0f } },
			settings::Field{ "enabled", "Enable screen-space occlusion and indirect lighting.", &Settings::enabled },
			settings::Field{ "enable_temporal_denoiser", "Enable temporal reprojection of indirect light.", &Settings::enableTemporalDenoiser },
			settings::Field{ "depth_disocclusion", "Depth disocclusion threshold for temporal history.", &Settings::depthDisocclusion, settings::Range{ 0.0f, 0.2f } },
			settings::Field{ "num_slices", "Number of screen-space sweep slices.", &Settings::numSlices, settings::Range{ 1, 64 }, settings::Range{ 1, 8 } },
			settings::Field{ "num_steps", "Number of samples per sweep slice.", &Settings::numSteps, settings::Range{ 1, 64 }, settings::Range{ 4, 32 } },
			settings::Field{ "max_accum_frames", "Maximum accumulated temporal frames.", &Settings::maxAccumFrames, settings::Range{ 1, 255 }, settings::Range{ 1, 64 } }
		}
	};
}

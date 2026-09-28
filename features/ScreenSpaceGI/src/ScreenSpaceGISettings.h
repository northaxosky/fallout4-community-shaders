#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::ssgi_settings
{
	// Upstream d330bf12d defaults and ranges.
	struct Settings
	{
		bool  enabled = true;
		bool  enableGI = true;
		bool  enableVanillaSSAO = false;
		int   numSlices = 4;
		int   numSteps = 8;
		// 0 full, 1 half, 2 quarter.
		int   resolutionMode = 1;
		float minScreenRadius = 0.01f;
		float aoRadius = 256.0f;
		float giRadius = 256.0f;
		float thickness = 32.0f;
		float depthFadeStart = 40000.0f;
		float depthFadeEnd = 50000.0f;
		float giSaturation = 0.8f;
		float giDistanceCompensation = 0.0f;
		float aoPower = 1.0f;
		float giStrength = 1.0f;
		bool  enableTemporalDenoiser = true;
		bool  enableBlur = true;
		float depthDisocclusion = 0.1f;
		float normalDisocclusion = 0.1f;
		int   maxAccumFrames = 16;
		float blurRadius = 2.0f;
		float distanceNormalisation = 2.0f;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable screen-space global illumination.", &Settings::enabled },
			settings::Field{ "enable_gi", "Enable indirect lighting.", &Settings::enableGI },
			settings::Field{ "enable_vanilla_ssao", "Enable Fallout 4's built-in SSAO.", &Settings::enableVanillaSSAO },
			settings::Field{ "num_slices", "How many directions the samples take.", &Settings::numSlices, settings::Range{ 1, 64 }, settings::Range{ 1, 10 } },
			settings::Field{ "num_steps", "How many samples are taken in one direction.", &Settings::numSteps, settings::Range{ 1, 64 }, settings::Range{ 1, 20 } },
			settings::Field{ "resolution_mode", "Indirect lighting resolution: 0 full, 1 half, 2 quarter.", &Settings::resolutionMode, settings::Range{ 0, 2 } },
			settings::Field{ "min_screen_radius", "Minimum screen-space effect radius as a proportion of display width.", &Settings::minScreenRadius, {}, settings::Range{ 0.0f, 0.05f } },
			settings::Field{ "ao_radius", "Ambient occlusion radius in game units.", &Settings::aoRadius, {}, settings::Range{ 10.0f, 1024.0f } },
			settings::Field{ "gi_radius", "Indirect lighting radius in game units.", &Settings::giRadius, {}, settings::Range{ 10.0f, 1024.0f } },
			settings::Field{ "thickness", "Occluder thickness in game units; affects AO only.", &Settings::thickness, {}, settings::Range{ 0.0f, 128.0f } },
			settings::Field{ "depth_fade_start", "Distance where the effect starts fading, in game units.", &Settings::depthFadeStart, {}, settings::Range{ 10000.0f, 50000.0f } },
			settings::Field{ "depth_fade_end", "Distance where the effect finishes fading, in game units.", &Settings::depthFadeEnd, {}, settings::Range{ 10000.0f, 50000.0f } },
			settings::Field{ "gi_saturation", "Indirect lighting saturation.", &Settings::giSaturation, {}, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "gi_distance_compensation", "Brightens or dims further radiance samples.", &Settings::giDistanceCompensation, {}, settings::Range{ -5.0f, 5.0f } },
			settings::Field{ "ao_power", "Ambient occlusion power.", &Settings::aoPower, {}, settings::Range{ 0.0f, 6.0f } },
			settings::Field{ "gi_strength", "Indirect lighting source brightness.", &Settings::giStrength, {}, settings::Range{ 0.0f, 6.0f } },
			settings::Field{ "enable_temporal_denoiser", "Enable temporal denoising.", &Settings::enableTemporalDenoiser },
			settings::Field{ "enable_blur", "Enable the indirect lighting blur.", &Settings::enableBlur },
			settings::Field{ "depth_disocclusion", "Movement disocclusion threshold for temporal history.", &Settings::depthDisocclusion, {}, settings::Range{ 0.0f, 0.2f } },
			settings::Field{ "normal_disocclusion", "Normal disocclusion threshold for temporal history.", &Settings::normalDisocclusion },
			// Stored in R8.
			settings::Field{ "max_accum_frames", "Maximum accumulated temporal frames.", &Settings::maxAccumFrames, settings::Range{ 1, 255 }, settings::Range{ 1, 64 } },
			settings::Field{ "blur_radius", "Blur radius in pixels.", &Settings::blurRadius, {}, settings::Range{ 0.0f, 30.0f } },
			settings::Field{ "distance_normalisation", "Blur geometry weight.", &Settings::distanceNormalisation, {}, settings::Range{ 0.0f, 5.0f } }
		}
	};
}

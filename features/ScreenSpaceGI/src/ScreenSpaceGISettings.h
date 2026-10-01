#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::ssgi_settings
{
	// Upstream d330bf12d defaults and ranges.
	struct Settings
	{
		bool enabled = true;
		bool enableGI = true;
		bool enableExperimentalSpecularGI = false;
		bool enableVanillaSSAO = false;
		int numSlices = 4;
		int numSteps = 8;
		// 0 full, 1 half, 2 quarter.
		int resolutionMode = 1;
		float minScreenRadius = 0.01f;
		float aoRadius = 256.0f;
		float giRadius = 256.0f;
		float thickness = 32.0f;
		settings::Float2 depthFadeRange{ 40000.0f, 50000.0f };
		float giSaturation = 0.8f;
		float giDistanceCompensation = 0.0f;
		// FO4: placed-light interiors use the deliberate AO calibration (upstream default 1).
		float aoPower = 4.0f;
		float giStrength = 1.0f;
		bool enableTemporalDenoiser = true;
		bool enableBlur = true;
		float depthDisocclusion = 0.1f;
		float normalDisocclusion = 0.1f;
		int maxAccumFrames = 16;
		float blurRadius = 2.0f;
		float distanceNormalisation = 2.0f;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "Enabled", "Enable screen-space global illumination.", &Settings::enabled },
			settings::Field{ "EnableGI", "Enable indirect lighting.", &Settings::enableGI },
			settings::Field{ "EnableExperimentalSpecularGI", "Enable experimental unblurred specular indirect lighting.", &Settings::enableExperimentalSpecularGI },
			settings::Field{ "EnableVanillaSSAO", "Enable Fallout 4's built-in SSAO.", &Settings::enableVanillaSSAO },
			settings::Field{ "NumSlices", "How many directions the samples take.", &Settings::numSlices, settings::Range{ 1, 64 }, settings::Range{ 1, 10 } },
			settings::Field{ "NumSteps", "How many samples are taken in one direction.", &Settings::numSteps, settings::Range{ 1, 64 }, settings::Range{ 1, 20 } },
			settings::Field{ "ResolutionMode", "Indirect lighting resolution: 0 full, 1 half, 2 quarter.", &Settings::resolutionMode, settings::Range{ 0, 2 } },
			settings::Field{ "MinScreenRadius", "Minimum screen-space effect radius as a proportion of display width.", &Settings::minScreenRadius, {}, settings::Range{ 0.0f, 0.05f } },
			settings::Field{ "AORadius", "Ambient occlusion radius in game units.", &Settings::aoRadius, {}, settings::Range{ 10.0f, 1024.0f } },
			settings::Field{ "GIRadius", "Indirect lighting radius in game units.", &Settings::giRadius, {}, settings::Range{ 10.0f, 1024.0f } },
			settings::Field{ "Thickness", "Occluder thickness in game units; affects AO only.", &Settings::thickness, {}, settings::Range{ 0.0f, 128.0f } },
			settings::Float2Field<Settings>{ "DepthFadeRange", "Effect fade start/end distances in game units.", &Settings::depthFadeRange },
			settings::Field{ "GISaturation", "Indirect lighting saturation.", &Settings::giSaturation, {}, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "GIDistanceCompensation", "Brightens or dims further radiance samples.", &Settings::giDistanceCompensation, {}, settings::Range{ -5.0f, 5.0f } },
			settings::Field{ "AOPower", "Ambient occlusion power.", &Settings::aoPower, {}, settings::Range{ 0.0f, 12.0f } },
			settings::Field{ "GIStrength", "Indirect lighting source brightness.", &Settings::giStrength, {}, settings::Range{ 0.0f, 6.0f } },
			settings::Field{ "EnableTemporalDenoiser", "Enable temporal denoising.", &Settings::enableTemporalDenoiser },
			settings::Field{ "EnableBlur", "Enable the indirect lighting blur.", &Settings::enableBlur },
			settings::Field{ "DepthDisocclusion", "Movement disocclusion threshold for temporal history.", &Settings::depthDisocclusion, {}, settings::Range{ 0.0f, 0.2f } },
			settings::Field{ "NormalDisocclusion", "Normal disocclusion threshold for temporal history.", &Settings::normalDisocclusion },
			// Stored in R8.
			settings::Field{ "MaxAccumFrames", "Maximum accumulated temporal frames.", &Settings::maxAccumFrames, settings::Range{ 1, 255 }, settings::Range{ 1, 64 } },
			settings::Field{ "BlurRadius", "Blur radius in pixels.", &Settings::blurRadius, {}, settings::Range{ 0.0f, 30.0f } },
			settings::Field{ "DistanceNormalisation", "Blur geometry weight.", &Settings::distanceNormalisation, {}, settings::Range{ 0.0f, 5.0f } } }
	};
}

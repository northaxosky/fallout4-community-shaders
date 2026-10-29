#pragma once

#include "Settings/SettingsSchema.h"

#include <cstdint>
#include <string>

namespace cs::features::sky_sync
{
	enum class MoonSource : std::uint8_t
	{
		kBrightest,
		kMasser,
		kSecunda,
		kCount
	};

	enum class SunPathMode : std::uint8_t
	{
		kSouthern,
		kNorthern,
		kVanilla,
		kCustom,
		kCount
	};

	inline constexpr float kMaxHorizonFadeHours = 1.5f;
	inline constexpr float kMaxHorizonFadeElevation = 45.0f;
	inline constexpr float kMinHorizonElevation = -10.0f;

	struct Settings
	{
		bool UseAlternateSunPath = false;
		MoonSource MoonLightSource = MoonSource::kBrightest;
		SunPathMode SunPath = SunPathMode::kSouthern;
		float CustomAngle = -35.0f;
		float MinShadowElevation = 7.0f;
		float ShadowTransitionDuration = 300.0f;
		bool DimSunlightUnderHorizon = true;
		bool DimVolumetricLighting = true;
		float HorizonFadeHours = 0.7f;
		float HorizonFadeElevation = 7.0f;
		float HorizonElevation = -1.0f;
		float NewMoonIntensity = 0.05f;
		float CrescentMoonIntensity = 0.25f;
		float FullMoonIntensity = 1.0f;
		// Editor IDs; Skyrim's constant-light realms have no FO4 equivalent.
		settings::StringList DisabledWorldspaces;
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "UseAlternateSunPath", "Use alternate sun path", &Settings::UseAlternateSunPath },
			settings::Field{ "MoonLightSource", "Moon light source", &Settings::MoonLightSource,
				settings::Range{ MoonSource::kBrightest, MoonSource::kSecunda } },
			settings::Field{ "SunPath", "Sun path", &Settings::SunPath,
				settings::Range{ SunPathMode::kSouthern, SunPathMode::kCustom } },
			settings::Field{ "CustomAngle", "Custom angle", &Settings::CustomAngle, settings::Range{ -90.0f, 90.0f } },
			settings::Field{ "MinShadowElevation", "Min Shadow Elevation", &Settings::MinShadowElevation, settings::Range{ 0.0f, 45.0f } },
			settings::Field{ "ShadowTransitionDuration", "Shadow Transition Duration", &Settings::ShadowTransitionDuration, settings::Range{ 0.0f, 500.0f } },
			settings::Field{ "DimSunlightUnderHorizon", "Dim Sunlight Under Horizon", &Settings::DimSunlightUnderHorizon },
			settings::Field{ "DimVolumetricLighting", "Fade Volumetric Lighting", &Settings::DimVolumetricLighting },
			settings::Field{ "HorizonFadeHours", "Moonlight Fade Duration", &Settings::HorizonFadeHours, settings::Range{ 0.0f, kMaxHorizonFadeHours } },
			settings::Field{ "HorizonFadeElevation", "Horizon Fade Elevation", &Settings::HorizonFadeElevation, settings::Range{ 0.0f, kMaxHorizonFadeElevation } },
			settings::Field{ "HorizonElevation", "Effective Horizon", &Settings::HorizonElevation, settings::Range{ kMinHorizonElevation, 0.0f } },
			settings::Field{ "NewMoonIntensity", "New Moon Intensity", &Settings::NewMoonIntensity, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "CrescentMoonIntensity", "Crescent Intensity", &Settings::CrescentMoonIntensity, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "FullMoonIntensity", "Full Moon Intensity", &Settings::FullMoonIntensity, settings::Range{ 0.0f, 1.0f } },
			settings::StringListField<Settings>{ "DisabledWorldspaces", "Disabled Worldspaces", &Settings::DisabledWorldspaces } }
	};
}

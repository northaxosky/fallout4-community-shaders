#pragma once

#include "Settings/SettingsSchema.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace cs::features::image_based_lighting
{
	// Upstream IBL::Settings without static IBL and the world-map toggle.
	struct Settings
	{
		// Weather-only; the framework enabled state gates the feature.
		bool EnableIBL = true;
		bool PreserveFogLuminance = false;
		float DALCAmount = 1.0f;
		float EnvIBLScale = 1.0f;
		float SkyIBLScale = 1.0f;
		float EnvIBLSaturation = 1.0f;
		float SkyIBLSaturation = 1.0f;
		float FogAmount = 0.0f;
		// 0: Luminance Ratio, 1: Color Ratio, 2: DALC + Sky, 3: DALC + Sky (Directional)
		std::uint32_t DALCMode = 2;
		bool DisableInLoadingScreen = true;
	};

	inline constexpr std::array<std::string_view, 7> kWeatherVariables{
		"EnableIBL", "EnvIBLScale", "SkyIBLScale", "EnvIBLSaturation",
		"SkyIBLSaturation", "DALCAmount", "FogAmount"
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "EnvIBLScale", "Env IBL Scale", &Settings::EnvIBLScale, settings::Range{ 0.0f, 10.0f } }.Help("Intensity multiplier for the environment IBL (from Dynamic Cubemaps). Controls how strongly the surrounding environment contributes to ambient lighting."),
			settings::Field{ "SkyIBLScale", "Sky IBL Scale", &Settings::SkyIBLScale, settings::Range{ 0.0f, 10.0f } }.Help("Intensity multiplier for the sky IBL (from the host sky reflection cubemap). Controls how strongly the sky contributes to ambient lighting."),
			settings::Field{ "EnvIBLSaturation", "Env IBL Saturation", &Settings::EnvIBLSaturation, settings::Range{ 0.0f, 2.0f } }.Help("Color saturation of the environment IBL. Lower values produce more neutral ambient light; higher values produce more vivid color."),
			settings::Field{ "SkyIBLSaturation", "Sky IBL Saturation", &Settings::SkyIBLSaturation, settings::Range{ 0.0f, 2.0f } }.Help("Color saturation of the sky IBL. Lower values produce more neutral ambient light; higher values produce more vivid color."),
			settings::Field{ "DALCAmount", "DALC Amount", &Settings::DALCAmount, settings::Range{ 0.0f, 1.0f } }.Help("Blends the IBL brightness toward the game's vanilla ambient (DALC) level. 0 = no matching (pure IBL brightness), 1 = fully matched to vanilla ambient."),
			settings::Field{ "DALCMode", "DALC Mode", &Settings::DALCMode, settings::Range{ 0u, 3u } }.Help(
				"How the DALC-to-IBL brightness ratio is computed. "
				"Luminance Ratio: scalar ratio from overall luminance (loses DALC color tint). "
				"Color Ratio: per-channel ratio (preserves DALC color tint). "
				"DALC + Sky: uses vanilla ambient as base, sky IBL on top; Skylighting only affects sky. "
				"DALC + Sky (Directional): same, but Skylighting also dims vanilla ambient per-direction."),
			settings::Field{ "FogAmount", "Fog Mix", &Settings::FogAmount, settings::Range{ 0.0f, 1.0f } }.Help("Blends the fog color toward the IBL ambient color. 0 = vanilla fog, 1 = fog fully tinted by IBL."),
			settings::Field{ "PreserveFogLuminance", "Preserve Fog Luminance", &Settings::PreserveFogLuminance }.Help("When Fog Mix is active, rescales the IBL-tinted fog to keep the original fog brightness. Prevents fog from becoming too bright or too dark."),
			settings::Field{ "DisableInLoadingScreen", "Disable in loading screens", &Settings::DisableInLoadingScreen }.Help("Disables IBL during loading screens and the main menu.") }
	};

	inline constexpr settings::Schema kWeatherSchema{
		std::tuple_cat(
			kSchema.fields,
			std::tuple{ settings::Field{ "EnableIBL", "Enable IBL", &Settings::EnableIBL } })
	};
}

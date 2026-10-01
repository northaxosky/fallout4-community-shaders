#pragma once

#include "Settings/SettingsSchema.h"

namespace cs::features::exponential_height_fog
{
	struct Settings
	{
		std::uint32_t enabled = 0, useDynamicCubemaps = 1;
		float startDistance = 0.0f, fogHeight = 0.0f, fogHeightFalloff = 0.2f, fogDensity = 0.005f;
		float directionalInscatteringMultiplier = 1.0f, directionalInscatteringAnisotropy = 0.2f;
		settings::Color4 inscatteringTint{ 1, 1, 1, 1 };
		float cubemapMipLevel = 8.0f, sunlightAttenuationAmount = 1.0f;
		std::uint32_t respectVanillaFogFade = 0, disableVanillaFog = 1;
		settings::Color4 fogInscatteringColor{ 0, 0, 0, 1 };
		float originalFogColorAmount = 0.0f;
		std::uint32_t volumetricFogEnabled = 0, volumetricGridPixelSize = 16, volumetricGridSizeZ = 64;
		float volumetricFogDistance = 60000.0f, volumetricFogStartDistance = 0.0f;
		float volumetricFogNearFadeInDistance = 1000.0f, volumetricFogExtinctionScale = 1.0f;
		settings::Color4 volumetricFogAlbedo{ 1, 1, 1, 1 }, volumetricFogEmissive{ 0, 0, 0, 0 };
		float volumetricDirectionalScatteringIntensity = 1.0f, volumetricShadowBias = 0.002f;
		float volumetricDepthDistributionScale = 8.0f, volumetricSkyLightingIntensity = 1.0f;
		float volumetricFogScatteringDistribution = 0.2f, volumetricHistoryWeight = 0.96f;
		std::uint32_t volumetricHistoryMissSampleCount = 4;
		float volumetricSampleJitterMultiplier = 0.0f, volumetricUpsampleJitterMultiplier = 1.0f;
		float volumetricLocalLightScatteringIntensity = 1.0f;
		float pad0[2]{};
	};
	static_assert(sizeof(Settings) == 192);
	inline constexpr std::array<std::string_view, 23> kWeatherVariables{
		"startDistance", "fogHeight", "fogHeightFalloff", "fogInscatteringColor",
		"originalFogColorAmount", "fogDensity", "directionalInscatteringMultiplier",
		"sunlightAttenuationAmount", "directionalInscatteringAnisotropy", "inscatteringTint",
		"respectVanillaFogFade", "disableVanillaFog", "volumetricFogEnabled",
		"volumetricFogDistance", "volumetricFogStartDistance", "volumetricFogNearFadeInDistance",
		"volumetricFogExtinctionScale", "volumetricFogScatteringDistribution",
		"volumetricDirectionalScatteringIntensity", "volumetricFogAlbedo", "volumetricFogEmissive",
		"volumetricSkyLightingIntensity", "volumetricLocalLightScatteringIntensity"
	};

	inline constexpr settings::Schema kSchema{
		std::tuple{
			settings::Field{ "enabled", "Enable Exponential Height Fog", &Settings::enabled, settings::Range{ 0u, 1u } },
			settings::Field{ "startDistance", "Start Distance", &Settings::startDistance, settings::Range{ 0.0f, 100000.0f } },
			settings::Field{ "fogHeight", "Fog Height", &Settings::fogHeight, settings::Range{ -22000.0f, 22000.0f } },
			settings::Field{ "fogHeightFalloff", "Fog Height Falloff", &Settings::fogHeightFalloff, settings::Range{ 0.001f, 2.0f } },
			settings::ColorField<Settings>{ "fogInscatteringColor", "Fog Inscattering Color", &Settings::fogInscatteringColor },
			settings::Field{ "originalFogColorAmount", "Original Fog Color Amount", &Settings::originalFogColorAmount, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "fogDensity", "Fog Density", &Settings::fogDensity, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "directionalInscatteringMultiplier", "Directional Light Inscattering Multiplier", &Settings::directionalInscatteringMultiplier, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "sunlightAttenuationAmount", "Sunlight Attenuation Amount", &Settings::sunlightAttenuationAmount, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "directionalInscatteringAnisotropy", "Directional Light Inscattering Anisotropy", &Settings::directionalInscatteringAnisotropy, settings::Range{ -0.99f, 0.99f } },
			settings::Field{ "disableVanillaFog", "Disable Vanilla Fog", &Settings::disableVanillaFog, settings::Range{ 0u, 1u } },
			settings::Field{ "respectVanillaFogFade", "Apply Vanilla Fade", &Settings::respectVanillaFogFade, settings::Range{ 0u, 1u } },
			settings::Field{ "useDynamicCubemaps", "Use Dynamic Cubemaps for Inscattering", &Settings::useDynamicCubemaps, settings::Range{ 0u, 1u } },
			settings::ColorField<Settings>{ "inscatteringTint", "Inscattering Cubemap Tint", &Settings::inscatteringTint },
			settings::Field{ "cubemapMipLevel", "Cubemap Mip Level", &Settings::cubemapMipLevel, settings::Range{ 1.0f, 8.0f } },
			settings::Field{ "volumetricFogEnabled", "Enable Volumetric Fog", &Settings::volumetricFogEnabled, settings::Range{ 0u, 1u } },
			settings::Field{ "volumetricFogDistance", "Volumetric View Distance", &Settings::volumetricFogDistance, settings::Range{ 1000.0f, 200000.0f } },
			settings::Field{ "volumetricFogStartDistance", "Volumetric Start Distance", &Settings::volumetricFogStartDistance, settings::Range{ 0.0f, 200000.0f }, settings::Range{ 0.0f, 20000.0f } },
			settings::Field{ "volumetricFogNearFadeInDistance", "Near Fade In Distance", &Settings::volumetricFogNearFadeInDistance, settings::Range{ 0.0f, 20000.0f } },
			settings::Field{ "volumetricFogExtinctionScale", "Volumetric Extinction Scale", &Settings::volumetricFogExtinctionScale, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "volumetricFogScatteringDistribution", "Volumetric Scattering Distribution", &Settings::volumetricFogScatteringDistribution, settings::Range{ -0.9f, 0.9f } },
			settings::ColorField<Settings>{ "volumetricFogAlbedo", "Volumetric Albedo", &Settings::volumetricFogAlbedo },
			settings::ColorField<Settings>{ "volumetricFogEmissive", "Volumetric Emissive", &Settings::volumetricFogEmissive },
			settings::Field{ "volumetricDirectionalScatteringIntensity", "Directional Scattering Intensity", &Settings::volumetricDirectionalScatteringIntensity, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "volumetricSkyLightingIntensity", "Sky Lighting Scattering Intensity", &Settings::volumetricSkyLightingIntensity, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "volumetricLocalLightScatteringIntensity", "Local Light Scattering Intensity", &Settings::volumetricLocalLightScatteringIntensity, settings::Range{ 0.0f, 100.0f }, settings::Range{ 0.0f, 10.0f } },
			settings::Field{ "volumetricGridPixelSize", "Grid Pixel Size", &Settings::volumetricGridPixelSize, settings::Range{ 4u, 64u } },
			settings::Field{ "volumetricGridSizeZ", "Grid Depth Slices", &Settings::volumetricGridSizeZ, settings::Range{ 16u, 160u } },
			settings::Field{ "volumetricShadowBias", "Directional Shadow Bias", &Settings::volumetricShadowBias, settings::Range{ 0.0f, 0.05f } },
			settings::Field{ "volumetricDepthDistributionScale", "Depth Distribution Scale", &Settings::volumetricDepthDistributionScale, settings::Range{ 1.0f, 128.0f } },
			settings::Field{ "volumetricHistoryWeight", "Temporal History Weight", &Settings::volumetricHistoryWeight, settings::Range{ 0.0f, 0.99f } },
			settings::Field{ "volumetricHistoryMissSampleCount", "History Miss Samples", &Settings::volumetricHistoryMissSampleCount, settings::Range{ 1u, 16u } },
			settings::Field{ "volumetricSampleJitterMultiplier", "Sample Jitter Multiplier", &Settings::volumetricSampleJitterMultiplier, settings::Range{ 0.0f, 1.0f } },
			settings::Field{ "volumetricUpsampleJitterMultiplier", "Upsample Jitter Multiplier", &Settings::volumetricUpsampleJitterMultiplier, settings::Range{ 0.0f, 1.0f } } }
	};
}

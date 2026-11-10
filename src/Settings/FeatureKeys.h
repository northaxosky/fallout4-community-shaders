#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace cs::feature_config
{
	inline constexpr std::string_view kDynamicCubemaps = "DynamicCubemaps";
	inline constexpr std::string_view kImageBasedLighting = "ImageBasedLighting";
	inline constexpr std::string_view kCloudShadows = "CloudShadows";
	inline constexpr std::string_view kMotionVectorFixes = "MotionVectorFixes";
	inline constexpr std::string_view kUpscaling = "Upscaling";
	inline constexpr std::string_view kFrameGeneration = "FrameGeneration";
	inline constexpr std::string_view kHDRDisplay = "HDRDisplay";
	inline constexpr std::string_view kRenderDoc = "RenderDoc";

	inline constexpr std::array<std::string_view, 26> kAllFeatureKeys{
		"ScreenSpaceGI",
		"SubsurfaceScattering",
		"Skylighting",
		"LightLimitFix",
		kDynamicCubemaps,
		kImageBasedLighting,
		"WaterEffects",
		"UnifiedWater",
		"ScreenSpaceShadows",
		"TerrainShadows",
		"SkySync",
		"VolumetricShadows",
		kCloudShadows,
		"LODBlending",
		"TerrainVariation",
		"TerrainBlending",
		"ExtendedMaterials",
		kMotionVectorFixes,
		kUpscaling,
		kFrameGeneration,
		kHDRDisplay,
		"PerformanceOverlay",
		kRenderDoc,
		"Screenshot",
		"RemoteControl",
		"CSEditor"
	};

	// Activation follows this order, and IBL fails to load before Dynamic Cubemaps.
	static_assert(std::ranges::find(kAllFeatureKeys, kDynamicCubemaps) < std::ranges::find(kAllFeatureKeys, kImageBasedLighting));
}

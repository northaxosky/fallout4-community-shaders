#pragma once

namespace cs::engine::shader_injection_defines
{
	inline constexpr auto kSubstrate = "FO4CS_SUBSTRATE";
	inline constexpr auto kRuntimeOG = "OG";

	inline constexpr auto kScreenSpaceShadows = "SCREEN_SPACE_SHADOWS";
	inline constexpr auto kScreenSpaceGi = "SSGI";
	inline constexpr auto kLightLimitFix = "LIGHT_LIMIT_FIX";
	inline constexpr auto kDynamicCubemaps = "DYNAMIC_CUBEMAPS";
	inline constexpr auto kTerrainShadows = "TERRAIN_SHADOWS";
	inline constexpr auto kTerrainShadowsFullscreenDebug =
		"TERRAIN_SHADOWS_FULLSCREEN_DEBUG";
	inline constexpr auto kWaterEffects = "WATER_EFFECTS";
	inline constexpr auto kWaterEffectsFullscreenDebug =
		"WATER_EFFECTS_FULLSCREEN_DEBUG";
}

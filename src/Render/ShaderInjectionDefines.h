#pragma once

namespace cs::engine::shader_injection_defines
{
	inline constexpr auto kSubstrate = "FO4CS_SUBSTRATE";

	inline constexpr auto kScreenSpaceShadows = "SCREEN_SPACE_SHADOWS";
	inline constexpr auto kScreenSpaceGi = "SSGI";
	inline constexpr auto kWetnessEffects = "WETNESS_EFFECTS";
	inline constexpr auto kWetnessEffectsFullscreenDebug =
		"WETNESS_EFFECTS_FULLSCREEN_DEBUG";
	inline constexpr auto kInverseSquareLighting = "INVERSE_SQUARE_LIGHTING";
	inline constexpr auto kDynamicCubemaps = "DYNAMIC_CUBEMAPS";
	inline constexpr auto kDynamicCubemapsFullscreenDebug =
		"DYNAMIC_CUBEMAPS_FULLSCREEN_DEBUG";
	inline constexpr auto kTerrainShadows = "TERRAIN_SHADOWS";
	inline constexpr auto kTerrainShadowsFullscreenDebug =
		"TERRAIN_SHADOWS_FULLSCREEN_DEBUG";
	inline constexpr auto kWaterEffects = "WATER_EFFECTS";
	inline constexpr auto kWaterEffectsFullscreenDebug =
		"WATER_EFFECTS_FULLSCREEN_DEBUG";
}

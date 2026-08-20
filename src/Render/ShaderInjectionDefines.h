#pragma once

namespace cs::engine::shader_injection_defines
{
	// System define; injected for any stage-matching contribution.
	inline constexpr auto kSubstrate = "FO4CS_SUBSTRATE";

	inline constexpr auto kScreenSpaceShadows = "SCREEN_SPACE_SHADOWS";
	inline constexpr auto kScreenSpaceGi = "SSGI";
	inline constexpr auto kWetnessEffects = "WETNESS_EFFECTS";
}

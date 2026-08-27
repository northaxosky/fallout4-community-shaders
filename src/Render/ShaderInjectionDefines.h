#pragma once

namespace cs::engine::shader_injection_defines
{
	// injected for every contributed stage
	inline constexpr auto kSubstrate = "FO4CS_SUBSTRATE";

	inline constexpr auto kScreenSpaceShadows = "SCREEN_SPACE_SHADOWS";
	inline constexpr auto kScreenSpaceGi = "SSGI";
	inline constexpr auto kWetnessEffects = "WETNESS_EFFECTS";
	inline constexpr auto kUpscalingMipBias = "UPSCALING_MIP_BIAS";
}

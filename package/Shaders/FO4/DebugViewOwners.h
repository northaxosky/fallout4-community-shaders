#ifndef FO4_DEBUG_VIEW_OWNERS_H
#define FO4_DEBUG_VIEW_OWNERS_H

#ifdef __cplusplus
#	include <cstdint>
#endif

namespace FullscreenDebugOwner
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		None = 0,
		ExponentialHeightFog = 2,
		TerrainShadows = 3,
		WaterEffects = 4;
}
#endif

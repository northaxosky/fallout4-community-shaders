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
		TerrainShadows = 3,
		WaterEffects = 4,
		Skylighting = 5,
		DynamicCubemaps = 6,
		LightLimitFix = 7,
		VanillaPBR = 8,
		TruePBR = 9;
}

namespace DynamicCubemapsDebugMode
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		DirectionCube = 1,
		EngineCube = 2;
}

namespace TruePBRDebugMode
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		Classified = 1,
		Record = 2,
		BaseColor = 3,
		Roughness = 4,
		MetallicF0 = 5,
		AmbientOcclusion = 6,
		ParallaxOffset = 7;
}
#endif

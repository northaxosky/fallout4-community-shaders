#ifndef FO4_PREPASS_LANE_H
#define FO4_PREPASS_LANE_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// The per-draw lane the host bakes into the prepass cb2 pad (PrepassFeatureLane, uint4):
// .x draw flags, .y material record id (low 16 bits; 0 is none), .zw zero.
namespace PrepassLaneFlag
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		ObjectLOD = 1,
		TerrainVariation = 2,
		ExtendedMaterials = 4,
		HumanSkin = 8;
}

namespace PrepassLaneRecord
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		IdMask = 0xFFFF;
}
#endif

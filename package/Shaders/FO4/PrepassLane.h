#ifndef FO4_PREPASS_LANE_H
#define FO4_PREPASS_LANE_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// cb2 pad lane: .x flags, .y record id, .z emissive scale bits, .w zero.
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
#endif

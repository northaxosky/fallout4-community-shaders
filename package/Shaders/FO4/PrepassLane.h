#ifndef FO4_PREPASS_LANE_H
#define FO4_PREPASS_LANE_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// Per-draw lane in the prepass cb2 pad: .x flags, .y record id, .zw zero.
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

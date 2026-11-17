#ifndef FO4_PREPASS_LANE_H
#define FO4_PREPASS_LANE_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// cb2 pad lane: .x flags, .y record id, .z emissive bits, .w textures.
namespace PrepassLaneTexture
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		// Record texture i: bit 1 << i of .w, pixel slot FirstSlot + i.
		FirstSlot = 4,
		Displacement = 1;
}

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

#ifndef FO4_PREPASS_LANE_H
#define FO4_PREPASS_LANE_H

#ifdef __cplusplus
#	include <cstdint>
#endif

// cb2 pad lane: .x flags, .y record id, .z emissive scale bits, .w bound record textures.
namespace PrepassLaneTexture
{
#ifdef __cplusplus
	inline constexpr std::uint32_t
#else
	static const uint
#endif
		// Bits of .w; bit i marks record texture i as bound.
		Displacement = 1,
		Features0 = 2,
		Features1 = 4,
		// Pixel shader slots of record textures 0..2.
		DisplacementSlot = 4,
		Features0Slot = 5,
		Features1Slot = 6;
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

#ifndef FO4_VANILLA_PBR_BLUR_MATCH_HLSLI
#define FO4_VANILLA_PBR_BLUR_MATCH_HLSLI

// Sizes are compile defines from features\VanillaPBR\src\BlurMatchConstants.h.
namespace BlurMatch
{
	static const uint kGrid = BLUR_MATCH_GRID;
	static const uint kBlock = BLUR_MATCH_BLOCK;
	static const uint kGroupsPerSide = BLUR_MATCH_GROUPS_PER_SIDE;
	static const uint kGroupsPerFace = kGroupsPerSide * kGroupsPerSide;

	uint PartialIndex(uint target, uint mip, uint face, uint group)
	{
		return ((target * BLUR_MATCH_MAX_MIPS + mip) * 6 + face) * kGroupsPerFace + group;
	}
}

#endif

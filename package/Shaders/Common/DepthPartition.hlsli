#ifndef DEPTH_PARTITION_HLSLI_INCLUDED
#define DEPTH_PARTITION_HLSLI_INCLUDED
#include "FO4/Depth.hlsli"

namespace DepthPartition
{
	// FO4 merges first-person and world depths into separate projection ranges.
	bool IsNear(float rawDepth)
	{
		return FO4Depth::IsFirstPerson(rawDepth);
	}

	float ToProjectionDepth(float rawDepth, bool isNear)
	{
		return FO4Depth::ProjectionDepth(rawDepth, isNear);
	}
}

#endif

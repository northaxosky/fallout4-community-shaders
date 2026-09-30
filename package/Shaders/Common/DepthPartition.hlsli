#ifndef DEPTH_PARTITION_HLSLI_INCLUDED
#define DEPTH_PARTITION_HLSLI_INCLUDED

namespace DepthPartition
{
	// FO4 merges first-person and world depths into separate projection ranges.
	bool IsNear(float rawDepth)
	{
		return rawDepth <= 0.01;
	}

	float ToProjectionDepth(float rawDepth, bool isNear)
	{
		return isNear ? rawDepth * 100.0 : rawDepth * 1.01 - 0.01;
	}
}

#endif

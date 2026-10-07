#ifndef FO4_SKYLIGHTING_CONSUMER
#define FO4_SKYLIGHTING_CONSUMER

#include "FO4/FO4ShaderData.hlsli"

namespace Skylighting
{
#ifdef SKYLIGHTING_FULLSCREEN_DEBUG
	// FO4: composite families share no normal input; a producer isolates visibility.
	bool TryGetDebugColor(float2 pixelPosition, out float4 color)
	{
		color = 0.0;
		if (FO4SharedData::DebugOwner != FullscreenDebugOwner::Skylighting || FO4SharedData::DebugMode == 0)
			return false;
		uint2 dimensions;
		FO4SharedData::DebugTexture.GetDimensions(dimensions.x, dimensions.y);
		if (any(dimensions == 0))
			return false;
		color = FO4SharedData::DebugTexture.Load(int3(min(uint2(pixelPosition), dimensions - 1), 0));
		return true;
	}
#endif
}
#endif

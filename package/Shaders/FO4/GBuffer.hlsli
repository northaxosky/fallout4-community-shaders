#ifndef FO4_GBUFFER_HLSLI
#define FO4_GBUFFER_HLSLI

#include "Common/GBuffer.hlsli"

namespace FO4GBuffer
{
	// Native sphere-map XY is not upstream octahedral encoding.
	float3 DecodeViewNormal(float2 encoded)
	{
		float2 f = encoded * 4.0 - 2.0;
		float lengthSquared = dot(f, f);
		if (lengthSquared > 4.0)
			return float3(0, 0, -1);
		return float3(f * sqrt(1.0 - lengthSquared * 0.25), lengthSquared * 0.5 - 1.0);
	}
}
#endif

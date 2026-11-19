#ifndef FO4_GBUFFER_HLSLI
#define FO4_GBUFFER_HLSLI

#include "Common/GBuffer.hlsli"
#include "FO4/SphereMapNormal.hlsli"

namespace FO4GBuffer
{
	// Native sphere-map XY is not upstream octahedral encoding.
	float3 DecodeViewNormal(float2 encoded)
	{
		float2 f = UnpackSphereMapXY(encoded);
		float lengthSquared = dot(f, f);
		return float3(f * sqrt(1.0 - lengthSquared * 0.25), lengthSquared * 0.5 - 1.0);
	}
}
#endif

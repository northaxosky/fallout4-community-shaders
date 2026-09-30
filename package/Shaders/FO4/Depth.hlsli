#ifndef FO4_DEPTH_HLSLI
#define FO4_DEPTH_HLSLI

namespace FO4Depth
{
	bool IsFirstPerson(float depth) { return depth <= 0.01; }
	float ProjectionDepth(float depth, bool firstPerson)
	{
		return firstPerson ? depth * 100.0 : mad(depth, 1.01, -0.01);
	}
	float ProjectionDepth(float depth)
	{
		return ProjectionDepth(depth, IsFirstPerson(depth));
	}

	float CanonicalDepth(float depth, float2 ndc,
		row_major float4x4 nearInverse, row_major float4x4 worldProjection)
	{
		if (depth >= 1.0)
			return 1.0;
		float projected = ProjectionDepth(depth);
		if (!IsFirstPerson(depth))
			return projected;
		float4 view = mul(nearInverse, float4(ndc, projected, 1.0));
		float4 clip = mul(worldProjection, view);
		return saturate(clip.z / clip.w);
	}
}
#endif

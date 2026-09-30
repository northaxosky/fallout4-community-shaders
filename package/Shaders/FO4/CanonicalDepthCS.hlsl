#include "FO4/Depth.hlsli"

cbuffer CanonicalDepthData : register(b0)
{
	row_major float4x4 NearInverse;
	row_major float4x4 WorldProjection;
	uint2 Allocation;
	uint2 Active;
};

Texture2D<float> PartitionedDepth : register(t0);
RWTexture2D<float> CanonicalDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	if (any(pixel >= Allocation))
		return;
	float depth = 1.0;
	if (all(pixel < Active)) {
		float2 uv = (float2(pixel) + 0.5) / float2(Active);
		depth = FO4Depth::CanonicalDepth(PartitionedDepth.Load(int3(pixel, 0)),
			uv * float2(2.0, -2.0) + float2(-1.0, 1.0), NearInverse, WorldProjection);
	}
	CanonicalDepth[pixel] = depth;
}

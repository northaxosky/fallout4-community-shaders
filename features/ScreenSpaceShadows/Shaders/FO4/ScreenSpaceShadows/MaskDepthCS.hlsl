#include "Common/SharedData.hlsli"
#include "FO4/Depth.hlsli"

Texture2D<float> PartitionedDepth : register(t0);
RWTexture2D<float> WorldShadowDepth : register(u0);

[numthreads(8, 8, 1)] void main(uint2 pixel : SV_DispatchThreadID) {
	uint2 allocation;
	WorldShadowDepth.GetDimensions(allocation.x, allocation.y);
	if (any(pixel >= allocation))
		return;
	// FO4: keep first-person geometry from casting world contact shadows.
	WorldShadowDepth[pixel] = FO4Depth::IsFirstPerson(PartitionedDepth.Load(int3(pixel, 0))) ? 1.0 : SharedData::DepthTexture.Load(int3(pixel, 0));
}

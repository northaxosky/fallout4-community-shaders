Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> Mip0 : register(u0);
RWTexture2D<float> Mip1 : register(u1);

groupshared float GroupMinimum[64];

[numthreads(8, 8, 1)]
void main(
	uint2 groupThreadID : SV_GroupThreadID,
	uint2 dispatchThreadID : SV_DispatchThreadID)
{
	uint groupIndex = groupThreadID.y * 8 + groupThreadID.x;
	float4 dispatchCoordinate = (float4)dispatchThreadID.xyyy;
	uint2 sourceCoordinate =
		(uint2)(dispatchCoordinate.xw + dispatchCoordinate.xw);

	float upperLeft = SourceDepth.Load(int3(sourceCoordinate, 0));
	uint2 right = sourceCoordinate + uint2(1, 0);
	uint2 below = sourceCoordinate + uint2(0, 1);
	float lowerLeft = SourceDepth.Load(int3(below, 0));
	float upperRight = SourceDepth.Load(int3(right, 0));
	sourceCoordinate += 1;
	float lowerRight = SourceDepth.Load(int3(sourceCoordinate, 0));

	float upperMinimum = min(upperLeft, lowerLeft);
	float lowerMinimum = min(upperRight, lowerRight);
	float minimum = min(upperMinimum, lowerMinimum);

	GroupMinimum[groupIndex] = minimum;
	GroupMemoryBarrierWithGroupSync();
	Mip0[dispatchThreadID] = minimum;

	uint2 parity = dispatchThreadID & uint2(1, 1);
	if (parity.x == 0 && parity.y == 0)
	{
		uint3 neighborIndices = groupIndex + uint3(1, 8, 9);
		float neighbor0 = GroupMinimum[neighborIndices.x];
		float neighbor1 = GroupMinimum[neighborIndices.y];
		float neighbor2 = GroupMinimum[neighborIndices.z];
		float firstReduced = min(minimum, neighbor0);
		float secondReduced = min(neighbor1, neighbor2);
		float reduced = min(firstReduced, secondReduced);

		uint2 mipCoordinate = (uint2)(dispatchCoordinate * 0.5);
		Mip1[mipCoordinate] = reduced;
	}
}

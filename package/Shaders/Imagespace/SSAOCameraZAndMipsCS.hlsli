cbuffer CameraZAndMipsParameters : register(b0)
{
	float4 CameraZParameters;
};

Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> CameraZ : register(u0);
RWTexture2D<float> CameraZMip0 : register(u1);
RWTexture2D<float> CameraZMip1 : register(u2);

groupshared float GroupMinimum[64];

[numthreads(8, 8, 1)]
void main(
	uint2 groupThreadID : SV_GroupThreadID,
	uint2 dispatchThreadID : SV_DispatchThreadID)
{
	float4 dispatchCoordinate = (float4)dispatchThreadID.xyyy;
	uint2 sourceCoordinate =
		(uint2)(dispatchCoordinate.xw + dispatchCoordinate.xw);
	uint2 right = sourceCoordinate + uint2(1, 0);
	uint2 below = sourceCoordinate + uint2(0, 1);
	uint2 lowerRightCoordinate = sourceCoordinate + uint2(1, 1);

	float upperLeft = CameraZParameters.x /
		(CameraZParameters.y * SourceDepth.Load(int3(sourceCoordinate, 0)) +
		 CameraZParameters.z);
	float upperRight = CameraZParameters.x /
		(CameraZParameters.y * SourceDepth.Load(int3(right, 0)) +
		 CameraZParameters.z);
	float lowerLeft = CameraZParameters.x /
		(CameraZParameters.y * SourceDepth.Load(int3(below, 0)) +
		 CameraZParameters.z);
	float lowerRight = CameraZParameters.x /
		(CameraZParameters.y *
			 SourceDepth.Load(int3(lowerRightCoordinate, 0)) +
		 CameraZParameters.z);

	float upperMinimum = min(upperLeft, upperRight);
	float lowerMinimum = min(lowerLeft, lowerRight);
	float minimum = min(upperMinimum, lowerMinimum);

	CameraZ[sourceCoordinate] = upperLeft;
	CameraZ[right] = upperRight;
	CameraZ[below] = lowerLeft;
	CameraZ[lowerRightCoordinate] = lowerRight;

	uint groupIndex = groupThreadID.y * 8 + groupThreadID.x;
	GroupMinimum[groupIndex] = minimum;
	GroupMemoryBarrierWithGroupSync();
	CameraZMip0[dispatchThreadID] = minimum;

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
		CameraZMip1[mipCoordinate] = reduced;
	}
}

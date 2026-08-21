// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#include "common.hlsli"

Texture2D<float> srcViewDepth : register(t0);

RWTexture2D<float> outDepth0 : register(u0);
RWTexture2D<float> outDepth1 : register(u1);
RWTexture2D<float> outDepth2 : register(u2);
RWTexture2D<float> outDepth3 : register(u3);
RWTexture2D<float> outDepth4 : register(u4);

float ClampDepth(float depth)
{
	return clamp(depth, 0.0, 3.402823466e+38);
}

float DepthMIPFilter(float depth0, float depth1, float depth2, float depth3)
{
#ifdef LINEAR_FILTER
	float4 depths = float4(depth0, depth1, depth2, depth3);
	float4 valid = float4(
		depth0 > FP_Z ? 1.0 : 0.0,
		depth1 > FP_Z ? 1.0 : 0.0,
		depth2 > FP_Z ? 1.0 : 0.0,
		depth3 > FP_Z ? 1.0 : 0.0);
	float validCount = dot(valid, float4(1.0, 1.0, 1.0, 1.0));
	return dot(depths, valid) / max(validCount, 1.0);
#elif defined(MAX_FILTER)
	return max(max(depth0, depth1), max(depth2, depth3));
#elif defined(MIN_FILTER)
	return min(min(depth0, depth1), min(depth2, depth3));
#endif
}

groupshared float g_scratchDepths[8][8];

void StoreDepth0(uint2 coord, float depth)
{
	if (all(coord < uint2(FrameDim)))
		outDepth0[coord] = depth;
}

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
	const uint2 baseCoord = dispatchThreadID;
	const uint2 pixCoord = baseCoord * 2;
	const int2 maxCoord = max(int2(FrameDim) - 1, int2(0, 0));
	const int2 coord0 = min(int2(pixCoord) + int2(0, 0), maxCoord);
	const int2 coord1 = min(int2(pixCoord) + int2(1, 0), maxCoord);
	const int2 coord2 = min(int2(pixCoord) + int2(0, 1), maxCoord);
	const int2 coord3 = min(int2(pixCoord) + int2(1, 1), maxCoord);

	float depth0 = ClampDepth(srcViewDepth.Load(int3(coord0, 0)));
	float depth1 = ClampDepth(srcViewDepth.Load(int3(coord1, 0)));
	float depth2 = ClampDepth(srcViewDepth.Load(int3(coord2, 0)));
	float depth3 = ClampDepth(srcViewDepth.Load(int3(coord3, 0)));
	StoreDepth0(pixCoord + uint2(0, 0), depth0);
	StoreDepth0(pixCoord + uint2(1, 0), depth1);
	StoreDepth0(pixCoord + uint2(0, 1), depth2);
	StoreDepth0(pixCoord + uint2(1, 1), depth3);

	float dm1 = DepthMIPFilter(depth0, depth1, depth2, depth3);
	if (all(baseCoord < MipFrameDim(1)))
		outDepth1[baseCoord] = dm1;
	g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm1;

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 1];
		float inBR = g_scratchDepths[groupThreadID.x + 1][groupThreadID.y + 1];

		float dm2 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 2 < MipFrameDim(2)))
			outDepth2[baseCoord / 2] = dm2;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm2;
	}

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 2];
		float inBR = g_scratchDepths[groupThreadID.x + 2][groupThreadID.y + 2];

		float dm3 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 4 < MipFrameDim(3)))
			outDepth3[baseCoord / 4] = dm3;
		g_scratchDepths[groupThreadID.x][groupThreadID.y] = dm3;
	}

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float inTL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 0];
		float inTR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 0];
		float inBL = g_scratchDepths[groupThreadID.x + 0][groupThreadID.y + 4];
		float inBR = g_scratchDepths[groupThreadID.x + 4][groupThreadID.y + 4];

		float dm4 = DepthMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 8 < MipFrameDim(4)))
			outDepth4[baseCoord / 8] = dm4;
	}
}

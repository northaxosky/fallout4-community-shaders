// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#include "../Common/Math.hlsli"
#include "common.hlsli"

// Mip 0 is written by the g-buffer decode pass; this fills the rest of the chain.
Texture2D<float4> srcViewNormal : register(t0);

RWTexture2D<float4> outNormal1 : register(u0);
RWTexture2D<float4> outNormal2 : register(u1);
RWTexture2D<float4> outNormal3 : register(u2);
RWTexture2D<float4> outNormal4 : register(u3);

float3 NormalMIPFilter(float3 normal0, float3 normal1, float3 normal2, float3 normal3)
{
	float3 avg = normal0 + normal1 + normal2 + normal3;
	float lenSq = dot(avg, avg);
	return lenSq > EPSILON_LENGTH_SQ ? avg * rsqrt(lenSq) : float3(0, 0, -1);
}

groupshared float3 g_scratchNormal[8][8];

[numthreads(8, 8, 1)]
void main(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID)
{
	const uint2 baseCoord = dispatchThreadID;
	const int2 pixCoord = int2(baseCoord * 2);
	// Clamp to the active rectangle so the chain never averages stale texels.
	const int2 maxCoord = max(int2(OUT_FRAME_DIM) - 1, int2(0, 0));

	float3 normal0 = srcViewNormal[min(pixCoord + int2(0, 0), maxCoord)].xyz;
	float3 normal1 = srcViewNormal[min(pixCoord + int2(1, 0), maxCoord)].xyz;
	float3 normal2 = srcViewNormal[min(pixCoord + int2(0, 1), maxCoord)].xyz;
	float3 normal3 = srcViewNormal[min(pixCoord + int2(1, 1), maxCoord)].xyz;

	float3 nm1 = NormalMIPFilter(normal0, normal1, normal2, normal3);
	if (all(baseCoord < MipFrameDim(1)))
		outNormal1[baseCoord] = float4(nm1, 0);
	g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm1;

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float3 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchNormal[groupThreadID.x + 1][groupThreadID.y + 0];
		float3 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 1];
		float3 inBR = g_scratchNormal[groupThreadID.x + 1][groupThreadID.y + 1];

		float3 nm2 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 2 < MipFrameDim(2)))
			outNormal2[baseCoord / 2] = float4(nm2, 0);
		g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm2;
	}

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float3 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchNormal[groupThreadID.x + 2][groupThreadID.y + 0];
		float3 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 2];
		float3 inBR = g_scratchNormal[groupThreadID.x + 2][groupThreadID.y + 2];

		float3 nm3 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 4 < MipFrameDim(3)))
			outNormal3[baseCoord / 4] = float4(nm3, 0);
		g_scratchNormal[groupThreadID.x][groupThreadID.y] = nm3;
	}

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float3 inTL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchNormal[groupThreadID.x + 4][groupThreadID.y + 0];
		float3 inBL = g_scratchNormal[groupThreadID.x + 0][groupThreadID.y + 4];
		float3 inBR = g_scratchNormal[groupThreadID.x + 4][groupThreadID.y + 4];

		float3 nm4 = NormalMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 8 < MipFrameDim(4)))
			outNormal4[baseCoord / 8] = float4(nm4, 0);
	}
}

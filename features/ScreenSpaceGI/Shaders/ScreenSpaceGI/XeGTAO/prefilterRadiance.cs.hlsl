// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#include "common.hlsli"

Texture2D<float3> srcRadiance : register(t0);

RWTexture2D<float3> outRadiance0 : register(u0);
RWTexture2D<float3> outRadiance1 : register(u1);
RWTexture2D<float3> outRadiance2 : register(u2);
RWTexture2D<float3> outRadiance3 : register(u3);
RWTexture2D<float3> outRadiance4 : register(u4);

float3 RadianceMIPFilter(float3 radiance0, float3 radiance1, float3 radiance2, float3 radiance3)
{
	return (radiance0 + radiance1 + radiance2 + radiance3) * 0.25;
}

groupshared float3 g_scratchRadiance[8][8];

void StoreRadiance0(uint2 coord, float3 radiance)
{
	if (all(coord < uint2(FrameDim)))
		outRadiance0[coord] = radiance;
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

	float3 radiance0 = srcRadiance.Load(int3(coord0, 0));
	float3 radiance1 = srcRadiance.Load(int3(coord1, 0));
	float3 radiance2 = srcRadiance.Load(int3(coord2, 0));
	float3 radiance3 = srcRadiance.Load(int3(coord3, 0));

	StoreRadiance0(pixCoord + uint2(0, 0), radiance0);
	StoreRadiance0(pixCoord + uint2(1, 0), radiance1);
	StoreRadiance0(pixCoord + uint2(0, 1), radiance2);
	StoreRadiance0(pixCoord + uint2(1, 1), radiance3);

	float3 rm1 = RadianceMIPFilter(radiance0, radiance1, radiance2, radiance3);
	if (all(baseCoord < MipFrameDim(1)))
		outRadiance1[baseCoord] = rm1;
	g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm1;

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 2) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 1][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 1];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 1][groupThreadID.y + 1];

		float3 rm2 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 2 < MipFrameDim(2)))
			outRadiance2[baseCoord / 2] = rm2;
		g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm2;
	}

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 4) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 2][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 2];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 2][groupThreadID.y + 2];

		float3 rm3 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 4 < MipFrameDim(3)))
			outRadiance3[baseCoord / 4] = rm3;
		g_scratchRadiance[groupThreadID.x][groupThreadID.y] = rm3;
	}

	GroupMemoryBarrierWithGroupSync();

	[branch] if (all((groupThreadID.xy % 8) == 0))
	{
		float3 inTL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 0];
		float3 inTR = g_scratchRadiance[groupThreadID.x + 4][groupThreadID.y + 0];
		float3 inBL = g_scratchRadiance[groupThreadID.x + 0][groupThreadID.y + 4];
		float3 inBR = g_scratchRadiance[groupThreadID.x + 4][groupThreadID.y + 4];

		float3 rm4 = RadianceMIPFilter(inTL, inTR, inBL, inBR);
		if (all(baseCoord / 8 < MipFrameDim(4)))
			outRadiance4[baseCoord / 8] = rm4;
	}
}

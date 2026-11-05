// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer ISSAOBlurCSConstants : register(b0)
{
	float4 g_ScreenInfos;
};

Texture2D<float4> source : register(t0);
RWTexture2D<float4> Output : register(u0);

static const int R = 3;
static const int SCALE = 2;
static const int APRON = R * SCALE;
static const int TILE_SIZE = GRID_SIZE - 2 * APRON;
static const float EDGE_SHARPNESS = 1.0;
static const float gaussian[R + 1] = { 0.153170, 0.144893, 0.122649, 0.092902 };

groupshared float2 SharedValueKey[GRID_SIZE];

float unpackKey(float2 p)
{
	return dot(p, float2(256.0 / 257.0, 1.0 / 257.0));
}

#	ifdef AXIS_H
[numthreads(GRID_SIZE, 1, 1)]
#	else
[numthreads(1, GRID_SIZE, 1)]
#	endif
	void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID,
		uint3 dispatchId : SV_DispatchThreadID) {
#	ifdef AXIS_H
		uint thread = groupThreadId.x;
		int local = thread - APRON;
		uint size = g_ScreenInfos.x;
		uint2 position = uint2(groupId.x * TILE_SIZE + local, groupId.y);
		uint along = position.x;
#	else
		uint thread = groupThreadId.y;
		int local = thread - APRON;
		uint size = g_ScreenInfos.y;
		uint2 position = uint2(groupId.x, groupId.y * TILE_SIZE + local);
		uint along = position.y;
#	endif
		float4 temp = source.Load(int3(position, 0));
		float key = unpackKey(temp.yz);
		SharedValueKey[thread] = float2(temp.x, key);
		GroupMemoryBarrierWithGroupSync();
		if (local >= 0 && local < TILE_SIZE && along < size) {
			if (key == 1.0) {
				Output[dispatchId.xy] = 0;
				return;
			}
			float sum = temp.x;
			float totalWeight = gaussian[0];
			sum *= totalWeight;
			[unroll] for (int r = -R; r <= R; ++r)
			{
				if (r != 0) {
					float2 tap = SharedValueKey[thread + r * SCALE];
					float weight = 0.3 + gaussian[abs(r)];
					weight *= max(0.0, 1.0 - (EDGE_SHARPNESS * 2000.0) * abs(tap.y - key));
					sum += tap.x * weight;
					totalWeight += weight;
				}
			}
			const float epsilon = 0.0001;
#	ifdef AXIS_H
			temp.x = sum / (totalWeight + epsilon);
			temp.w = 0;
			Output[position] = temp;
#	else
			Output[position] = sum / (totalWeight + epsilon);
#	endif
		}
	}
#endif

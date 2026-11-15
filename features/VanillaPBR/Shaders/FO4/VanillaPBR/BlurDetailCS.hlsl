#include "FO4/VanillaPBR/BlurMatch.hlsli"

// Cube faces as array slices, so bilinear sampling clamps at face edges.
Texture2DArray<float4> Faces : register(t0);
SamplerState LinearClamp : register(s0);
RWStructuredBuffer<float4> Partials : register(u0);

cbuffer DetailCB : register(b0)
{
	// x target, y mip, z first face slice; six Z groups per job.
	uint4 Jobs[BLUR_MATCH_MAX_JOBS];
};

groupshared float3 Sums[64];

float Luminance(float2 position, float slice, float mip)
{
	float3 color = Faces.SampleLevel(LinearClamp, float3(position / BlurMatch::kGrid, slice), mip).rgb;
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)] void main(uint3 groupId : SV_GroupID, uint3 threadId : SV_GroupThreadID, uint threadIndex : SV_GroupIndex) {
	const uint4 job = Jobs[groupId.z / 6];
	const uint face = groupId.z % 6;
	const float slice = float(job.z + face);
	const float mip = float(job.y);
	// Sums relative to the face center keep the variance precise in float.
	const float shift = Luminance(BlurMatch::kGrid * 0.5, slice, mip);
	const uint2 origin = (groupId.xy * 8 + threadId.xy) * BlurMatch::kBlock;
	const bool right = origin.x + BlurMatch::kBlock < BlurMatch::kGrid;
	const bool below = origin.y + BlurMatch::kBlock < BlurMatch::kGrid;

	float3 sums = 0.0;
	float above[BlurMatch::kBlock];
	[unroll] for (uint column = 0; column < BlurMatch::kBlock; ++column)
		above[column] = 0.0;
	// The block plus one texel right and below; differences stay within the face.
	[unroll] for (uint row = 0; row <= BlurMatch::kBlock; ++row)
	{
		[branch] if (row < BlurMatch::kBlock || below)
		{
			float current[BlurMatch::kBlock + 1];
			[unroll] for (uint column = 0; column <= BlurMatch::kBlock; ++column)
				current[column] = Luminance(float2(origin + uint2(column, row)) + 0.5, slice, mip) - shift;
			[unroll] for (uint column = 0; column < BlurMatch::kBlock; ++column)
			{
				if (row < BlurMatch::kBlock) {
					sums.x += current[column];
					sums.y += current[column] * current[column];
					float dx = current[column + 1] - current[column];
					if (column + 1 < BlurMatch::kBlock || right)
						sums.z += dx * dx;
				}
				float dy = current[column] - above[column];
				if (row > 0)
					sums.z += dy * dy;
				above[column] = current[column];
			}
		}
	}

	Sums[threadIndex] = sums;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint stride = 32; stride > 0; stride >>= 1)
	{
		if (threadIndex < stride)
			Sums[threadIndex] += Sums[threadIndex + stride];
		GroupMemoryBarrierWithGroupSync();
	}
	if (threadIndex == 0)
		Partials[BlurMatch::PartialIndex(job.x, job.y, face, groupId.y * BlurMatch::kGroupsPerSide + groupId.x)] = float4(Sums[0], shift);
}

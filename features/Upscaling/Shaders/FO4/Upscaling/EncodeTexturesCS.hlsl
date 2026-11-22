// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) Skyrim Community Shaders contributors
// Ported from Skyrim Community Shaders.

#include "FO4/FO4ShaderData.hlsli"
#include "FO4/Upscaling/MaterialClass.hlsli"

cbuffer UpscalingData : register(b0)
{
	float2 TrueSamplingDim;
	float2 pad0;
};

Texture2D<float4> MaterialClassMask : register(t0);
Texture2D<float2> MotionVectorMask : register(t1);
Texture2D<float> DepthMask : register(t2);

RWTexture2D<float> TransparencyCompositionMask : register(u0);
RWTexture2D<float2> MotionVectorOutput : register(u1);
#if defined(DEPTH_OUTPUT)
RWTexture2D<float> DepthOutput : register(u2);
#endif

#if defined(DLSS)
#	define DILATE_TILE 12
groupshared float TileDepth[DILATE_TILE * DILATE_TILE];
groupshared float2 TileMotion[DILATE_TILE * DILATE_TILE];
#endif

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID, uint3 groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
#if defined(DLSS)
	const int2 tileOrigin = int2(groupID.xy) * 8 - 2;
	[unroll] for (uint t = groupIndex; t < DILATE_TILE * DILATE_TILE; t += 64)
	{
		int2 tilePos = tileOrigin + int2(t % DILATE_TILE, t / DILATE_TILE);
		TileDepth[t] = DepthMask[tilePos];
		TileMotion[t] = MotionVectorMask[tilePos];
	}
	GroupMemoryBarrierWithGroupSync();
#endif

	// Bounds check
	if (any(dispatchID.xy >= uint2(TrueSamplingDim)))
		return;

	// FO4 translation: water stencil classes 2 and 3 stand in for water normal.z.
	const float transparencyCompositionMask = IsWaterClass(MaterialClassMask[dispatchID.xy].a) ? 1.0 : 0.0;

#if defined(DLSS)
	const uint centerIndex = (groupThreadID.y + 2) * DILATE_TILE + groupThreadID.x + 2;
	float depth = TileDepth[centerIndex];
	float nearFactor = smoothstep(4096.0 * 2.5, 0.0, SharedData::GetScreenDepth(depth));

	// Find longest motion vector in 5x5 neighborhood
	float2 motionVector = TileMotion[centerIndex];
	float2 longestMotionVector = motionVector;
	float maxMotionLengthSq = dot(motionVector, motionVector);

	[unroll] for (int y = -2; y <= 2; y++)
	{
		[unroll] for (int x = -2; x <= 2; x++)
		{
			int2 samplePos = int2(dispatchID.xy) + int2(x, y);

			// Bounds check
			if (any(samplePos < 0) || any(samplePos >= int2(TrueSamplingDim)))
				continue;

			const uint neighborIndex = centerIndex + y * DILATE_TILE + x;
			float neighborDepth = TileDepth[neighborIndex];

			// Take neighbor if it's longer AND closer
			if (neighborDepth < depth) {
				float2 neighborMotionVector = TileMotion[neighborIndex];

				// Square motion vector for length
				float motionLengthSq = dot(neighborMotionVector, neighborMotionVector);

				if (motionLengthSq > maxMotionLengthSq) {
					maxMotionLengthSq = motionLengthSq;
					longestMotionVector = neighborMotionVector;
				}
			}
		}
	}

	MotionVectorOutput[dispatchID.xy] = lerp(longestMotionVector, motionVector, nearFactor);
#elif defined(FSR)
	MotionVectorOutput[dispatchID.xy] = MotionVectorMask[dispatchID.xy];
#endif

#if defined(DEPTH_OUTPUT)
	// Providers need typed R32_FLOAT depth rather than the engine's typeless depth resource.
	DepthOutput[dispatchID.xy] = DepthMask[dispatchID.xy];
#endif

	TransparencyCompositionMask[dispatchID.xy] = transparencyCompositionMask;
}

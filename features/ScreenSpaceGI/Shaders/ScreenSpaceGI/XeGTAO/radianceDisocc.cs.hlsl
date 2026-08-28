// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) Skyrim Community Shaders contributors
// Ported from Skyrim Community Shaders.
// Gathers deferred radiance and reprojects the previous indirect result.

#include "../Common/Math.hlsli"
#include "common.hlsli"

Texture2D<float3> srcSourceA : register(t0);
Texture2D<float3> srcSourceB : register(t1);
Texture2D<float> srcCurrDepth : register(t2);
Texture2D<float2> srcMotionVec : register(t3);
Texture2D<float3> srcPrevGeo : register(t4);
Texture2D<unorm float> srcAccumFrames : register(t5);
Texture2D<float4> srcPrevIlY : register(t6);
Texture2D<float2> srcPrevIlCoCg : register(t7);

RWTexture2D<float3> outRadiance : register(u0);
RWTexture2D<unorm float> outAccumFrames : register(u1);
RWTexture2D<float4> outRemappedIlY : register(u2);
RWTexture2D<float2> outRemappedIlCoCg : register(u3);

void ReadHistory(
	float currDepth, float3 currPos, int2 pixCoord, float weight,
	inout float4 prevY, inout float2 prevCoCg, inout float accumFrames, inout float wsum)
{
	if (any(pixCoord < 0) || any(pixCoord >= int2(PrevFrameDim)))
		return;

	const float prevDepth = srcPrevGeo[pixCoord].x;
	if (prevDepth <= FP_Z)
		return;

	// Cheap reject before the world-space reconstruction; wider so parallax survives.
	if (abs(currDepth - prevDepth) > currDepth * DepthDisocclusion * 3)
		return;

	const float2 prevUV = (pixCoord + 0.5) * RcpPrevFrameDim;
	const float3 prevView = PreviousScreenToViewPosition(prevUV, prevDepth);
	const float3 prevPos = ViewToCameraRelativeWorld(prevView, PrevViewToWorld) +
		(CameraOrigin(PrevViewToWorld) - CameraOrigin(ViewToWorld));

	const float3 deltaPos = currPos - prevPos;
	const float movementThreshold = currDepth * DepthDisocclusion;
	if (dot(deltaPos, deltaPos) >= movementThreshold * movementThreshold)
		return;

	prevY += srcPrevIlY[pixCoord] * weight;
	prevCoCg += srcPrevIlCoCg[pixCoord] * weight;
	accumFrames += srcAccumFrames[pixCoord] * weight;
	wsum += weight;
}

[numthreads(8, 8, 1)]
void main(const uint2 pixCoord : SV_DispatchThreadID)
{
	if (any(pixCoord >= uint2(OUT_FRAME_DIM)))
		return;

	const float2 uv = (pixCoord + 0.5) * RCP_OUT_FRAME_DIM;
	const float currDepth = READ_DEPTH(srcCurrDepth, pixCoord);

	// The first-person partition has no matching projection, so it seeds fresh every frame.
	if (currDepth <= FP_Z) {
		outRadiance[pixCoord] = 0;
		outAccumFrames[pixCoord] = 1.0 / 255.0;
		outRemappedIlY[pixCoord] = 0;
		outRemappedIlCoCg[pixCoord] = 0;
		return;
	}

	// Raw deferred radiance: no transfer conversion, no albedo.
	float3 radiance = max(0.0, srcSourceA.SampleLevel(samplerPointClamp, uv * RadianceScale, 0));
	if (IncludeSourceB())
		radiance += max(0.0, srcSourceB.SampleLevel(samplerPointClamp, uv * RadianceScale, 0));
	outRadiance[pixCoord] = radiance * 3.0;

	float4 prevY = 0;
	float2 prevCoCg = 0;
	float accumFrames = 0;
	float wsum = 0;
	float2 motion = 0;

	const bool reproject =
		TemporalEnabled() && HistoryValid() && currDepth <= DepthFadeRange.y;
	if (reproject) {
		motion = srcMotionVec[pixCoord];
		const float2 prevUV = uv + motion;
		if (!(any(prevUV < 0) || any(prevUV > 1))) {
			const float3 currPos =
				ViewToCameraRelativeWorld(ScreenToViewPosition(uv, currDepth), ViewToWorld);

			const float2 prevPxCoord = prevUV * PrevFrameDim;
			const int2 prevPxLU = int2(floor(prevPxCoord - 0.5));
			const float2 bilinearWeights = prevPxCoord - 0.5 - prevPxLU;

			ReadHistory(currDepth, currPos, prevPxLU,
				(1 - bilinearWeights.x) * (1 - bilinearWeights.y),
				prevY, prevCoCg, accumFrames, wsum);
			ReadHistory(currDepth, currPos, prevPxLU + int2(1, 0),
				bilinearWeights.x * (1 - bilinearWeights.y),
				prevY, prevCoCg, accumFrames, wsum);
			ReadHistory(currDepth, currPos, prevPxLU + int2(0, 1),
				(1 - bilinearWeights.x) * bilinearWeights.y,
				prevY, prevCoCg, accumFrames, wsum);
			ReadHistory(currDepth, currPos, prevPxLU + int2(1, 1),
				bilinearWeights.x * bilinearWeights.y,
				prevY, prevCoCg, accumFrames, wsum);
		}
	}

	if (!TemporalEnabled()) {
		outAccumFrames[pixCoord] = 1.0 / 255.0;
		outRemappedIlY[pixCoord] = 0;
		outRemappedIlCoCg[pixCoord] = 0;
		return;
	}

	if (wsum > 1e-2) {
		const float rcpWsum = rcp(wsum + EPSILON_WEIGHT_SUM);
		prevY *= rcpWsum;
		prevCoCg *= rcpWsum;
		accumFrames *= rcpWsum;
	} else {
		// The gathered accumulation stays raw so the disocclusion halving below is live.
		prevY = 0;
		prevCoCg = 0;
	}

	// Disocclusion halves rather than resets, softening the blend flash.
	float prevAccum = accumFrames * 255;
	if (wsum < 1e-2)
		prevAccum = prevAccum * 0.5;

	// Fast motion makes history less trustworthy, so cap accumulation with it.
	const float motionMaxAccum = lerp(
		(float)MaxAccumFrames,
		min((float)MaxAccumFrames, max(MaxAccumFrames * 0.25, 4)),
		saturate(length(motion) * 20));

	outAccumFrames[pixCoord] = max(1, min(prevAccum + 1, motionMaxAccum)) / 255.0;
	outRemappedIlY[pixCoord] = prevY;
	outRemappedIlCoCg[pixCoord] = prevCoCg;
}

// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#include "../Common/FastMath.hlsli"
#include "../Common/Math.hlsli"
#include "common.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float3> srcNormal : register(t1);
Texture2D<unorm float2> srcNoise : register(t2);

RWTexture2D<unorm float> outAo : register(u0);

float GetDepthFade(float depth)
{
	return saturate((depth - DepthFadeRange.x) * DepthFadeScaleConst);
}

float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
	uint2 noiseCoord = (pixCoord % 128) + uint2(0, (temporalIndex % 64) * 128);
	return srcNoise.Load(uint3(noiseCoord, 0));
}

void CalculateGI(
	uint2 dtid, float2 uv, float viewspaceZ, float3 viewspaceNormal,
	out float o_ao)
{
	const float2 frameScale = FrameDim * RcpTexDim;

	float2 normalizedScreenPos = uv;

	const float rcpNumSlices = rcp((float)NumSlices);
	const float rcpNumSteps = rcp((float)NumSteps);

	const float pixelTooCloseThreshold = 1.3;
	const float2 pixelDirRBViewspaceSizeAtCenterZ = viewspaceZ.xx * NDCToViewMul.xy * RCP_OUT_FRAME_DIM;

	float screenspaceRadius = EffectRadius / pixelDirRBViewspaceSizeAtCenterZ.x;
	screenspaceRadius = max(MinScreenRadius, screenspaceRadius);
	const float minS = pixelTooCloseThreshold / screenspaceRadius;

	uint2 noiseCoord = uint2(normalizedScreenPos * OUT_FRAME_DIM);
	const float2 localNoise = SpatioTemporalNoise(noiseCoord, FrameIndex);
	const float noiseSlice = localNoise.x;
	const float noiseStep = localNoise.y;

	const float3 pixCenterPos = ScreenToViewPosition(normalizedScreenPos, viewspaceZ);
	const float3 viewVec = normalize(-pixCenterPos);

	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float visibility = 0;

	for (uint slice = 0; slice < NumSlices; slice++) {
		float phi = (Math::PI * rcpNumSlices) * (slice + noiseSlice);
		float3 directionVec = 0;
		sincos(phi, directionVec.y, directionVec.x);

		float2 omega = float2(directionVec.x, -directionVec.y) * screenspaceRadius;

		const float logLenOmega = 0.5 * log2(max(dot(omega, omega), EPSILON_LENGTH_SQ));

		const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
		const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

		float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
		float rcpProjectedNormalVecLength = rsqrt(max(dot(projectedNormalVec, projectedNormalVec), EPSILON_LENGTH_SQ));
		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcpProjectedNormalVecLength);

		float n = signNorm * FastMath::ACos(cosNorm);

		uint bitmask = 0;

		float stepNoise = frac(noiseStep + slice * 0.6180339887498948482);

		[unroll] for (int sideSign = -1; sideSign <= 1; sideSign += 2)
		{
			[loop] for (uint step = 0; step < NumSteps; step++)
			{
				float s = (step + stepNoise) * rcpNumSteps;
				s *= s;
				s += minS;

				float2 sampleOffset = s * omega;

				float2 samplePxCoord = dtid + .5 + sampleOffset * sideSign;
				float2 sampleUV = samplePxCoord * RCP_OUT_FRAME_DIM;

				float2 sampleScreenPos = sampleUV;
				[branch] if (any(sampleScreenPos > 1.0) || any(sampleScreenPos < 0.0)) continue;

				float mipLevel = clamp(log2(s) + logLenOmega - 3.3, 0, 5);

				float SZ = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel);
				if (SZ <= FP_Z) continue;

				float3 samplePos = ScreenToViewPosition(sampleScreenPos, SZ);
				float3 sampleDelta = samplePos - pixCenterPos;
				float3 sampleHorizonVec = normalize(sampleDelta);

				float3 sampleBackHorizonVec = normalize(sampleDelta - viewVec * Thickness);

				float angleFront = FastMath::ACos(dot(sampleHorizonVec, viewVec));
				float angleBack = FastMath::ACos(dot(sampleBackHorizonVec, viewVec));
				float2 angleRange = -sideSign * (sideSign == -1 ? float2(angleFront, angleBack) : float2(angleBack, angleFront));
				angleRange = smoothstep(0, 1, (angleRange + n) * Math::INV_PI + .5);

				uint2 bitsRange = uint2(round(angleRange.x * 32u), round((angleRange.y - angleRange.x) * 32u));
				uint maskedBits = s < AORadius ? ((1 << bitsRange.y) - 1) << bitsRange.x : 0;

				bitmask |= maskedBits;
			}
		}

		visibility += countbits(bitmask) * 0.03125;
	}

	float depthFade = GetDepthFade(viewspaceZ);

	visibility *= rcpNumSlices;
	visibility = lerp(saturate(visibility), 0, depthFade);

	o_ao = visibility;
}

[numthreads(8, 8, 1)]
void main(const uint2 dtid : SV_DispatchThreadID)
{
	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + .5) * RCP_OUT_FRAME_DIM;

	float viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);
	float3 viewspaceNormal = srcNormal[pxCoord];

	viewspaceZ *= 0.99920h;

	float currAo = 0;

	bool needGI = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (needGI) {
		CalculateGI(pxCoord, uv, viewspaceZ, viewspaceNormal, currAo);
	}

	// Stored AO is occlusion: 0 is open and 1 is fully occluded.
	outAo[pxCoord] = currAo;
}

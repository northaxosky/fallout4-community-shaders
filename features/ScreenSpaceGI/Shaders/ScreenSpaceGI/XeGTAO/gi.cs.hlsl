// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation
// Ported from Skyrim Community Shaders d330bf12d, with additional edits by FiveLimbedCat/ProfJack.
//
// Screen Space Indirect Lighting with Visibility Bitmask: https://arxiv.org/abs/2301.11376

#include "ScreenSpaceGI/Common/FastMath.hlsli"
#include "ScreenSpaceGI/Common/Math.hlsli"
#ifdef GI
#	include "Common/SphericalHarmonics.hlsli"
#	include "ScreenSpaceGI/Common/Color.hlsli"
#endif
#include "ScreenSpaceGI/XeGTAO/common.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float2> srcNormal : register(t1);
#ifdef GI
Texture2D<float3> srcRadiance : register(t2);
#endif
Texture2D<unorm float2> srcNoise : register(t3);
#ifdef GI
Texture2D<unorm float> srcAccumFrames : register(t4);
Texture2D<float4> srcPrevY : register(t5);
Texture2D<float2> srcPrevCoCg : register(t6);
#endif

RWTexture2D<unorm float> outAo : register(u0);
#ifdef GI
RWTexture2D<float4> outY : register(u1);
RWTexture2D<float2> outCoCg : register(u2);
RWTexture2D<float3> outPrevGeo : register(u3);
#endif

float GetDepthFade(float depth)
{
	return saturate((depth - DepthFadeRange.x) * DepthFadeScaleConst);
}

// noise texture from https://github.com/electronicarts/fastnoise, 128x128x64
float2 SpatioTemporalNoise(uint2 pixCoord, uint temporalIndex)
{
	uint2 noiseCoord = (pixCoord % 128) + uint2(0, (temporalIndex % 64) * 128);
	return srcNoise.Load(uint3(noiseCoord, 0));
}

void CalculateGI(
	uint2 dtid, float2 uv, float viewspaceZ, float3 viewspaceNormal,
	out float o_ao
#ifdef GI
	,
	out float4 o_currY, out float2 o_currCoCg
#endif
)
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

	// flip foliage normal
	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float visibility = 0;
#ifdef GI
	float4 radianceY = 0;
	float2 radianceCoCg = 0;
#endif

	for (uint slice = 0; slice < NumSlices; slice++) {
		float phi = (Math::PI * rcpNumSlices) * (slice + noiseSlice);
		float3 directionVec = 0;
		sincos(phi, directionVec.y, directionVec.x);

		float2 omega = float2(directionVec.x, -directionVec.y) * screenspaceRadius;

		// log2(s * |omega|) = log2(s) + logLenOmega.
		const float logLenOmega = 0.5 * log2(max(dot(omega, omega), EPSILON_LENGTH_SQ));

		const float3 orthoDirectionVec = directionVec - (dot(directionVec, viewVec) * viewVec);
		const float3 axisVec = normalize(cross(orthoDirectionVec, viewVec));

		float3 projectedNormalVec = viewspaceNormal - axisVec * dot(viewspaceNormal, axisVec);
		float rcpProjectedNormalVecLength = rsqrt(max(dot(projectedNormalVec, projectedNormalVec), EPSILON_LENGTH_SQ));
		float signNorm = sign(dot(orthoDirectionVec, projectedNormalVec));
		float cosNorm = saturate(dot(projectedNormalVec, viewVec) * rcpProjectedNormalVecLength);

		float n = signNorm * FastMath::ACos(cosNorm);

		uint bitmask = 0;
#ifdef GI
		uint bitmaskGI = 0;
#endif

		// R1 sequence (http://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/)
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
				float mipLevelRadiance = mipLevel;
#if defined(HALF_RES)
				mipLevel = max(mipLevel, 1);
				mipLevelRadiance = max(mipLevelRadiance, 2);
#elif defined(QUARTER_RES)
				mipLevel = max(mipLevel, 2);
				mipLevelRadiance = max(mipLevelRadiance, 3);
#else
				mipLevelRadiance = max(mipLevelRadiance, 1);
#endif

				float SZ = srcWorkingDepth.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel);

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

#ifdef GI
				// GI uses a fixed 300-unit thickness.
				float3 sampleBackHorizonVecGI = normalize(sampleDelta - viewVec * 300);
				float angleBackGI = FastMath::ACos(dot(sampleBackHorizonVecGI, viewVec));
				float2 angleRangeGI = -sideSign * (sideSign == -1 ? float2(angleFront, angleBackGI) : float2(angleBackGI, angleFront));

				angleRangeGI = smoothstep(0, 1, (angleRangeGI + n) * Math::INV_PI + .5);

				uint2 bitsRangeGI = uint2(round(angleRangeGI.x * 32u), round((angleRangeGI.y - angleRangeGI.x) * 32u));
				uint maskedBitsGI = s < GIRadius ? ((1 << bitsRangeGI.y) - 1) << bitsRangeGI.x : 0;

				uint validBits = maskedBitsGI & ~bitmaskGI;
				bool checkGI = validBits;

				if (checkGI) {
					float giBoost = 4.0 * Math::PI * (1 + GIDistanceCompensation * smoothstep(0, GICompensationMaxDist, s * EffectRadius));

					float3 normalSample = GBuffer::DecodeNormal(srcNormal.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevelRadiance));
					if (dot(samplePos, normalSample) > 0)
						normalSample = -normalSample;
					float frontBackMult = -dot(normalSample, sampleHorizonVec);
					frontBackMult = frontBackMult < 0 ? 0.0 : frontBackMult;  // backface

					if (frontBackMult > 0.f) {
						float3 sampleHorizonVecWS = ViewToWorldDirection(sampleHorizonVec, ViewToWorld);

						float3 sampleRadiance = srcRadiance.SampleLevel(samplerPointClamp, sampleUV * OUT_FRAME_SCALE, mipLevelRadiance).rgb * frontBackMult * giBoost * countbits(validBits) * 0.03125;
						sampleRadiance = max(sampleRadiance, 0);
						float3 sampleRadianceYCoCg = Color::RGBToYCoCg(sampleRadiance);

						radianceY += sampleRadianceYCoCg.r * SphericalHarmonics::Evaluate(sampleHorizonVecWS);
						radianceCoCg += sampleRadianceYCoCg.gb;
					}
				}
#endif  // GI

				bitmask |= maskedBits;
#ifdef GI
				bitmaskGI |= maskedBitsGI;
#endif
			}
		}

		visibility += countbits(bitmask) * 0.03125;
	}

	float depthFade = GetDepthFade(viewspaceZ);

	visibility *= rcpNumSlices;
	visibility = lerp(saturate(visibility), 0, depthFade);
	visibility = 1 - pow(abs(1 - visibility), AOPower);

#ifdef GI
	radianceY *= rcpNumSlices;
	radianceY = lerp(radianceY, 0, depthFade);

	radianceCoCg *= rcpNumSlices * GISaturation;
#endif

	o_ao = visibility;
#ifdef GI
	o_currY = radianceY;
	o_currCoCg = radianceCoCg;
#endif
}

[numthreads(8, 8, 1)] void main(const uint2 dtid : SV_DispatchThreadID) {
	if (any(dtid >= uint2(OUT_FRAME_DIM)))
		return;

	const float2 frameScale = FrameDim * RcpTexDim;

	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + .5) * RCP_OUT_FRAME_DIM;

	float viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);
	float3 viewspaceNormal = GBuffer::DecodeNormal(FULLRES_LOAD(srcNormal, pxCoord, uv * OUT_FRAME_SCALE, samplerLinearClamp));

#ifdef GI
	outPrevGeo[pxCoord] = float3(
		clamp(viewspaceZ, 0.0, R11_MAX_DEPTH),
		GBuffer::EncodeNormal(ViewToWorldDirection(viewspaceNormal, ViewToWorld)));
#endif

	// Bias toward the camera against depth imprecision.
	viewspaceZ *= 0.99920h;

	float currAo = 0;
#ifdef GI
	float4 currY = 0;
	float2 currCoCg = 0;
#endif

	bool needGI = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (needGI) {
		CalculateGI(
			pxCoord, uv, viewspaceZ, viewspaceNormal, currAo
#ifdef GI
			,
			currY, currCoCg
#endif
		);

#if defined(GI) && defined(TEMPORAL_DENOISER)
		float lerpFactor = rcp(srcAccumFrames[pxCoord] * 255);

		float4 prevY = srcPrevY[pxCoord];
		float2 prevCoCg = srcPrevCoCg[pxCoord];

		// Clamp young history against ghosting.
		[branch] if (lerpFactor >= 0.15)
		{
			float4 yL = srcPrevY[pxCoord + int2(-1, 0)];
			float4 yR = srcPrevY[pxCoord + int2(1, 0)];
			float4 yU = srcPrevY[pxCoord + int2(0, -1)];
			float4 yD = srcPrevY[pxCoord + int2(0, 1)];
			float2 cL = srcPrevCoCg[pxCoord + int2(-1, 0)];
			float2 cR = srcPrevCoCg[pxCoord + int2(1, 0)];
			float2 cU = srcPrevCoCg[pxCoord + int2(0, -1)];
			float2 cD = srcPrevCoCg[pxCoord + int2(0, 1)];

			float4 nMinY = min(min(min(yL, yR), min(yU, yD)), currY);
			float4 nMaxY = max(max(max(yL, yR), max(yU, yD)), currY);
			float2 nMinCoCg = min(min(min(cL, cR), min(cU, cD)), currCoCg);
			float2 nMaxCoCg = max(max(max(cL, cR), max(cU, cD)), currCoCg);

			prevY = clamp(prevY, nMinY, nMaxY);
			prevCoCg = clamp(prevCoCg, nMinCoCg, nMaxCoCg);
		}

		currY = lerp(prevY, currY, lerpFactor);
		currCoCg = lerp(prevCoCg, currCoCg, lerpFactor);
#endif
	}
#ifdef GI
	currY = filterNaN(currY);
	currCoCg = filterNaN(currCoCg);
#endif

	// Output is occlusion: 0=open, 1=occluded.
	outAo[pxCoord] = currAo;
#ifdef GI
	outY[pxCoord] = currY;
	outCoCg[pxCoord] = currCoCg;
#endif
}

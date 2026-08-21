// SPDX-License-Identifier: MIT
// Copyright (C) 2016-2021 Intel Corporation

#include "../Common/FastMath.hlsli"
#include "../Common/Math.hlsli"
#ifdef SSGI_BOUNCE
#include "../Common/Color.hlsli"
#include "../Common/SphericalHarmonics.hlsli"
#endif
#include "common.hlsli"

Texture2D<float> srcWorkingDepth : register(t0);
Texture2D<float3> srcNormal : register(t1);
#ifdef SSGI_BOUNCE
Texture2D<float3> srcRadiance : register(t2);
#endif
Texture2D<unorm float2> srcNoise : register(t3);
#ifdef SSGI_BOUNCE
Texture2D<unorm float> srcAccumFrames : register(t4);
Texture2D<float4> srcPrevIlY : register(t5);
Texture2D<float2> srcPrevIlCoCg : register(t6);
#endif

RWTexture2D<unorm float> outAo : register(u0);
#ifdef SSGI_BOUNCE
RWTexture2D<float4> outBounceSH : register(u1);
RWTexture2D<float2> outBounceCoCg : register(u2);
RWTexture2D<float3> outPrevGeo : register(u3);
#endif

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
	out float o_ao
#ifdef SSGI_BOUNCE
	, out float4 o_bounceSH, out float2 o_bounceCoCg
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

	if (dot(viewVec, pixCenterPos) > 0)
		viewspaceNormal = -viewspaceNormal;

	float visibility = 0;
#ifdef SSGI_BOUNCE
	float4 radianceY = 0;
	float2 radianceCoCg = 0;
#endif

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
#ifdef SSGI_BOUNCE
		uint bitmaskGI = 0;
#endif

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
				uint sampleBits = ((1 << bitsRange.y) - 1) << bitsRange.x;
				uint maskedBits = s < AORadius ? sampleBits : 0;

#ifdef SSGI_BOUNCE
				uint giBits = s < GIRadius ? sampleBits : 0;
				uint validBits = giBits & ~bitmaskGI;
				bitmaskGI |= giBits;
				if (validBits != 0) {
					float3 sampleNormal = normalize(
						srcNormal.SampleLevel(samplerPointClamp, sampleUV * frameScale, mipLevel));
					if (dot(samplePos, sampleNormal) > 0)
						sampleNormal = -sampleNormal;

					float frontBackMult = max(0, -dot(sampleNormal, sampleHorizonVec));
					if (frontBackMult > 0) {
						float angularWeight = countbits(validBits) * 0.03125;
						float3 sampleRadiance = max(
							0,
							srcRadiance.SampleLevel(
								samplerPointClamp, sampleUV * frameScale, mipLevel));
						sampleRadiance *= frontBackMult * angularWeight;

						float3 sampleYCoCg = Color::RGBToYCoCg(sampleRadiance);
						float3 horizonVecWS = ViewToWorldDirection(sampleHorizonVec);
						radianceY += sampleYCoCg.x * SphericalHarmonics::Evaluate(horizonVecWS);
						radianceCoCg += sampleYCoCg.yz;
					}
				}
#endif
				bitmask |= maskedBits;
			}
		}

		visibility += countbits(bitmask) * 0.03125;
	}

	float depthFade = GetDepthFade(viewspaceZ);

	visibility *= rcpNumSlices;
	visibility = lerp(saturate(visibility), 0, depthFade);

	o_ao = visibility;
#ifdef SSGI_BOUNCE
	radianceY *= rcpNumSlices;
	radianceCoCg *= rcpNumSlices;
	o_bounceSH = lerp(radianceY, 0, depthFade);
	o_bounceCoCg = radianceCoCg;
#endif
}

[numthreads(8, 8, 1)]
void main(const uint2 dtid : SV_DispatchThreadID)
{
	if (any(dtid >= uint2(OUT_FRAME_DIM)))
		return;

	uint2 pxCoord = dtid;
	float2 uv = (pxCoord + .5) * RCP_OUT_FRAME_DIM;

	float viewspaceZ = READ_DEPTH(srcWorkingDepth, pxCoord);
	float3 viewspaceNormal = srcNormal[pxCoord];

#ifdef SSGI_BOUNCE
	outPrevGeo[pxCoord] = float3(
		clamp(viewspaceZ, 0.0, R11_MAX_DEPTH),
		EncodeWorldNormal(ViewToWorldDirection(viewspaceNormal)));
#endif

	viewspaceZ *= 0.99920h;

	float currAo = 0;
#ifdef SSGI_BOUNCE
	float4 currBounceSH = 0;
	float2 currBounceCoCg = 0;
#endif

	bool needGI = viewspaceZ > FP_Z && viewspaceZ < DepthFadeRange.y;
	if (needGI) {
		CalculateGI(
			pxCoord, uv, viewspaceZ, viewspaceNormal, currAo
#ifdef SSGI_BOUNCE
			, currBounceSH, currBounceCoCg
#endif
		);

#ifdef SSGI_BOUNCE
		if (TemporalEnabled()) {
			// The reprojection pass floors accumulation at one, so this stays in range.
			float lerpFactor = saturate(rcp(max(srcAccumFrames[pxCoord] * 255, 1.0)));

			float4 prevY = srcPrevIlY[pxCoord];
			float2 prevCoCg = srcPrevIlCoCg[pxCoord];

			// Clamp history to its neighbourhood while the blend is still young.
			[branch] if (lerpFactor >= 0.15)
			{
				const int2 maxCoord = int2(OUT_FRAME_DIM) - 1;
				const int2 center = int2(pxCoord);
				const int2 left = clamp(center + int2(-1, 0), int2(0, 0), maxCoord);
				const int2 right = clamp(center + int2(1, 0), int2(0, 0), maxCoord);
				const int2 up = clamp(center + int2(0, -1), int2(0, 0), maxCoord);
				const int2 down = clamp(center + int2(0, 1), int2(0, 0), maxCoord);

				float4 yL = srcPrevIlY[left];
				float4 yR = srcPrevIlY[right];
				float4 yU = srcPrevIlY[up];
				float4 yD = srcPrevIlY[down];
				float2 cL = srcPrevIlCoCg[left];
				float2 cR = srcPrevIlCoCg[right];
				float2 cU = srcPrevIlCoCg[up];
				float2 cD = srcPrevIlCoCg[down];

				float4 minY = min(min(min(yL, yR), min(yU, yD)), currBounceSH);
				float4 maxY = max(max(max(yL, yR), max(yU, yD)), currBounceSH);
				float2 minCoCg = min(min(min(cL, cR), min(cU, cD)), currBounceCoCg);
				float2 maxCoCg = max(max(max(cL, cR), max(cU, cD)), currBounceCoCg);

				prevY = clamp(prevY, minY, maxY);
				prevCoCg = clamp(prevCoCg, minCoCg, maxCoCg);
			}

			currBounceSH = lerp(prevY, currBounceSH, lerpFactor);
			currBounceCoCg = lerp(prevCoCg, currBounceCoCg, lerpFactor);
		}
#endif
	}

	// Output is occlusion: 0=open, 1=occluded.
	outAo[pxCoord] = currAo;
#ifdef SSGI_BOUNCE
	outBounceSH[pxCoord] = currBounceSH;
	outBounceCoCg[pxCoord] = currBounceCoCg;
#endif
}

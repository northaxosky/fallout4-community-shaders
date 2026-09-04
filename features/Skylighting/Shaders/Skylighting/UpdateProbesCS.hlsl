// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 northaxosky

#include "../Common/SphericalHarmonics.hlsli"

Texture2D<float> SrcOcclusionDepth : register(t0);
RWTexture3D<sh2> OutProbeArray : register(u0);
RWTexture3D<uint> OutAccumFramesArray : register(u1);
SamplerComparisonState ComparisonSampler : register(s0);

cbuffer ProbeUpdateData : register(b0)
{
	row_major float4x4 OcclusionViewProj;
	float4 OcclusionDirection;
	float4 PosOffset;
	uint4 ArrayOrigin;
	int4 ValidMargin;
	float OcclusionExtent;
	float3 ProbeUpdatePadding;
};

static const uint3 ARRAY_DIM = uint3(256, 256, 128);
static const float PI = 3.14159265358979323846;
static const sh2 UNIT_SH = float4(sqrt(4.0 * PI), 0.0, 0.0, 0.0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	const float fadeInThreshold = 15.0;
	uint3 cellID =
		uint3(max(int3(dtid) - ArrayOrigin.xyz, 0) % ARRAY_DIM);
	uint3 validMin = (uint3)max(0, ValidMargin.xyz);
	uint3 validMax =
		ARRAY_DIM - 1 + (uint3)min(0, ValidMargin.xyz);
	bool isValid = all(cellID >= validMin) && all(cellID <= validMax);

	float3 arraySize = OcclusionExtent * float3(1.0, 1.0, 0.5);
	float3 cellCentreMS = cellID + 0.5 - ARRAY_DIM / 2;
	cellCentreMS =
		cellCentreMS / ARRAY_DIM * arraySize + PosOffset.xyz;

	float3 cellCentreOS =
		mul(OcclusionViewProj, float4(cellCentreMS, 1.0)).xyz;
	cellCentreOS.y = -cellCentreOS.y;
	float2 occlusionUV = cellCentreOS.xy * 0.5 + 0.5;

	if (all(occlusionUV > 0.0) && all(occlusionUV < 1.0)) {
		uint accumFrames =
			isValid ? (OutAccumFramesArray[dtid] + 1) : 1;
		float visibility = SrcOcclusionDepth.SampleCmpLevelZero(
			ComparisonSampler, occlusionUV, cellCentreOS.z);
		sh2 occlusionSH =
			SphericalHarmonics::Evaluate(OcclusionDirection.xyz) *
			(visibility * 4.0 * PI);
		if (isValid) {
			float lerpFactor = rcp((float)accumFrames);
			sh2 previousProbeSH = UNIT_SH;
			if (accumFrames > 1) {
				previousProbeSH +=
					(OutProbeArray[dtid] - UNIT_SH) *
					fadeInThreshold /
					min(fadeInThreshold, (float)accumFrames - 1.0);
			}
			occlusionSH = lerp(
				previousProbeSH, occlusionSH, lerpFactor);
		}
		occlusionSH = lerp(
			UNIT_SH,
			occlusionSH,
			min(fadeInThreshold, (float)accumFrames) /
				fadeInThreshold);

		OutProbeArray[dtid] = occlusionSH;
		OutAccumFramesArray[dtid] = accumFrames;
	} else if (!isValid) {
		OutProbeArray[dtid] = UNIT_SH;
		OutAccumFramesArray[dtid] = 0;
	}
}

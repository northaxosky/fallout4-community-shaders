// SPDX-License-Identifier: GPL-3.0-only
#include "Common/SharedData.hlsli"
#include "TerrainShadows/TerrainShadows.hlsli"
Texture2D<float> TexHeight : register(t0);
SamplerState TerrainSampler : register(s0);
RWByteAddressBuffer StatsOut : register(u0);

cbuffer ShadowStatisticsCB : register(b0)
{
	float2 PosRange : packoffset(c0.x);
	float2 ZRange : packoffset(c0.z);
}

static const uint kGridSize = 256;
static const float kFixedPointScale = 65535.0;

[numthreads(16, 16, 1)] void main(const uint3 dtid : SV_DispatchThreadID) {
	float2 uv = (float2(dtid.xy) + 0.5) / float(kGridSize);

	uint2 heightDims;
	TexHeight.GetDimensions(heightDims.x, heightDims.y);
	uint2 heightPx = min(uint2(uv * heightDims), heightDims - 1);
	float surfaceZ = lerp(PosRange.x, PosRange.y, TexHeight[heightPx]);

	float2 worldXY = (uv - SharedData::terraOccSettings.Offset) / SharedData::terraOccSettings.Scale.xy;
	float term = TerrainShadows::GetTerrainShadow(float3(worldXY, surfaceZ), TerrainSampler);
	uint fixedTerm = uint(round(term * kFixedPointScale));

	StatsOut.InterlockedAdd(0, 1);
	if (term < 0.99)
		StatsOut.InterlockedAdd(4, 1);
	if (term < 0.95)
		StatsOut.InterlockedAdd(8, 1);
	if (term < 0.75)
		StatsOut.InterlockedAdd(12, 1);
	if (term < 0.5)
		StatsOut.InterlockedAdd(16, 1);
	StatsOut.InterlockedAdd(20, fixedTerm);
	StatsOut.InterlockedMin(24, fixedTerm);
	StatsOut.InterlockedMax(28, fixedTerm);
}

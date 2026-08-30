// SPDX-License-Identifier: GPL-3.0-only
Texture2D<float> TexHeight : register(t0);
Texture2D<float2> TexShadow : register(t1);
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

	uint2 shadowDims;
	TexShadow.GetDimensions(shadowDims.x, shadowDims.y);
	uint2 shadowPx = min(uint2(uv * shadowDims), shadowDims - 1);
	float2 shadowRaw = TexShadow[shadowPx];
	// Same decode and -256 bias as TerrainShadows::GetTerrainZ.
	float2 shadowHeight = lerp(ZRange.xx, ZRange.yy, shadowRaw) - 256.0;

	float penumbra = max(shadowHeight.x - shadowHeight.y, 1e-3);
	float term = saturate((surfaceZ - shadowHeight.y) / penumbra);
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

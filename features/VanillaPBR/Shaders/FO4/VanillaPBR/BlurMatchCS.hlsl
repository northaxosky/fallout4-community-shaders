#include "Common/Color.hlsli"
#include "FO4/VanillaPBR/BlurMatch.hlsli"

StructuredBuffer<float4> Partials : register(t0);
Texture2DArray<float4> AuthoredFaces : register(t1);
Texture2DArray<float4> LiveFaces : register(t2);
SamplerState LinearClamp : register(s0);
// Fractional live LOD per authored slot (row) and authored mip (column).
RWTexture2D<float> MatchedLOD : register(u0);

cbuffer MatchCB : register(b0)
{
	uint LiveTarget;
	uint LiveMips;
	uint AuthoredMips;
	uint SlotCount;
	uint2 SlotReady;
	uint2 MatchPad;
};

groupshared float LiveDetail[BLUR_MATCH_MAX_MIPS];
groupshared float SlotDetail[BLUR_MATCH_MAX_MIPS][BLUR_MATCH_MAX_SLOTS];
groupshared float SlotMean[BLUR_MATCH_MAX_SLOTS];
groupshared float LiveLuminance;

struct Detail
{
	float ratio;
	float mean;
};

// Detail q: RMS first difference over RMS within-face deviation, six faces.
Detail Measure(uint target, uint mip)
{
	const float facePixels = float(BlurMatch::kGrid * BlurMatch::kGrid);
	const float differences = 2.0 * 6.0 * float(BlurMatch::kGrid * (BlurMatch::kGrid - 1));
	float variance = 0.0, gradient = 0.0, mean = 0.0;
	[unroll] for (uint face = 0; face < 6; ++face)
	{
		float3 sums = 0.0;
		float shift = 0.0;
		[unroll] for (uint group = 0; group < BlurMatch::kGroupsPerFace; ++group)
		{
			float4 partial = Partials[BlurMatch::PartialIndex(target, mip, face, group)];
			sums += partial.xyz;
			shift = partial.w;
		}
		float faceMean = sums.x / facePixels;
		variance += max(sums.y / facePixels - faceMean * faceMean, 0.0);
		gradient += sums.z;
		mean += faceMean + shift;
	}
	Detail detail;
	detail.ratio = sqrt(gradient / differences) / max(sqrt(variance / 6.0), 1e-10);
	detail.mean = mean / 6.0;
	return detail;
}

// The composite's six last-mip axis samples hit the face centers.
float3 FaceCenter(Texture2DArray<float4> faces, uint slice, uint mips)
{
	return faces.SampleLevel(LinearClamp, float3(0.5, 0.5, slice), mips - 1).rgb;
}

// Lowest fractional live mip with this detail; flatter maps to the last.
float EquivalentLOD(float ratio)
{
	float highest = 0.0;
	for (uint mip = 0; mip + 1 < LiveMips; ++mip) {
		float a = LiveDetail[mip];
		float b = LiveDetail[mip + 1];
		highest = max(highest, max(a, b));
		if (a != b && min(a, b) <= ratio && ratio <= max(a, b))
			return float(mip) + (ratio - a) / (b - a);
	}
	return ratio > highest ? 0.0 : float(LiveMips - 1);
}

// One thread per authored slot and mip measures; then one per slot matches.
[numthreads(BLUR_MATCH_MAX_SLOTS, BLUR_MATCH_MAX_MIPS, 1)] void main(uint3 threadId : SV_GroupThreadID) {
	const uint slot = threadId.x;
	const uint mip = threadId.y;
	if (slot == 0 && mip < LiveMips)
		LiveDetail[mip] = Measure(LiveTarget, mip).ratio;
	if (slot == 0 && mip == 0) {
		float luminance = 0.0;
		[unroll] for (uint face = 0; face < 6; ++face)
			luminance += Color::RGBToLuminance(FaceCenter(LiveFaces, face, LiveMips));
		LiveLuminance = luminance / 6.0;
	}
	const bool ready = slot < SlotCount && mip < AuthoredMips &&
	                   (((slot < 32 ? SlotReady.x : SlotReady.y) >> (slot & 31)) & 1) != 0;
	Detail detail = (Detail)0;
	[branch] if (ready)
		detail = Measure(slot, mip);
	SlotDetail[mip][slot] = detail.ratio;
	if (mip == 0)
		SlotMean[slot] = detail.mean;
	GroupMemoryBarrierWithGroupSync();
	if (mip != 0 || slot >= SlotCount)
		return;

	// Unprofiled or empty slots keep the resolution-only LOD (zero here).
	const bool measured = ready && SlotMean[slot] > 1e-6;
	float matched = 0.0;
	for (uint level = 0; level < AuthoredMips; ++level) {
		float ratio = measured ? SlotDetail[level][slot] : 0.0;
		// Authored mips never get sharper, so neither does their match.
		if (measured)
			matched = max(matched, EquivalentLOD(ratio));
		MatchedLOD[uint2(level, slot)] = matched;
	}
	float3 average = 0.0;
	[unroll] for (uint face = 0; face < 6; ++face)
		average += FaceCenter(AuthoredFaces, slot * 6 + face, AuthoredMips);
	average /= 6.0;
	[unroll] for (uint channel = 0; channel < 3; ++channel)
		MatchedLOD[uint2(BLUR_MATCH_MAX_MIPS + channel, slot)] = average[channel];
	MatchedLOD[uint2(BLUR_MATCH_MAX_MIPS + 3, slot)] = LiveLuminance;
}
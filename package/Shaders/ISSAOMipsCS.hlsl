// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
Texture2D<float4> source : register(t0);
RWTexture2D<float4> Output0 : register(u0);
RWTexture2D<float4> Output1 : register(u1);

groupshared float GroupMinimum[64];

[numthreads(8, 8, 1)] void main(uint3 dispatchId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID) {
	uint groupIndex = groupThreadId.y * 8 + groupThreadId.x;
	uint2 base = dispatchId.xy * 2.0;
	float z00 = source.Load(int3(base, 0)).x;
	float z01 = source.Load(int3(base + uint2(0, 1), 0)).x;
	float z10 = source.Load(int3(base + uint2(1, 0), 0)).x;
	float z11 = source.Load(int3(base + uint2(1, 1), 0)).x;
	float minimum = min(min(z00, z01), min(z10, z11));
	GroupMinimum[groupIndex] = minimum;
	GroupMemoryBarrierWithGroupSync();
	Output0[dispatchId.xy] = minimum;
	if ((dispatchId.x & 1) == 0 && (dispatchId.y & 1) == 0) {
		float right = GroupMinimum[groupIndex + 1];
		float below = GroupMinimum[groupIndex + 8];
		float diagonal = GroupMinimum[groupIndex + 9];
		minimum = min(min(minimum, right), min(below, diagonal));
		Output1[dispatchId.xy * 0.5] = minimum;
	}
}
#endif

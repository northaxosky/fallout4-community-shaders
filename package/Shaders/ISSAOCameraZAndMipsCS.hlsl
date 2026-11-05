// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer ISSAOCameraZAndMipsCSConstants : register(b0)
{
	float4 g_ClipInfos;
};

Texture2D<float4> Depth : register(t0);
RWTexture2D<float4> Output0 : register(u0);
RWTexture2D<float4> Output1 : register(u1);
RWTexture2D<float4> Output2 : register(u2);

groupshared float GroupMinimum[64];

float CameraZ(int2 position)
{
	float depth = Depth.Load(int3(position, 0)).x;
	return g_ClipInfos.x / (g_ClipInfos.y * depth + g_ClipInfos.z);
}

[numthreads(8, 8, 1)] void main(uint3 dispatchId : SV_DispatchThreadID, uint3 groupThreadId : SV_GroupThreadID) {
	uint2 base = dispatchId.xy * 2.0;
	int2 position00 = base;
	int2 position10 = base + int2(1, 0);
	int2 position01 = base + int2(0, 1);
	int2 position11 = base + int2(1, 1);
	float z00 = CameraZ(position00);
	float z10 = CameraZ(position10);
	float z01 = CameraZ(position01);
	float z11 = CameraZ(position11);
	float minimum = min(min(z00, z10), min(z01, z11));
	Output0[position00] = z00;
	Output0[position10] = z10;
	Output0[position01] = z01;
	Output0[position11] = z11;
	uint groupIndex = groupThreadId.y * 8 + groupThreadId.x;
	GroupMinimum[groupIndex] = minimum;
	GroupMemoryBarrierWithGroupSync();
	Output1[dispatchId.xy] = minimum;
	if ((dispatchId.x & 1) == 0 && (dispatchId.y & 1) == 0) {
		float right = GroupMinimum[groupIndex + 1];
		float below = GroupMinimum[groupIndex + 8];
		float diagonal = GroupMinimum[groupIndex + 9];
		minimum = min(min(minimum, right), min(below, diagonal));
		Output2[dispatchId.xy * 0.5] = minimum;
	}
}
#endif

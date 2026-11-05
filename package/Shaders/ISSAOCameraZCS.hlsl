// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
cbuffer ISSAOCameraZCSConstants : register(b0)
{
	float4 g_ClipInfos;
};

Texture2D<float4> Depth : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
	float depth = Depth.Load(int3(dispatchId.xy, 0)).x;
	Output[dispatchId.xy] = g_ClipInfos.x / (g_ClipInfos.y * depth + g_ClipInfos.z);
}
#endif

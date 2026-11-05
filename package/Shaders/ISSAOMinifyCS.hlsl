// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 northaxosky

#ifdef CSHADER
Texture2D<float4> source : register(t0);
RWTexture2D<float4> Output : register(u0);

[numthreads(16, 16, 1)] void main(uint3 dispatchId : SV_DispatchThreadID) {
	uint2 ssP = dispatchId.xy * 2.0;
	uint2 offset = float2((int)((ssP.y & 1) ^ 1), (int)((ssP.x & 1) ^ 1));
	Output[dispatchId.xy] = source.Load(int3(ssP + offset, 0)).x;
}
#endif

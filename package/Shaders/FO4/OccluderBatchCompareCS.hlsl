// SPDX-License-Identifier: GPL-3.0-only
// Counts differing D16 texels between the engine and batched occlusion maps.

cbuffer Compare : register(b0)
{
	uint2 Extent;
}

Texture2D<float> Engine : register(t0);
Texture2D<float> Batch : register(t1);
// Mismatched texels, largest D16 step difference, batch-nearer texels, engine-nearer texels,
// then the mismatch bounds as min x, min y, max x, max y.
RWByteAddressBuffer Result : register(u0);

[numthreads(8, 8, 1)] void main(uint2 texel : SV_DispatchThreadID) {
	if (any(texel >= Extent))
		return;
	int engine = (int)round(Engine.Load(int3(texel, 0)) * 65535.0);
	int batch = (int)round(Batch.Load(int3(texel, 0)) * 65535.0);
	uint delta = (uint)abs(engine - batch);
	if (delta == 0)
		return;
	uint ignored;
	Result.InterlockedAdd(0, 1, ignored);
	Result.InterlockedMax(4, delta, ignored);
	Result.InterlockedAdd(batch < engine ? 8 : 12, 1, ignored);
	Result.InterlockedMin(16, texel.x, ignored);
	Result.InterlockedMin(20, texel.y, ignored);
	Result.InterlockedMax(24, texel.x, ignored);
	Result.InterlockedMax(28, texel.y, ignored);
}

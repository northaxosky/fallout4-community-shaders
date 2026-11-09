// Resamples an FO4 map (bottom-left square of its slice) into the cache layout.
// Omni hemispheres stack front above back; depth is min-reduced per cache texel.

Texture2DArray<float> SourceShadowMaps : register(t0);
RWTexture2DArray<float> CacheShadowMaps : register(u0);

cbuffer CopyCB : register(b0)
{
	uint FrontSlice;
	uint BackSlice;
	uint TargetSlice;
	uint Omni;
	uint TargetSize;
	uint MapSize;
	uint SourceSize;
	uint pad0;
}

// Source texel interval covering [lo, hi) map-space texels, never empty.
uint2 GetFootprint(float lo, float hi)
{
	const float epsilon = 1e-3;
	const uint start = (uint)floor(lo + epsilon);
	return uint2(start, max((uint)ceil(hi - epsilon), start + 1));
}

[numthreads(8, 8, 1)] void main(uint2 dispatchThreadId : SV_DispatchThreadID) {
	if (any(dispatchThreadId >= TargetSize))
		return;

	const float2 uvMin = dispatchThreadId / (float)TargetSize;
	const float2 uvMax = (dispatchThreadId + 1) / (float)TargetSize;

	uint slice = FrontSlice;
	bool back = false;
	// Map-space v range, 0 at the paraboloid pole side for omni lights.
	float2 mapV = float2(uvMin.y, uvMax.y);
	[branch] if (Omni != 0)
	{
		back = dispatchThreadId.y * 2 >= TargetSize;
		slice = back ? BackSlice : FrontSlice;
		mapV = back ? 2.0 * (1.0 - float2(uvMax.y, uvMin.y)) : 2.0 * mapV;
	}

	const float mapSize = MapSize;
	const float sourceSize = SourceSize;
	// Front rows grow up from the corner row, back rows down from the bottom.
	const float2 rows = back ? sourceSize - mapV.yx * mapSize : sourceSize - mapSize + mapV * mapSize;
	const uint2 columnRange = GetFootprint(uvMin.x * mapSize, uvMax.x * mapSize);
	const uint2 rowRange = GetFootprint(rows.x, rows.y);

	const uint lastRow = SourceSize - 1;
	const uint lastColumn = MapSize - 1;
	float depth = 1.0;
	[loop] for (uint row = rowRange.x; row < rowRange.y; row++)
	{
		[loop] for (uint column = columnRange.x; column < columnRange.y; column++)
		{
			depth = min(depth, SourceShadowMaps.Load(int4(min(column, lastColumn), min(row, lastRow), slice, 0)));
		}
	}
	CacheShadowMaps[uint3(dispatchThreadId, TargetSlice)] = depth;
}

cbuffer TelemetryCB : register(b0)
{
	uint2 gSourceExtent;
	uint2 gOutputExtent;
};

Texture2D<float> gWetnessMask : register(t0);
RWTexture2D<float4> gStats : register(u0);

[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID)
{
	if (any(id >= gOutputExtent)) {
		return;
	}

	uint2 begin = id * gSourceExtent / gOutputExtent;
	uint2 end = (id + 1) * gSourceExtent / gOutputExtent;
	float sum = 0.0;
	float covered = 0.0;
	float count = 0.0;
	for (uint y = begin.y; y < end.y; ++y) {
		for (uint x = begin.x; x < end.x; ++x) {
			float wetness = gWetnessMask.Load(int3(uint2(x, y), 0));
			if (!isfinite(wetness)) {
				wetness = 0.0;
			}
			sum += wetness;
			covered += wetness > 0.5 ? 1.0 : 0.0;
			count += 1.0;
		}
	}

	gStats[id] = float4(sum, covered, count, 0.0);
}

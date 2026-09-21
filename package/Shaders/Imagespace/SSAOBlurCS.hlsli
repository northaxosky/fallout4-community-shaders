#if GRID_SIZE != 972 && GRID_SIZE != 552
#error "GRID_SIZE must be 972 or 552"
#endif

cbuffer BlurParameters : register(b0)
{
	float4 TargetSize;
};

Texture2D<float4> Source : register(t0);
RWTexture2D<float4> Target : register(u0);

groupshared float2 Samples[GRID_SIZE];

#if GRID_SIZE == 972
#define SSAO_BLUR_AXIS x
#define SSAO_BLUR_TILE_LENGTH 960
[numthreads(972, 1, 1)]
#else
#define SSAO_BLUR_AXIS y
#define SSAO_BLUR_TILE_LENGTH 540
[numthreads(1, 552, 1)]
#endif

void main(
	uint3 groupID : SV_GroupID,
	uint3 groupThreadID : SV_GroupThreadID,
	uint3 dispatchThreadID : SV_DispatchThreadID)
{
	int lane = int(groupThreadID.SSAO_BLUR_AXIS) - 6;
	uint limit = (uint)TargetSize.SSAO_BLUR_AXIS;
#if GRID_SIZE == 972
	uint2 pixel = uint2(groupID.x * 960 + lane, groupID.y);
#else
	uint2 pixel = uint2(groupID.x, groupID.y * 540 + lane);
#endif
	float3 source = Source.Load(int3(pixel, 0)).xyz;
	float2 own = float2(
		source.x,
		dot(
			source.yz,
			float2(asfloat(0x3f7f00ff), asfloat(0x3b7f00ff))));
	Samples[groupThreadID.SSAO_BLUR_AXIS] = own;
	GroupMemoryBarrierWithGroupSync();

	if (lane >= 0 && lane < SSAO_BLUR_TILE_LENGTH && pixel.SSAO_BLUR_AXIS < limit)
	{
		if (own.y == 1)
		{
			Target[dispatchThreadID.xy] = 0;
			return;
		}

		float2 tap = Samples[lane];
		float weight = max(1 - abs(tap.y - own.y) * 2000, 0);
		float factor = weight * asfloat(0x3ec92a74);
		float sum = own.x * asfloat(0x3e1cd899);
		sum = mad(tap.x, factor, sum);
		float total = weight * asfloat(0x3ec92a74) + asfloat(0x3e1cd899);
		int4 indices = int(groupThreadID.SSAO_BLUR_AXIS) + int4(-4, -2, 2, 4);

		tap = Samples[indices.x];
		weight = max(1 - abs(tap.y - own.y) * 2000, 0);
		sum += tap.x * (weight * asfloat(0x3ed86574));
		total += weight * asfloat(0x3ed86574);
		tap = Samples[indices.y];
		weight = max(1 - abs(tap.y - own.y) * 2000, 0);
		sum += tap.x * (weight * 0.444893);
		total += weight * 0.444893;
		tap = Samples[indices.z];
		weight = max(1 - abs(tap.y - own.y) * 2000, 0);
		sum += tap.x * (weight * 0.444893);
		total += weight * 0.444893;
		tap = Samples[indices.w];
		weight = max(1 - abs(tap.y - own.y) * 2000, 0);
		sum += tap.x * (weight * asfloat(0x3ed86574));
		total += weight * asfloat(0x3ed86574);
		tap = Samples[groupThreadID.SSAO_BLUR_AXIS + 6];
		weight = max(1 - abs(tap.y - own.y) * 2000, 0);
		sum += tap.x * (weight * asfloat(0x3ec92a74));
		total += weight * asfloat(0x3ec92a74);

#if GRID_SIZE == 972
		Target[pixel] = float4(sum / (total + 0.0001), source.yz, 0);
#else
		Target[pixel] = sum / (total + 0.0001);
#endif
	}
}

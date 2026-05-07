// 16x16 sparse-sample mean luminance into a 1x1 R32F UAV.

Texture2D<float4>   InputColor : register(t0);
RWTexture2D<float>  OutputLuma : register(u0);

cbuffer LumCB : register(b0)
{
	uint2 InputDimensions;
	uint2 _pad0;
};

groupshared float gs_luma[256];

[numthreads(16, 16, 1)] void main(uint3 gtid : SV_GroupThreadID)
{
	const uint linearIdx = gtid.y * 16 + gtid.x;

	const float2 uv = (float2(gtid.xy) + 0.5) / 16.0;
	const int2   px = int2(uv * float2(InputDimensions));
	const float3 color = InputColor.Load(int3(px, 0)).rgb;
	const float  luma = dot(color, float3(0.2126, 0.7152, 0.0722));

	gs_luma[linearIdx] = luma;
	GroupMemoryBarrierWithGroupSync();

	[unroll] for (uint stride = 128; stride > 0; stride >>= 1) {
		if (linearIdx < stride)
			gs_luma[linearIdx] += gs_luma[linearIdx + stride];
		GroupMemoryBarrierWithGroupSync();
	}

	if (linearIdx == 0)
		OutputLuma[uint2(0, 0)] = gs_luma[0] / 256.0;
}
